#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "facephys/config.h"
#include "facephys/gpio_line.h"

namespace facephys {

constexpr std::uint16_t rgb565(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
  return static_cast<std::uint16_t>(((red & 0xF8U) << 8U) | ((green & 0xFCU) << 3U) | (blue >> 3U));
}

class St7735 {
 public:
  St7735() = default;
  ~St7735();
  St7735(const St7735&) = delete;
  St7735& operator=(const St7735&) = delete;
  bool open(const DisplayConfig& config, std::string* error);
  void close();
  [[nodiscard]] bool is_open() const { return spi_fd_ >= 0; }
  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  void clear(std::uint16_t color);
  void pixel(int x, int y, std::uint16_t color);
  void fill_rect(int x, int y, int width, int height, std::uint16_t color);
  void line(int x0, int y0, int x1, int y1, std::uint16_t color);
  void text(int x, int y, const std::string& value, std::uint16_t foreground,
            std::uint16_t background, int scale = 1);
  bool flush(std::string* error);
  bool flush_rect(int x, int y, int width, int height, std::string* error);
 private:
  bool reset_and_initialize(std::string* error);
  bool command(std::uint8_t value, std::string* error);
  bool data(const std::uint8_t* bytes, std::size_t count, std::string* error);
  bool set_window(int x, int y, int width, int height, std::string* error);
  const std::uint8_t* glyph(char character) const;
  int spi_fd_ = -1;
  int width_ = 0;
  int height_ = 0;
  DisplayConfig config_;
  GpioLine dc_;
  GpioLine reset_;
  std::vector<std::uint16_t> framebuffer_;
  std::vector<std::uint8_t> transfer_;
};

}  // namespace facephys
