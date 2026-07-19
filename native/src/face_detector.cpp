#include "facephys/face_detector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "facephys/tflite_model.h"
#include "tensorflow/lite/c/common.h"

namespace facephys {
namespace {
constexpr int kInputSide = 128;
constexpr int kAnchorCount = 896;
constexpr int kBoxValues = 16;
float sigmoid(float value) { value = std::clamp(value, -80.0F, 80.0F); return 1.0F / (1.0F + std::exp(-value)); }
float iou(const RectF& a, const RectF& b) {
  const float left = std::max(a.x, b.x), top = std::max(a.y, b.y);
  const float right = std::min(a.x + a.width, b.x + b.width), bottom = std::min(a.y + a.height, b.y + b.height);
  const float intersection = std::max(0.0F, right - left) * std::max(0.0F, bottom - top);
  const float union_area = a.width * a.height + b.width * b.height - intersection;
  return union_area > 0.0F ? intersection / union_area : 0.0F;
}
float bilinear(const RgbFrame& frame, float x, float y, int component) {
  x = std::clamp(x, 0.0F, static_cast<float>(frame.width - 1)); y = std::clamp(y, 0.0F, static_cast<float>(frame.height - 1));
  const int x0 = static_cast<int>(x), y0 = static_cast<int>(y), x1 = std::min(x0 + 1, frame.width - 1), y1 = std::min(y0 + 1, frame.height - 1);
  const float fx = x - x0, fy = y - y0;
  const auto at = [&frame, component](int px, int py) { return static_cast<float>(frame.rgb[(static_cast<std::size_t>(py) * frame.width + px) * 3U + component]); };
  return (at(x0,y0) * (1-fx) + at(x1,y0) * fx) * (1-fy) + (at(x0,y1) * (1-fx) + at(x1,y1) * fx) * fy;
}
}  // namespace

BlazeFaceDetector::BlazeFaceDetector() { input_.resize(kInputSide * kInputSide * 3U); }
BlazeFaceDetector::~BlazeFaceDetector() = default;

bool BlazeFaceDetector::initialize(const FaceDetectorOptions& options, std::string* error) {
  options_ = options;
  runner_ = std::make_unique<TfliteRunner>();
  if (!runner_->load(options.model_path, {options.threads, false}, error)) { runner_.reset(); return false; }
  const auto* interpreter = runner_->interpreter();
  if (interpreter->inputs().size() != 1U || interpreter->outputs().size() != 2U ||
      interpreter->tensor(interpreter->inputs()[0])->type != kTfLiteFloat32 || interpreter->tensor(interpreter->inputs()[0])->bytes != input_.size() * sizeof(float) ||
      interpreter->tensor(interpreter->outputs()[0])->type != kTfLiteFloat32 || interpreter->tensor(interpreter->outputs()[0])->bytes != kAnchorCount * kBoxValues * sizeof(float) ||
      interpreter->tensor(interpreter->outputs()[1])->type != kTfLiteFloat32 || interpreter->tensor(interpreter->outputs()[1])->bytes != kAnchorCount * sizeof(float)) {
    if (error) *error = "BlazeFace model does not have expected [1,128,128,3]/[896,16]/[896,1] tensors";
    runner_.reset(); return false;
  }
  anchors_.clear();
  // MediaPipe short-range SSD anchors: one stride-8 layer with two anchors,
  // then three stride-16 layers grouped into six anchors per cell.
  for (int group = 0; group < 2; ++group) {
    const int stride = group == 0 ? 8 : 16;
    const int anchors_per_cell = group == 0 ? 2 : 6;
    const int feature = static_cast<int>(std::ceil(static_cast<float>(kInputSide) / stride));
    for (int y = 0; y < feature; ++y) for (int x = 0; x < feature; ++x) for (int anchor = 0; anchor < anchors_per_cell; ++anchor)
      anchors_.push_back({(x + 0.5F) / feature, (y + 0.5F) / feature});
  }
  if (anchors_.size() != kAnchorCount) { if (error) *error = "internal BlazeFace anchor generation failed"; runner_.reset(); return false; }
  return true;
}

bool BlazeFaceDetector::detect(const RgbFrame& frame, std::vector<RectF>* faces, std::string* error) {
  if (!runner_ || !frame.valid()) { if (error) *error = "BlazeFace detector is not initialized or frame is invalid"; return false; }
  for (int y = 0; y < kInputSide; ++y) for (int x = 0; x < kInputSide; ++x) {
    const float sx = ((x + 0.5F) * frame.width / kInputSide) - 0.5F;
    const float sy = ((y + 0.5F) * frame.height / kInputSide) - 0.5F;
    const auto destination = (static_cast<std::size_t>(y) * kInputSide + x) * 3U;
    // MediaPipe FaceDetector task image preprocessor uses mean/std 127.5.
    input_[destination] = bilinear(frame, sx, sy, 0) / 127.5F - 1.0F;
    input_[destination + 1U] = bilinear(frame, sx, sy, 1) / 127.5F - 1.0F;
    input_[destination + 2U] = bilinear(frame, sx, sy, 2) / 127.5F - 1.0F;
  }
  auto* interpreter = runner_->interpreter();
  std::memcpy(interpreter->tensor(interpreter->inputs()[0])->data.f, input_.data(), input_.size() * sizeof(float));
  if (interpreter->Invoke() != kTfLiteOk) { if (error) *error = "BlazeFace Invoke failed"; return false; }
  const float* boxes = interpreter->tensor(interpreter->outputs()[0])->data.f;
  const float* scores = interpreter->tensor(interpreter->outputs()[1])->data.f;
  struct Candidate { RectF box; float score; };
  std::vector<Candidate> candidates;
  candidates.reserve(32);
  for (int index = 0; index < kAnchorCount; ++index) {
    const float score = sigmoid(scores[index]);
    if (score < options_.score_threshold) continue;
    const float* raw = boxes + static_cast<std::size_t>(index) * kBoxValues;
    const float cx = raw[0] / kInputSide + anchors_[index].x_center;
    const float cy = raw[1] / kInputSide + anchors_[index].y_center;
    const float width = raw[2] / kInputSide;
    const float height = raw[3] / kInputSide;
    RectF rectangle{(cx - width * 0.5F) * frame.width, (cy - height * 0.5F) * frame.height, width * frame.width, height * frame.height};
    if (rectangle.valid()) candidates.push_back({rectangle, score});
  }
  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
  faces->clear();
  for (const auto& candidate : candidates) {
    bool suppressed = false;
    for (const auto& selected : *faces) if (iou(candidate.box, selected) > options_.nms_iou_threshold) { suppressed = true; break; }
    if (!suppressed) faces->push_back(candidate.box);
  }
  return true;
}

float FaceTracker::Kalman1D::update(float measurement) {
  if (!valid) { x = measurement; p = 1.0F; valid = true; return x; }
  constexpr float q = 1e-2F, r = 5e-1F;
  const float predicted_p = p + q;
  const float gain = predicted_p / (predicted_p + r);
  x += gain * (measurement - x); p = (1.0F - gain) * predicted_p; return x;
}

bool FaceTracker::initialize(const FaceDetectorOptions& detector_options, int interval_ms, std::optional<RectF> fixed_roi, std::string* error) {
  fixed_roi_ = fixed_roi; interval_ = std::chrono::milliseconds(std::max(50, interval_ms)); reset();
  return fixed_roi_.has_value() || detector_.initialize(detector_options, error);
}
void FaceTracker::reset() { last_attempt_ = {}; last_roi_ = {}; have_roi_ = false; x_ = {}; y_ = {}; width_ = {}; height_ = {}; }
bool FaceTracker::roi_for_frame(const RgbFrame& frame, std::chrono::steady_clock::time_point now, RectF* roi, std::string* error) {
  if (fixed_roi_) { *roi = *fixed_roi_; return true; }
  const auto due = last_attempt_ == std::chrono::steady_clock::time_point{} || now - last_attempt_ >= (have_roi_ ? interval_ : std::chrono::milliseconds(200));
  if (!due) { if (have_roi_) *roi = last_roi_; return have_roi_; }
  last_attempt_ = now;
  std::vector<RectF> faces;
  if (!detector_.detect(frame, &faces, error)) return false;
  if (faces.empty()) { have_roi_ = false; return false; }
  RectF raw = faces.front();
  raw.x = x_.update(raw.x); raw.y = y_.update(raw.y); raw.width = width_.update(raw.width); raw.height = height_.update(raw.height);
  raw.height *= 1.2F; raw.y -= raw.height * 0.2F;  // exact browser crop adjustment
  last_roi_ = raw; have_roi_ = true; *roi = raw; return true;
}

}  // namespace facephys
