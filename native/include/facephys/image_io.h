#pragma once

#include <string>

#include "facephys/types.h"

namespace facephys {
bool read_ppm(const std::string& path, RgbFrame* frame, std::string* error);
bool write_ppm(const std::string& path, const RgbFrame& frame, std::string* error);
}  // namespace facephys
