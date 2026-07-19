#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "facephys/camera.h"

namespace facephys {

class V4L2Camera final : public Camera {
 public:
  V4L2Camera() = default;
  ~V4L2Camera() override;
  bool open(const CameraConfig& config, std::string* error) override;
  void close() override;
  bool capture(RgbFrame* frame, std::string* error) override;
  [[nodiscard]] int width() const override { return width_; }
  [[nodiscard]] int height() const override { return height_; }
 private:
  struct MappedBuffer { void* address = nullptr; std::size_t length = 0U; };
  bool convert_yuyv(const std::uint8_t* source, std::size_t bytes, RgbFrame* destination,
                    std::string* error) const;
  bool convert_mjpeg(const std::uint8_t* source, std::size_t bytes, RgbFrame* destination,
                     std::string* error) const;
  int fd_ = -1;
  int width_ = 0;
  int height_ = 0;
  std::uint32_t pixel_format_ = 0;
  std::vector<MappedBuffer> buffers_;
  std::uint64_t sequence_ = 0;
};

}  // namespace facephys
