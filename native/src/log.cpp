#include "facephys/log.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>

namespace facephys {
namespace {
std::mutex g_log_mutex;
LogLevel g_log_level = LogLevel::kInfo;

const char* level_name(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug: return "DEBUG";
    case LogLevel::kInfo: return "INFO";
    case LogLevel::kWarn: return "WARN";
    case LogLevel::kError: return "ERROR";
  }
  return "UNKNOWN";
}
}  // namespace

void set_log_level(LogLevel level) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_log_level = level;
}

void log_message(LogLevel level, const std::string& message) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  if (static_cast<int>(level) < static_cast<int>(g_log_level)) return;
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
  localtime_r(&time, &local);
  std::cerr << std::put_time(&local, "%F %T") << ' ' << level_name(level) << ' ' << message << '\n';
}

}  // namespace facephys
