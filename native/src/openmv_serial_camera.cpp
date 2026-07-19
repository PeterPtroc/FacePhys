#include "facephys/openmv_serial_camera.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csetjmp>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#ifdef FACEPHYS_HAVE_JPEG
#include <jpeglib.h>
#endif

namespace facephys {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'F', 'P', 'M', 'V'}};
constexpr std::size_t kHeaderBytes = 20U;
constexpr std::size_t kReadChunkBytes = 8192U;

std::uint16_t read_u16(const std::uint8_t* source) {
  return static_cast<std::uint16_t>(source[0]) |
         (static_cast<std::uint16_t>(source[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* source) {
  return static_cast<std::uint32_t>(source[0]) |
         (static_cast<std::uint32_t>(source[1]) << 8U) |
         (static_cast<std::uint32_t>(source[2]) << 16U) |
         (static_cast<std::uint32_t>(source[3]) << 24U);
}

std::uint64_t monotonic_ns() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

std::string errno_text(const std::string& context) {
  return context + ": " + std::strerror(errno);
}

#ifdef FACEPHYS_HAVE_JPEG
struct JpegError {
  jpeg_error_mgr base;
  std::jmp_buf jump;
};

extern "C" void openmv_jpeg_failure(j_common_ptr context) {
  std::longjmp(reinterpret_cast<JpegError*>(context->err)->jump, 1);
}
#endif

}  // namespace

OpenMvSerialCamera::~OpenMvSerialCamera() { close(); }

bool OpenMvSerialCamera::open(const CameraConfig& config, std::string* error) {
  close();
#ifndef FACEPHYS_HAVE_JPEG
  if (error) *error = "openmv_serial requires libjpeg-turbo; install libjpeg-dev and rebuild";
  return false;
#else
  fd_ = ::open(config.device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (fd_ < 0) {
    if (error) *error = errno_text("open OpenMV serial device " + config.device);
    return false;
  }
  termios attributes{};
  if (tcgetattr(fd_, &attributes) != 0) {
    if (error) *error = errno_text("tcgetattr " + config.device);
    close();
    return false;
  }
  cfmakeraw(&attributes);
  // USB CDC has no physical baud rate; B115200 is retained for tty drivers
  // that require a valid termios speed field.
  cfsetispeed(&attributes, B115200);
  cfsetospeed(&attributes, B115200);
  attributes.c_cflag |= CLOCAL | CREAD;
  attributes.c_cc[VMIN] = 0;
  attributes.c_cc[VTIME] = 0;
  if (tcsetattr(fd_, TCSANOW, &attributes) != 0 || tcflush(fd_, TCIFLUSH) != 0) {
    if (error) *error = errno_text("configure OpenMV serial device " + config.device);
    close();
    return false;
  }
  timeout_ms_ = config.serial_timeout_ms;
  max_frame_bytes_ = static_cast<std::size_t>(config.max_frame_bytes);
  receive_buffer_.clear();
  receive_buffer_.reserve(max_frame_bytes_ + kHeaderBytes + kReadChunkBytes);
  width_ = config.width;
  height_ = config.height;
  fallback_sequence_ = 0;
  return true;
#endif
}

void OpenMvSerialCamera::close() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
  width_ = 0;
  height_ = 0;
  fallback_sequence_ = 0;
  receive_buffer_.clear();
}

bool OpenMvSerialCamera::read_available(int timeout_ms, std::string* error) {
  pollfd descriptor{fd_, POLLIN, 0};
  const int ready = poll(&descriptor, 1, timeout_ms);
  if (ready == 0) {
    if (error) *error = "OpenMV serial frame timeout";
    return false;
  }
  if (ready < 0) {
    if (error) *error = errno_text("poll OpenMV serial device");
    return false;
  }
  if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
    if (error) *error = "OpenMV serial device disconnected or in error state";
    return false;
  }
  std::array<std::uint8_t, kReadChunkBytes> chunk{};
  while (true) {
    const ssize_t count = ::read(fd_, chunk.data(), chunk.size());
    if (count > 0) {
      receive_buffer_.insert(receive_buffer_.end(), chunk.begin(), chunk.begin() + count);
      continue;
    }
    // Non-blocking tty drivers may return zero after a readiness transition
    // without meaning end-of-file. Actual disconnects are handled above via
    // POLLHUP/POLLERR, so treat this as a drained receive queue.
    if (count == 0) break;
    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    if (errno == EINTR) continue;
    if (error) *error = errno_text("read OpenMV serial device");
    return false;
  }
  // A corrupt stream must never turn into an unbounded buffer. Retain enough
  // tail bytes to find a split packet magic on the following read.
  const std::size_t maximum_buffer = max_frame_bytes_ * 2U + kHeaderBytes;
  if (receive_buffer_.size() > maximum_buffer) {
    receive_buffer_.erase(receive_buffer_.begin(), receive_buffer_.end() - 3);
  }
  return true;
}

bool OpenMvSerialCamera::extract_newest_packet(Packet* packet, std::string* error) {
  bool found = false;
  while (true) {
    const auto magic = std::search(receive_buffer_.begin(), receive_buffer_.end(),
                                   kMagic.begin(), kMagic.end());
    if (magic == receive_buffer_.end()) {
      if (receive_buffer_.size() > 3U) {
        receive_buffer_.erase(receive_buffer_.begin(), receive_buffer_.end() - 3);
      }
      return found;
    }
    if (magic != receive_buffer_.begin()) receive_buffer_.erase(receive_buffer_.begin(), magic);
    if (receive_buffer_.size() < kHeaderBytes) return found;
    const int packet_width = static_cast<int>(read_u16(receive_buffer_.data() + 4));
    const int packet_height = static_cast<int>(read_u16(receive_buffer_.data() + 6));
    const std::uint32_t packet_sequence = read_u32(receive_buffer_.data() + 8);
    const std::uint32_t jpeg_bytes = read_u32(receive_buffer_.data() + 16);
    if (packet_width <= 0 || packet_width > 1024 || packet_height <= 0 || packet_height > 1024 ||
        jpeg_bytes < 4U || jpeg_bytes > max_frame_bytes_) {
      receive_buffer_.erase(receive_buffer_.begin());
      continue;
    }
    const std::size_t packet_bytes = kHeaderBytes + static_cast<std::size_t>(jpeg_bytes);
    if (receive_buffer_.size() < packet_bytes) return found;
    Packet candidate;
    candidate.width = packet_width;
    candidate.height = packet_height;
    candidate.sequence = packet_sequence;
    candidate.jpeg.assign(receive_buffer_.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes),
                          receive_buffer_.begin() + static_cast<std::ptrdiff_t>(packet_bytes));
    receive_buffer_.erase(receive_buffer_.begin(), receive_buffer_.begin() + static_cast<std::ptrdiff_t>(packet_bytes));
    *packet = std::move(candidate);
    found = true;
  }
  (void)error;
}

bool OpenMvSerialCamera::decode_jpeg(const Packet& packet, RgbFrame* frame, std::string* error) const {
#ifndef FACEPHYS_HAVE_JPEG
  (void)packet;
  (void)frame;
  if (error) *error = "openmv_serial requires libjpeg-turbo";
  return false;
#else
  jpeg_decompress_struct decoder{};
  JpegError failure{};
  decoder.err = jpeg_std_error(&failure.base);
  failure.base.error_exit = openmv_jpeg_failure;
  if (setjmp(failure.jump) != 0) {
    jpeg_destroy_decompress(&decoder);
    if (error) *error = "libjpeg failed to decode OpenMV JPEG packet";
    return false;
  }
  jpeg_create_decompress(&decoder);
  jpeg_mem_src(&decoder, const_cast<unsigned char*>(packet.jpeg.data()),
               static_cast<unsigned long>(packet.jpeg.size()));
  jpeg_read_header(&decoder, TRUE);
  decoder.out_color_space = JCS_RGB;
  jpeg_start_decompress(&decoder);
  if (decoder.output_width != static_cast<unsigned int>(packet.width) ||
      decoder.output_height != static_cast<unsigned int>(packet.height)) {
    jpeg_destroy_decompress(&decoder);
    if (error) *error = "OpenMV packet dimensions do not match JPEG dimensions";
    return false;
  }
  if (frame->width != packet.width || frame->height != packet.height) frame->resize(packet.width, packet.height);
  while (decoder.output_scanline < decoder.output_height) {
    JSAMPROW row = frame->rgb.data() + static_cast<std::size_t>(decoder.output_scanline) * decoder.output_width * 3U;
    jpeg_read_scanlines(&decoder, &row, 1);
  }
  jpeg_finish_decompress(&decoder);
  jpeg_destroy_decompress(&decoder);
  return true;
#endif
}

bool OpenMvSerialCamera::capture(RgbFrame* frame, std::string* error) {
  if (fd_ < 0) {
    if (error) *error = "capture requested on closed OpenMV serial camera";
    return false;
  }
  Packet packet;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
  while (!extract_newest_packet(&packet, error)) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      if (error) *error = "OpenMV serial packet assembly timeout";
      return false;
    }
    const int remaining = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
    if (!read_available(std::max(1, remaining), error)) return false;
  }
  if (!decode_jpeg(packet, frame, error)) return false;
  width_ = frame->width;
  height_ = frame->height;
  frame->timestamp_ns = monotonic_ns();
  frame->sequence = ++fallback_sequence_;
  return true;
}

}  // namespace facephys
