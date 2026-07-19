#pragma once

#include <memory>
#include <string>

#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/model.h"

namespace facephys {

struct TfliteRunnerOptions {
  int threads = 1;
  bool enable_xnnpack = false;
};

// Owns a single, allocated TFLite interpreter.  It is deliberately reusable:
// callers never construct an interpreter in a frame-processing path.
class TfliteRunner {
 public:
  TfliteRunner() = default;
  ~TfliteRunner();
  TfliteRunner(const TfliteRunner&) = delete;
  TfliteRunner& operator=(const TfliteRunner&) = delete;

  bool load(const std::string& model_path, const TfliteRunnerOptions& options,
            std::string* error);
  [[nodiscard]] tflite::Interpreter* interpreter() const { return interpreter_.get(); }
  [[nodiscard]] const std::string& path() const { return path_; }
  [[nodiscard]] bool xnnpack_enabled() const { return xnnpack_enabled_; }
  [[nodiscard]] int delegated_kernel_count() const { return delegated_kernel_count_; }

  void describe_io(const std::string& model_label) const;

 private:
  std::string path_;
  std::unique_ptr<tflite::FlatBufferModel> model_;
  std::unique_ptr<tflite::Interpreter> interpreter_;
  TfLiteDelegate* xnnpack_delegate_ = nullptr;
  bool xnnpack_enabled_ = false;
  int delegated_kernel_count_ = 0;
};

}  // namespace facephys
