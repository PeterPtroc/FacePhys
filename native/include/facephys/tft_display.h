#pragma once

#include <string>

#include "facephys/application_state.h"
#include "facephys/config.h"
#include "facephys/st7735.h"

namespace facephys {
class TftDisplay {
 public:
  bool open(const DisplayConfig& config, std::string* error);
  void close();
  [[nodiscard]] bool is_open() const { return panel_.is_open(); }
  bool render(const StateSnapshot& snapshot, std::string* error);
 private:
  St7735 panel_;
};
}  // namespace facephys
