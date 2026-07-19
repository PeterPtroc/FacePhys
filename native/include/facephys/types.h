#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace facephys {

struct RectF {
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;

  [[nodiscard]] bool valid() const { return width > 1.0F && height > 1.0F; }
};

struct RgbFrame {
  int width = 0;
  int height = 0;
  std::uint64_t timestamp_ns = 0;
  std::uint64_t sequence = 0;
  std::vector<std::uint8_t> rgb;

  void resize(int new_width, int new_height) {
    width = new_width;
    height = new_height;
    rgb.resize(static_cast<std::size_t>(width) * height * 3U);
  }

  [[nodiscard]] bool valid() const {
    return width > 0 && height > 0 &&
           rgb.size() == static_cast<std::size_t>(width) * height * 3U;
  }
};

}  // namespace facephys
