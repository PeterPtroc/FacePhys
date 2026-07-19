#include "facephys/gpio_line.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

#ifdef FACEPHYS_HAVE_GPIOD
#include <gpiod.h>
#endif

namespace facephys {
namespace {
bool write_path(const std::string& path, const std::string& value, std::string* error) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
  if (fd < 0) { if (error) *error = "open " + path + ": " + std::strerror(errno); return false; }
  const ssize_t count = ::write(fd, value.data(), value.size());
  const int saved_errno = errno;
  ::close(fd);
  if (count != static_cast<ssize_t>(value.size())) { if (error) *error = "write " + path + ": " + std::strerror(saved_errno); return false; }
  return true;
}
}  // namespace

GpioLine::~GpioLine() { close(); }

bool GpioLine::open_output(const GpioLineConfig& config, bool initial_value, std::string* error) {
  close();
#ifdef FACEPHYS_HAVE_GPIOD
  if (config.line >= 0) {
    auto* chip = gpiod_chip_open(config.chip.c_str());
    if (chip != nullptr) {
      auto* line = gpiod_chip_get_line(chip, static_cast<unsigned int>(config.line));
      if (line != nullptr && gpiod_line_request_output(line, config.consumer.c_str(), initial_value ? 1 : 0) == 0) {
        chip_ = chip; line_ = line; backend_ = Backend::kGpiod; return true;
      }
      gpiod_chip_close(chip);
    }
  }
#endif
  if (config.sysfs_number < 0) {
    if (error) *error = "libgpiod unavailable/failed and no sysfs GPIO number was configured";
    return false;
  }
  sysfs_number_ = config.sysfs_number;
  const std::string directory = "/sys/class/gpio/gpio" + std::to_string(sysfs_number_);
  struct stat status {};
  if (stat(directory.c_str(), &status) != 0) {
    if (!write_path("/sys/class/gpio/export", std::to_string(sysfs_number_), error)) return false;
    exported_by_us_ = true;
    for (int tries = 0; tries < 50 && stat(directory.c_str(), &status) != 0; ++tries) usleep(10000);
    if (stat(directory.c_str(), &status) != 0) { if (error) *error = "sysfs GPIO did not appear: " + directory; close(); return false; }
  }
  if (!write_path(directory + "/direction", "out", error)) { close(); return false; }
  value_fd_ = ::open((directory + "/value").c_str(), O_WRONLY | O_CLOEXEC);
  if (value_fd_ < 0) { if (error) *error = "open GPIO value: " + std::string(std::strerror(errno)); close(); return false; }
  backend_ = Backend::kSysfs;
  return set(initial_value, error);
}

bool GpioLine::set(bool value, std::string* error) {
  if (backend_ == Backend::kGpiod) {
#ifdef FACEPHYS_HAVE_GPIOD
    if (gpiod_line_set_value(static_cast<gpiod_line*>(line_), value ? 1 : 0) == 0) return true;
    if (error) *error = "gpiod_line_set_value: " + std::string(std::strerror(errno));
    return false;
#endif
  }
  if (backend_ == Backend::kSysfs) {
    const char digit = value ? '1' : '0';
    if (lseek(value_fd_, 0, SEEK_SET) < 0 || ::write(value_fd_, &digit, 1) != 1) {
      if (error) *error = "write GPIO value: " + std::string(std::strerror(errno));
      return false;
    }
    return true;
  }
  if (error) *error = "GPIO line is not open";
  return false;
}

void GpioLine::close() {
#ifdef FACEPHYS_HAVE_GPIOD
  if (backend_ == Backend::kGpiod) {
    if (line_ != nullptr) gpiod_line_release(static_cast<gpiod_line*>(line_));
    if (chip_ != nullptr) gpiod_chip_close(static_cast<gpiod_chip*>(chip_));
  }
#endif
  if (value_fd_ >= 0) ::close(value_fd_);
  if (exported_by_us_ && sysfs_number_ >= 0) {
    std::string ignored;
    (void)write_path("/sys/class/gpio/unexport", std::to_string(sysfs_number_), &ignored);
  }
  chip_ = nullptr; line_ = nullptr; value_fd_ = -1; sysfs_number_ = -1; exported_by_us_ = false; backend_ = Backend::kNone;
}

const char* GpioLine::backend_name() const {
  switch (backend_) {
    case Backend::kGpiod: return "libgpiod";
    case Backend::kSysfs: return "sysfs";
    case Backend::kNone: return "closed";
  }
  return "unknown";
}

}  // namespace facephys
