#include "facephys/signal_processor.h"

#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstring>

#include "facephys/tflite_model.h"
#include "tensorflow/lite/c/common.h"

namespace facephys {
namespace {
bool is_float_tensor(const TfLiteTensor* tensor, std::size_t bytes) {
  return tensor != nullptr && tensor->type == kTfLiteFloat32 && tensor->bytes == bytes;
}
}  // namespace

SignalProcessor::SignalProcessor() { window_.reserve(450); }
SignalProcessor::~SignalProcessor() = default;

bool SignalProcessor::initialize(const SignalProcessorOptions& options, std::string* error) {
  options_ = options;
  sqi_runner_ = std::make_unique<TfliteRunner>();
  psd_runner_ = std::make_unique<TfliteRunner>();
  if (!sqi_runner_->load(options.sqi_model_path, {options.sqi_threads, false}, error) ||
      !psd_runner_->load(options.psd_model_path, {options.psd_threads, false}, error) ||
      !validate_contract(error)) {
    sqi_runner_.reset();
    psd_runner_.reset();
    return false;
  }
  reset();
  return true;
}

void SignalProcessor::reset() {
  bvp_.clear();
  window_.assign(450, 0.0F);
  latest_dt_seconds_ = 1.0F / 30.0F;
  last_sqi_ = {};
  last_psd_ = {};
  latest_ = {};
}

bool SignalProcessor::validate_contract(std::string* error) const {
  if (!sqi_runner_ || !psd_runner_) { if (error) *error = "signal models are not initialized"; return false; }
  const auto* sqi = sqi_runner_->interpreter();
  const auto* psd = psd_runner_->interpreter();
  if (sqi->inputs().size() != 1U || sqi->outputs().size() != 1U || psd->inputs().size() != 1U || psd->outputs().size() != 4U ||
      !is_float_tensor(sqi->tensor(sqi->inputs()[0]), 450U * sizeof(float)) ||
      !is_float_tensor(sqi->tensor(sqi->outputs()[0]), sizeof(float)) ||
      !is_float_tensor(psd->tensor(psd->inputs()[0]), 450U * sizeof(float))) {
    if (error) *error = "SQI/PSD model input contract does not match FacePhys";
    return false;
  }
  // Verified against psd_model.tflite SignatureDef: raw interpreter output
  // positions are freq=0, peak=1, hr=2, psd=3.
  if (!is_float_tensor(psd->tensor(psd->outputs()[0]), 2730U * sizeof(float)) ||
      psd->tensor(psd->outputs()[1])->type != kTfLiteInt32 || psd->tensor(psd->outputs()[1])->bytes != sizeof(std::int32_t) ||
      !is_float_tensor(psd->tensor(psd->outputs()[2]), sizeof(float)) ||
      !is_float_tensor(psd->tensor(psd->outputs()[3]), 2730U * sizeof(float))) {
    if (error) *error = "PSD output contract/signature mapping does not match FacePhys";
    return false;
  }
  return true;
}

void SignalProcessor::push_bvp(float bvp, float dt_seconds) {
  if (!std::isfinite(bvp) || !std::isfinite(dt_seconds) || dt_seconds <= 0.0F) return;
  bvp_.push(bvp);
  latest_dt_seconds_ = dt_seconds;
}

void SignalProcessor::prepare_window() { bvp_.copy_chronological_padded(window_); }

bool SignalProcessor::run_sqi(SignalResult* result, std::string* error) {
  prepare_window();
  auto* interpreter = sqi_runner_->interpreter();
  std::memcpy(interpreter->tensor(interpreter->inputs()[0])->data.f, window_.data(), 450U * sizeof(float));
  const auto start = std::chrono::steady_clock::now();
  if (interpreter->Invoke() != kTfLiteOk) { if (error) *error = "SQI model Invoke failed"; return false; }
  const auto end = std::chrono::steady_clock::now();
  const TfLiteTensor* output = interpreter->tensor(interpreter->outputs()[0]);
  if (!is_float_tensor(output, sizeof(float)) || output->data.f == nullptr) {
    if (error) *error = "SQI output buffer is unavailable after Invoke";
    return false;
  }
  const float sqi = output->data.f[0];
  if (!std::isfinite(sqi)) { if (error) *error = "SQI output is NaN/Inf"; return false; }
  latest_.sqi = sqi;
  latest_.has_sqi = true;
  latest_.sqi_ms = std::chrono::duration<double, std::milli>(end - start).count();
  if (result) *result = latest_;
  return true;
}

bool SignalProcessor::run_psd(SignalResult* result, std::string* error) {
  prepare_window();
  auto* interpreter = psd_runner_->interpreter();
  std::memcpy(interpreter->tensor(interpreter->inputs()[0])->data.f, window_.data(), 450U * sizeof(float));
  const auto start = std::chrono::steady_clock::now();
  if (interpreter->Invoke() != kTfLiteOk) { if (error) *error = "PSD model Invoke failed"; return false; }
  const auto end = std::chrono::steady_clock::now();
  const TfLiteTensor* hr_output = interpreter->tensor(interpreter->outputs()[2]);
  const TfLiteTensor* peak_output = interpreter->tensor(interpreter->outputs()[1]);
  if (!is_float_tensor(hr_output, sizeof(float)) || hr_output->data.f == nullptr ||
      peak_output == nullptr || peak_output->type != kTfLiteInt32 ||
      peak_output->bytes != sizeof(std::int32_t) || peak_output->data.i32 == nullptr) {
    if (error) *error = "PSD output buffer is unavailable after Invoke";
    return false;
  }
  const float hr = hr_output->data.f[0];
  const std::int32_t peak = peak_output->data.i32[0];
  if (!std::isfinite(hr) || latest_dt_seconds_ <= 0.0F || !std::isfinite(latest_dt_seconds_)) {
    if (error) *error = "PSD HR or dt is NaN/Inf";
    return false;
  }
  latest_.model_hr = hr;
  latest_.bpm = hr / 30.0F / latest_dt_seconds_;  // exact main.js conversion
  latest_.peak_index = peak;
  latest_.has_bpm = std::isfinite(latest_.bpm);
  latest_.psd_ms = std::chrono::duration<double, std::milli>(end - start).count();
  if (result) *result = latest_;
  return latest_.has_bpm;
}

bool SignalProcessor::process_if_due(std::chrono::steady_clock::time_point now,
                                     SignalResult* result, std::string* error) {
  const bool sqi_due = last_sqi_ == std::chrono::steady_clock::time_point{} ||
      now - last_sqi_ >= std::chrono::milliseconds(options_.sqi_interval_ms);
  const bool psd_due = last_psd_ == std::chrono::steady_clock::time_point{} ||
      now - last_psd_ >= std::chrono::milliseconds(options_.psd_interval_ms);
  if (!sqi_due && !psd_due) { if (result) *result = latest_; return true; }
  if (sqi_due && !run_sqi(result, error)) return false;
  if (psd_due && !run_psd(result, error)) return false;
  if (sqi_due) last_sqi_ = now;
  if (psd_due) last_psd_ = now;
  if (result) *result = latest_;
  return true;
}

bool SignalProcessor::process_now(SignalResult* result, std::string* error) {
  if (!run_sqi(result, error) || !run_psd(result, error)) return false;
  const auto now = std::chrono::steady_clock::now();
  last_sqi_ = now;
  last_psd_ = now;
  if (result) *result = latest_;
  return true;
}

std::vector<float> SignalProcessor::waveform_chronological() const {
  std::vector<float> result;
  bvp_.copy_chronological_padded(result);
  return result;
}

void SignalProcessor::describe_io() const {
  if (sqi_runner_) sqi_runner_->describe_io("FacePhys SQI model");
  if (psd_runner_) psd_runner_->describe_io("FacePhys PSD model");
}

}  // namespace facephys
