#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "facephys/ring_buffer.h"

namespace facephys {
class TfliteRunner;

struct SignalProcessorOptions {
  std::string sqi_model_path;
  std::string psd_model_path;
  int sqi_threads = 1;
  int psd_threads = 1;
  int sqi_interval_ms = 1000;
  int psd_interval_ms = 1000;
};

struct SignalResult {
  float sqi = 0.0F;
  float bpm = 0.0F;
  float model_hr = 0.0F;
  int peak_index = -1;
  bool has_sqi = false;
  bool has_bpm = false;
  double sqi_ms = 0.0;
  double psd_ms = 0.0;
};

class SignalProcessor {
 public:
  SignalProcessor();
  ~SignalProcessor();
  SignalProcessor(const SignalProcessor&) = delete;
  SignalProcessor& operator=(const SignalProcessor&) = delete;

  bool initialize(const SignalProcessorOptions& options, std::string* error);
  void reset();
  void push_bvp(float bvp, float dt_seconds);
  bool process_if_due(std::chrono::steady_clock::time_point now,
                      SignalResult* result, std::string* error);
  bool process_now(SignalResult* result, std::string* error);
  [[nodiscard]] std::vector<float> waveform_chronological() const;
  void describe_io() const;

 private:
  bool validate_contract(std::string* error) const;
  bool run_sqi(SignalResult* result, std::string* error);
  bool run_psd(SignalResult* result, std::string* error);
  void prepare_window();

  SignalProcessorOptions options_;
  std::unique_ptr<TfliteRunner> sqi_runner_;
  std::unique_ptr<TfliteRunner> psd_runner_;
  RingBuffer<float> bvp_{450};
  std::vector<float> window_;
  float latest_dt_seconds_ = 1.0F / 30.0F;
  std::chrono::steady_clock::time_point last_sqi_{};
  std::chrono::steady_clock::time_point last_psd_{};
  SignalResult latest_{};
};

}  // namespace facephys
