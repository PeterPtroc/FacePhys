#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "facephys/config.h"
#include "facephys/types.h"

namespace facephys {

class Camera {
 public:
  virtual ~Camera() = default;
  virtual bool open(const CameraConfig& config, std::string* error) = 0;
  virtual void close() = 0;
  virtual bool capture(RgbFrame* frame, std::string* error) = 0;
  [[nodiscard]] virtual int width() const = 0;
  [[nodiscard]] virtual int height() const = 0;
};

// Returns the configured camera implementation without opening the device.
// Backends are selected by CameraConfig::backend, never inferred from a path.
std::unique_ptr<Camera> create_camera(const CameraConfig& config, std::string* error);

// A bounded one-frame handoff.  Producers overwrite a stale frame rather than
// allowing capture latency to turn into an unbounded inference queue.
class LatestFrame {
 public:
  void publish(const RgbFrame& frame);
  bool wait_copy_after(std::uint64_t sequence, RgbFrame* destination,
                       std::chrono::milliseconds timeout);
  void wake_all();
 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  RgbFrame latest_;
  bool available_ = false;
};

}  // namespace facephys
