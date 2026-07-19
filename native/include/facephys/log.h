#pragma once

#include <string>

namespace facephys {

enum class LogLevel { kDebug, kInfo, kWarn, kError };

void set_log_level(LogLevel level);
void log_message(LogLevel level, const std::string& message);

}  // namespace facephys

#define FP_DEBUG(message) ::facephys::log_message(::facephys::LogLevel::kDebug, (message))
#define FP_INFO(message) ::facephys::log_message(::facephys::LogLevel::kInfo, (message))
#define FP_WARN(message) ::facephys::log_message(::facephys::LogLevel::kWarn, (message))
#define FP_ERROR(message) ::facephys::log_message(::facephys::LogLevel::kError, (message))
