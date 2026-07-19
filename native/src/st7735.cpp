#include "facephys/st7735.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

namespace facephys {
namespace {
constexpr std::uint8_t kNop = 0x00;
constexpr std::uint8_t kSwReset = 0x01;
constexpr std::uint8_t kSleepOut = 0x11;
constexpr std::uint8_t kNormalOn = 0x13;
constexpr std::uint8_t kDisplayOn = 0x29;
constexpr std::uint8_t kColumnAddressSet = 0x2A;
constexpr std::uint8_t kRowAddressSet = 0x2B;
constexpr std::uint8_t kMemoryWrite = 0x2C;
constexpr std::uint8_t kMadCtl = 0x36;
constexpr std::uint8_t kColorMode = 0x3A;
constexpr std::uint8_t kInversionOn = 0x21;
constexpr std::uint8_t kInversionOff = 0x20;
constexpr std::uint16_t kBlack = rgb565(0, 0, 0);

const std::array<std::uint8_t, 5> kBlank{0, 0, 0, 0, 0};
const std::array<std::uint8_t, 5> kDigits[][10] = {{
  {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10},
  {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03}, {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}
}};
// Five-column uppercase ASCII used by the compact status UI.
const std::array<std::array<std::uint8_t, 5>, 26> kLetters{{
  {{0x7E,0x11,0x11,0x11,0x7E}}, {{0x7F,0x49,0x49,0x49,0x36}}, {{0x3E,0x41,0x41,0x41,0x22}}, {{0x7F,0x41,0x41,0x22,0x1C}}, {{0x7F,0x49,0x49,0x49,0x41}}, {{0x7F,0x09,0x09,0x09,0x01}}, {{0x3E,0x41,0x49,0x49,0x7A}},
  {{0x7F,0x08,0x08,0x08,0x7F}}, {{0x00,0x41,0x7F,0x41,0x00}}, {{0x20,0x40,0x41,0x3F,0x01}}, {{0x7F,0x08,0x14,0x22,0x41}}, {{0x7F,0x40,0x40,0x40,0x40}}, {{0x7F,0x02,0x0C,0x02,0x7F}}, {{0x7F,0x04,0x08,0x10,0x7F}},
  {{0x3E,0x41,0x41,0x41,0x3E}}, {{0x7F,0x09,0x09,0x09,0x06}}, {{0x3E,0x41,0x51,0x21,0x5E}}, {{0x7F,0x09,0x19,0x29,0x46}}, {{0x46,0x49,0x49,0x49,0x31}}, {{0x01,0x01,0x7F,0x01,0x01}}, {{0x3F,0x40,0x40,0x40,0x3F}}, {{0x1F,0x20,0x40,0x20,0x1F}}, {{0x3F,0x40,0x38,0x40,0x3F}}, {{0x63,0x14,0x08,0x14,0x63}}, {{0x07,0x08,0x70,0x08,0x07}}, {{0x61,0x51,0x49,0x45,0x43}}
}};
const std::array<std::uint8_t, 5> kColon{0,0x36,0x36,0,0};
const std::array<std::uint8_t, 5> kDot{0,0x60,0x60,0,0};
const std::array<std::uint8_t, 5> kDash{0x08,0x08,0x08,0x08,0x08};
const std::array<std::uint8_t, 5> kSlash{0x20,0x10,0x08,0x04,0x02};
const std::array<std::uint8_t, 5> kPlus{0x08,0x08,0x3E,0x08,0x08};
}  // namespace

St7735::~St7735() { close(); }

bool St7735::open(const DisplayConfig& config, std::string* error) {
  close();
  config_ = config;
  if (config.rotation < 0 || config.rotation > 3 || config.width <= 0 || config.height <= 0) {
    if (error) *error = "invalid ST7735 physical dimensions or rotation";
    return false;
  }
  width_ = (config.rotation & 1) ? config.height : config.width;
  height_ = (config.rotation & 1) ? config.width : config.height;
  spi_fd_ = ::open(config.spi_device.c_str(), O_RDWR | O_CLOEXEC);
  if (spi_fd_ < 0) { if (error) *error = "open " + config.spi_device + ": " + std::strerror(errno); close(); return false; }
  std::uint8_t mode = SPI_MODE_0;
  std::uint8_t bits = 8;
  std::uint32_t speed = static_cast<std::uint32_t>(config.spi_speed_hz);
  if (ioctl(spi_fd_, SPI_IOC_WR_MODE, &mode) < 0 || ioctl(spi_fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
      ioctl(spi_fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
    if (error) *error = "configure spidev: " + std::string(std::strerror(errno));
    close();
    return false;
  }
  if (!dc_.open_output({config.dc_gpiochip, config.dc_gpio_line, config.dc_gpio_number, "facephys-dc"}, false, error) ||
      !reset_.open_output({config.reset_gpiochip, config.reset_gpio_line, config.reset_gpio_number, "facephys-reset"}, true, error)) { close(); return false; }
  framebuffer_.assign(static_cast<std::size_t>(width_) * height_, kBlack);
  transfer_.resize(static_cast<std::size_t>(width_) * 2U);
  if (!reset_and_initialize(error)) { close(); return false; }
  return true;
}

void St7735::close() {
  reset_.close(); dc_.close();
  if (spi_fd_ >= 0) ::close(spi_fd_);
  spi_fd_ = -1; width_ = 0; height_ = 0; framebuffer_.clear(); transfer_.clear();
}

bool St7735::command(std::uint8_t value, std::string* error) {
  if (!dc_.set(false, error)) return false;
  if (::write(spi_fd_, &value, 1) != 1) { if (error) *error = "SPI command write: " + std::string(std::strerror(errno)); return false; }
  return true;
}
bool St7735::data(const std::uint8_t* bytes, std::size_t count, std::string* error) {
  if (!dc_.set(true, error)) return false;
  const auto wrote = ::write(spi_fd_, bytes, count);
  if (wrote != static_cast<ssize_t>(count)) { if (error) *error = "SPI data write: " + std::string(std::strerror(errno)); return false; }
  return true;
}

bool St7735::reset_and_initialize(std::string* error) {
  if (!reset_.set(false, error)) return false;
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  if (!reset_.set(true, error)) return false;
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  if (!command(kSwReset, error)) return false;
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  if (!command(kSleepOut, error)) return false;
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  // ST7735/ ST7735S common baseline.  tab, offsets, inversion and BGR remain
  // configuration values because small 1.8-inch modules differ here.
  if (!command(kColorMode, error)) return false;
  const std::uint8_t rgb565_mode = 0x05;
  if (!data(&rgb565_mode, 1, error)) return false;
  std::uint8_t madctl = 0;
  switch (config_.rotation) {
    case 0: madctl = 0x00; break;
    case 1: madctl = 0x60; break;
    case 2: madctl = 0xC0; break;
    case 3: madctl = 0xA0; break;
  }
  if (config_.bgr) madctl |= 0x08;
  if (!command(kMadCtl, error) || !data(&madctl, 1, error) ||
      !command(config_.invert ? kInversionOn : kInversionOff, error) || !command(kNormalOn, error) ||
      !command(kDisplayOn, error)) return false;
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return flush(error);
}

void St7735::clear(std::uint16_t color) { std::fill(framebuffer_.begin(), framebuffer_.end(), color); }
void St7735::pixel(int x, int y, std::uint16_t color) { if (x >= 0 && x < width_ && y >= 0 && y < height_) framebuffer_[static_cast<std::size_t>(y) * width_ + x] = color; }
void St7735::fill_rect(int x, int y, int width, int height, std::uint16_t color) {
  const int left = std::max(0, x), top = std::max(0, y), right = std::min(width_, x + width), bottom = std::min(height_, y + height);
  for (int row = top; row < bottom; ++row) std::fill(framebuffer_.begin() + static_cast<std::size_t>(row) * width_ + left, framebuffer_.begin() + static_cast<std::size_t>(row) * width_ + right, color);
}
void St7735::line(int x0, int y0, int x1, int y1, std::uint16_t color) {
  const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;
  while (true) { pixel(x0, y0, color); if (x0 == x1 && y0 == y1) break; const int twice = 2 * error; if (twice >= dy) { error += dy; x0 += sx; } if (twice <= dx) { error += dx; y0 += sy; } }
}
const std::uint8_t* St7735::glyph(char character) const {
  if (character >= 'a' && character <= 'z') character = static_cast<char>(character - 'a' + 'A');
  if (character >= '0' && character <= '9') return kDigits[0][character - '0'].data();
  if (character >= 'A' && character <= 'Z') return kLetters[character - 'A'].data();
  switch (character) { case ':': return kColon.data(); case '.': return kDot.data(); case '-': return kDash.data(); case '/': return kSlash.data(); case '+': return kPlus.data(); default: return kBlank.data(); }
}
void St7735::text(int x, int y, const std::string& value, std::uint16_t foreground, std::uint16_t background, int scale) {
  scale = std::max(scale, 1);
  for (char character : value) {
    const auto* bitmap = glyph(character);
    for (int column = 0; column < 5; ++column) for (int row = 0; row < 7; ++row) fill_rect(x + column * scale, y + row * scale, scale, scale, (bitmap[column] & (1U << row)) ? foreground : background);
    x += 6 * scale;
  }
}
bool St7735::set_window(int x, int y, int width, int height, std::string* error) {
  const int x0 = x + config_.x_offset, x1 = x + width - 1 + config_.x_offset;
  const int y0 = y + config_.y_offset, y1 = y + height - 1 + config_.y_offset;
  const std::array<std::uint8_t, 4> columns{{static_cast<std::uint8_t>(x0 >> 8), static_cast<std::uint8_t>(x0), static_cast<std::uint8_t>(x1 >> 8), static_cast<std::uint8_t>(x1)}};
  const std::array<std::uint8_t, 4> rows{{static_cast<std::uint8_t>(y0 >> 8), static_cast<std::uint8_t>(y0), static_cast<std::uint8_t>(y1 >> 8), static_cast<std::uint8_t>(y1)}};
  return command(kColumnAddressSet, error) && data(columns.data(), columns.size(), error) && command(kRowAddressSet, error) && data(rows.data(), rows.size(), error) && command(kMemoryWrite, error);
}
bool St7735::flush(std::string* error) { return flush_rect(0, 0, width_, height_, error); }
bool St7735::flush_rect(int x, int y, int width, int height, std::string* error) {
  if (!is_open()) { if (error) *error = "ST7735 is not open"; return false; }
  const int left = std::max(0, x), top = std::max(0, y), right = std::min(width_, x + width), bottom = std::min(height_, y + height);
  if (left >= right || top >= bottom) return true;
  if (!set_window(left, top, right - left, bottom - top, error)) return false;
  for (int row = top; row < bottom; ++row) {
    for (int column = left; column < right; ++column) {
      const auto color = framebuffer_[static_cast<std::size_t>(row) * width_ + column];
      const auto offset = static_cast<std::size_t>(column - left) * 2U;
      transfer_[offset] = static_cast<std::uint8_t>(color >> 8U);
      transfer_[offset + 1U] = static_cast<std::uint8_t>(color & 0xFFU);
    }
    if (!data(transfer_.data(), static_cast<std::size_t>(right - left) * 2U, error)) return false;
  }
  return true;
}

}  // namespace facephys
