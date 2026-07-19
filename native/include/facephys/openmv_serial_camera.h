#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "facephys/camera.h"

namespace facephys {

// Receives JPEG frames emitted by openmv/openmv_facephys_stream.py over the
// OpenMV USB CDC ACM port. This is deliberately not a V4L2/UVC backend: the
// currently attached OpenMV enumerates only as /dev/ttyACM0.
class OpenMvSerialCamera final : public Camera {
 public:
  OpenMvSerialCamera() = default;
  ~OpenMvSerialCamera() override;
  bool open(const CameraConfig& config, std::string* error) override;
  void close() override;
  bool capture(RgbFrame* frame, std::string* error) override;
  [[nodiscard]] int width() const override { return width_; }
  [[nodiscard]] int height() const override { return height_; }

 private:
  struct Packet {
    int width = 0;
    int height = 0;
    std::uint32_t sequence = 0;
    std::vector<std::uint8_t> jpeg;
  };

  bool read_available(int timeout_ms, std::string* error);
  bool extract_newest_packet(Packet* packet, std::string* error);
  bool decode_jpeg(const Packet& packet, RgbFrame* frame, std::string* error) const;

  int fd_ = -1;
  int width_ = 0;
  int height_ = 0;
  int timeout_ms_ = 1000;
  std::size_t max_frame_bytes_ = 262144U;
  std::uint64_t fallback_sequence_ = 0;
  std::vector<std::uint8_t> receive_buffer_;
};

}  // namespace facephys
