#pragma once

#include <string>

namespace facephys {

struct GpioLineConfig {
  std::string chip = "/dev/gpiochip0";
  int line = -1;
  int sysfs_number = -1;
  std::string consumer;
};

class GpioLine {
 public:
  GpioLine() = default;
  ~GpioLine();
  GpioLine(const GpioLine&) = delete;
  GpioLine& operator=(const GpioLine&) = delete;
  bool open_output(const GpioLineConfig& config, bool initial_value, std::string* error);
  bool set(bool value, std::string* error);
  void close();
  [[nodiscard]] bool is_open() const { return backend_ != Backend::kNone; }
  [[nodiscard]] const char* backend_name() const;
 private:
  enum class Backend { kNone, kGpiod, kSysfs };
  Backend backend_ = Backend::kNone;
  void* chip_ = nullptr;
  void* line_ = nullptr;
  int value_fd_ = -1;
  int sysfs_number_ = -1;
  bool exported_by_us_ = false;
};

}  // namespace facephys
