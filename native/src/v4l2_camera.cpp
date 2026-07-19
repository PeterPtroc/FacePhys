#include "facephys/v4l2_camera.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef FACEPHYS_HAVE_JPEG
#include <csetjmp>
#include <jpeglib.h>
#endif

namespace facephys {
namespace {
bool ioctl_retry(int fd, unsigned long request, void* argument) {
  int result = 0;
  do { result = ioctl(fd, request, argument); } while (result == -1 && errno == EINTR);
  return result != -1;
}
std::string errno_text(const std::string& context) { return context + ": " + std::strerror(errno); }
std::uint8_t clamp_byte(int value) { return static_cast<std::uint8_t>(std::clamp(value, 0, 255)); }
std::uint64_t monotonic_ns() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}
#ifdef FACEPHYS_HAVE_JPEG
struct JpegError { jpeg_error_mgr base; std::jmp_buf jump; };
extern "C" void jpeg_failure(j_common_ptr context) { std::longjmp(reinterpret_cast<JpegError*>(context->err)->jump, 1); }
#endif
}  // namespace

V4L2Camera::~V4L2Camera() { close(); }

bool V4L2Camera::open(const CameraConfig& config, std::string* error) {
  close();
  fd_ = ::open(config.device.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
  if (fd_ < 0) { if (error) *error = errno_text("open " + config.device); return false; }
  v4l2_capability capability{};
  if (!ioctl_retry(fd_, VIDIOC_QUERYCAP, &capability) ||
      !(capability.capabilities & V4L2_CAP_VIDEO_CAPTURE) || !(capability.capabilities & V4L2_CAP_STREAMING)) {
    if (error) *error = "camera lacks V4L2 video-capture streaming support";
    close(); return false;
  }
  v4l2_format format{};
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  format.fmt.pix.width = static_cast<std::uint32_t>(config.width);
  format.fmt.pix.height = static_cast<std::uint32_t>(config.height);
  format.fmt.pix.field = V4L2_FIELD_ANY;
  format.fmt.pix.pixelformat = config.pixel_format == "MJPEG" ? V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_YUYV;
  if (!ioctl_retry(fd_, VIDIOC_S_FMT, &format)) { if (error) *error = errno_text("VIDIOC_S_FMT"); close(); return false; }
  if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV && format.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
    if (error) *error = "camera chose unsupported pixel format (only YUYV/MJPEG are supported)";
    close(); return false;
  }
  width_ = static_cast<int>(format.fmt.pix.width);
  height_ = static_cast<int>(format.fmt.pix.height);
  pixel_format_ = format.fmt.pix.pixelformat;
  v4l2_streamparm parameters{};
  parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  parameters.parm.capture.timeperframe.numerator = 1;
  parameters.parm.capture.timeperframe.denominator = static_cast<std::uint32_t>(config.fps);
  (void)ioctl_retry(fd_, VIDIOC_S_PARM, &parameters);  // best effort; drivers may fix their own FPS.
  v4l2_requestbuffers request{};
  request.count = 3;
  request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  request.memory = V4L2_MEMORY_MMAP;
  if (!ioctl_retry(fd_, VIDIOC_REQBUFS, &request) || request.count < 2) { if (error) *error = errno_text("VIDIOC_REQBUFS"); close(); return false; }
  buffers_.resize(request.count);
  for (std::uint32_t i = 0; i < request.count; ++i) {
    v4l2_buffer buffer{};
    buffer.type = request.type; buffer.memory = request.memory; buffer.index = i;
    if (!ioctl_retry(fd_, VIDIOC_QUERYBUF, &buffer)) { if (error) *error = errno_text("VIDIOC_QUERYBUF"); close(); return false; }
    buffers_[i].length = buffer.length;
    buffers_[i].address = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buffer.m.offset);
    if (buffers_[i].address == MAP_FAILED) { buffers_[i].address = nullptr; if (error) *error = errno_text("mmap camera buffer"); close(); return false; }
    if (!ioctl_retry(fd_, VIDIOC_QBUF, &buffer)) { if (error) *error = errno_text("VIDIOC_QBUF"); close(); return false; }
  }
  auto type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (!ioctl_retry(fd_, VIDIOC_STREAMON, &type)) { if (error) *error = errno_text("VIDIOC_STREAMON"); close(); return false; }
  sequence_ = 0;
  return true;
}

void V4L2Camera::close() {
  if (fd_ >= 0) {
    auto type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    (void)ioctl_retry(fd_, VIDIOC_STREAMOFF, &type);
  }
  for (auto& buffer : buffers_) {
    if (buffer.address != nullptr) munmap(buffer.address, buffer.length);
  }
  buffers_.clear();
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1; width_ = 0; height_ = 0; pixel_format_ = 0;
}

bool V4L2Camera::convert_yuyv(const std::uint8_t* source, std::size_t bytes, RgbFrame* destination, std::string* error) const {
  const std::size_t required = static_cast<std::size_t>(width_) * height_ * 2U;
  if (bytes < required) { if (error) *error = "short YUYV V4L2 frame"; return false; }
  if (destination->width != width_ || destination->height != height_) destination->resize(width_, height_);
  for (std::size_t input = 0, pixel = 0; input < required; input += 4U, pixel += 2U) {
    const int y0 = source[input] - 16; const int u = source[input + 1U] - 128;
    const int y1 = source[input + 2U] - 16; const int v = source[input + 3U] - 128;
    const auto convert = [u, v](int y, std::uint8_t* rgb) {
      const int scaled = std::max(y, 0) * 298;
      rgb[0] = clamp_byte((scaled + 409 * v + 128) >> 8);
      rgb[1] = clamp_byte((scaled - 100 * u - 208 * v + 128) >> 8);
      rgb[2] = clamp_byte((scaled + 516 * u + 128) >> 8);
    };
    convert(y0, destination->rgb.data() + pixel * 3U);
    convert(y1, destination->rgb.data() + (pixel + 1U) * 3U);
  }
  return true;
}

bool V4L2Camera::convert_mjpeg(const std::uint8_t* source, std::size_t bytes, RgbFrame* destination, std::string* error) const {
#ifndef FACEPHYS_HAVE_JPEG
  (void)source; (void)bytes; (void)destination;
  if (error) *error = "MJPEG camera selected but this build has no libjpeg-turbo";
  return false;
#else
  jpeg_decompress_struct decoder{};
  JpegError failure{};
  decoder.err = jpeg_std_error(&failure.base);
  failure.base.error_exit = jpeg_failure;
  if (setjmp(failure.jump) != 0) { jpeg_destroy_decompress(&decoder); if (error) *error = "libjpeg failed to decode V4L2 MJPEG frame"; return false; }
  jpeg_create_decompress(&decoder);
  jpeg_mem_src(&decoder, const_cast<unsigned char*>(source), static_cast<unsigned long>(bytes));
  jpeg_read_header(&decoder, TRUE);
  decoder.out_color_space = JCS_RGB;
  jpeg_start_decompress(&decoder);
  if (destination->width != static_cast<int>(decoder.output_width) || destination->height != static_cast<int>(decoder.output_height)) destination->resize(static_cast<int>(decoder.output_width), static_cast<int>(decoder.output_height));
  while (decoder.output_scanline < decoder.output_height) {
    JSAMPROW row = destination->rgb.data() + static_cast<std::size_t>(decoder.output_scanline) * decoder.output_width * 3U;
    jpeg_read_scanlines(&decoder, &row, 1);
  }
  jpeg_finish_decompress(&decoder);
  jpeg_destroy_decompress(&decoder);
  return true;
#endif
}

bool V4L2Camera::capture(RgbFrame* frame, std::string* error) {
  if (fd_ < 0) { if (error) *error = "capture requested on closed V4L2 camera"; return false; }
  pollfd descriptor{fd_, POLLIN, 0};
  const int ready = poll(&descriptor, 1, 1000);
  if (ready <= 0) { if (error) *error = ready == 0 ? "V4L2 capture timeout" : errno_text("poll camera"); return false; }
  v4l2_buffer buffer{};
  buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; buffer.memory = V4L2_MEMORY_MMAP;
  if (!ioctl_retry(fd_, VIDIOC_DQBUF, &buffer) || buffer.index >= buffers_.size()) { if (error) *error = errno_text("VIDIOC_DQBUF"); return false; }
  bool converted = pixel_format_ == V4L2_PIX_FMT_YUYV
      ? convert_yuyv(static_cast<const std::uint8_t*>(buffers_[buffer.index].address), buffer.bytesused, frame, error)
      : convert_mjpeg(static_cast<const std::uint8_t*>(buffers_[buffer.index].address), buffer.bytesused, frame, error);
  const bool queued = ioctl_retry(fd_, VIDIOC_QBUF, &buffer);
  if (!queued && converted) { if (error) *error = errno_text("VIDIOC_QBUF after frame"); return false; }
  if (!converted) return false;
  frame->timestamp_ns = monotonic_ns();
  frame->sequence = ++sequence_;
  return true;
}

}  // namespace facephys
