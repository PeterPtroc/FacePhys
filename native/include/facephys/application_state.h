#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace facephys {

enum class RunStatus { kStarting, kRun, kNoFace, kCameraError, kModelError, kStopping };

struct StateSnapshot {
  RunStatus status = RunStatus::kStarting;
  float bpm = 0.0F;
  float sqi = 0.0F;
  float inference_ms = 0.0F;
  float fps = 0.0F;
  bool has_face = false;
  std::vector<float> waveform;
  std::string detail;
};

class ApplicationState {
 public:
  ApplicationState() { state_.waveform.reserve(160); }
  void update(const StateSnapshot& state) { std::lock_guard<std::mutex> lock(mutex_); state_ = state; }
  [[nodiscard]] StateSnapshot snapshot() const { std::lock_guard<std::mutex> lock(mutex_); return state_; }
 private:
  mutable std::mutex mutex_;
  StateSnapshot state_;
};

const char* run_status_name(RunStatus status);

}  // namespace facephys
