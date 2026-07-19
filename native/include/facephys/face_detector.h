#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "facephys/types.h"

namespace facephys {
class TfliteRunner;

struct FaceDetectorOptions {
  std::string model_path;
  int threads = 1;
  float score_threshold = 0.55F;
  float nms_iou_threshold = 0.3F;
};

class BlazeFaceDetector {
 public:
  BlazeFaceDetector();
  ~BlazeFaceDetector();
  bool initialize(const FaceDetectorOptions& options, std::string* error);
  bool detect(const RgbFrame& frame, std::vector<RectF>* faces, std::string* error);
 private:
  struct Anchor { float x_center; float y_center; };
  std::vector<Anchor> anchors_;
  std::vector<float> input_;
  std::unique_ptr<TfliteRunner> runner_;
  FaceDetectorOptions options_;
};

// Implements the Web demo policy: fixed ROI when requested; otherwise low-rate
// detection, q=1e-2/r=5e-1 box smoothing and 1.2x upward face expansion.
class FaceTracker {
 public:
  bool initialize(const FaceDetectorOptions& detector_options, int interval_ms,
                  std::optional<RectF> fixed_roi, std::string* error);
  bool roi_for_frame(const RgbFrame& frame, std::chrono::steady_clock::time_point now,
                     RectF* roi, std::string* error);
  void reset();
 private:
  struct Kalman1D { float x = 0; float p = 1; bool valid = false; float update(float measurement); };
  BlazeFaceDetector detector_;
  std::optional<RectF> fixed_roi_;
  std::chrono::milliseconds interval_{1000};
  std::chrono::steady_clock::time_point last_attempt_{};
  RectF last_roi_{};
  bool have_roi_ = false;
  Kalman1D x_, y_, width_, height_;
};

}  // namespace facephys
