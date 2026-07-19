#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace facephys {

class TfliteRunner;

struct FacePhysEngineOptions {
  std::string model_path;
  std::string state_path;
  int threads = 4;
  bool enable_xnnpack = true;
};

struct FacePhysInference {
  float bvp = 0.0F;
  double inference_ms = 0.0;
  std::uint64_t step = 0;
};

class FacePhysEngine {
 public:
  FacePhysEngine();
  ~FacePhysEngine();
  FacePhysEngine(const FacePhysEngine&) = delete;
  FacePhysEngine& operator=(const FacePhysEngine&) = delete;

  bool initialize(const FacePhysEngineOptions& options, std::string* error);
  bool reset(std::string* error);
  bool invoke(const std::array<float, 36U * 36U * 3U>& rgb,
              float dt_seconds, FacePhysInference* result, std::string* error);
  [[nodiscard]] bool initialized() const;
  void describe_io() const;

 private:
  bool load_initial_state(std::string* error);
  bool validate_contract(std::string* error) const;

  FacePhysEngineOptions options_;
  std::unique_ptr<TfliteRunner> runner_;
  int image_input_tensor_ = -1;
  int dt_input_tensor_ = -1;
  std::uint64_t step_ = 0;
};

}  // namespace facephys
