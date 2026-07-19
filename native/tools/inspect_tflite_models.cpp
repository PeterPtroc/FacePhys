#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <vector>

#include "facephys/face_detector.h"
#include "facephys/facephys_engine.h"
#include "facephys/signal_processor.h"
#include "facephys/tflite_model.h"
#include "tensorflow/lite/c/common.h"

namespace {
bool run_generic(const std::string& label, const std::string& path, bool xnnpack) {
  facephys::TfliteRunner runner;
  std::string error;
  if (!runner.load(path, {1, xnnpack}, &error)) { std::cerr << "ERROR " << label << ": " << error << '\n'; return false; }
  runner.describe_io(label);
  for (const int index : runner.interpreter()->inputs()) std::memset(runner.interpreter()->tensor(index)->data.raw, 0, runner.interpreter()->tensor(index)->bytes);
  if (runner.interpreter()->Invoke() != kTfLiteOk) { std::cerr << "ERROR " << label << ": zero-input Invoke failed\n"; return false; }
  std::cout << label << " zero-input Invoke: OK; XNNPACK=" << (runner.xnnpack_enabled() ? "ON" : "OFF")
            << "; delegate_kernels=" << runner.delegated_kernel_count() << '\n';
  return true;
}
}

int main(int argc, char** argv) {
  const std::filesystem::path model_dir = argc > 1 ? argv[1] : "../models";
  const bool xnnpack = argc > 2 && std::string(argv[2]) == "--xnnpack";
  const auto model = (model_dir / "model.tflite").string();
  const auto state = (model_dir / "state.gz").string();
  std::string error;
  // Print raw TFLite ordering before applying the browser/LiteRT state map.
  // LiteRT's JS metadata ordering differs for image/dt, so this is useful
  // when validating a new runtime version.
  if (!run_generic("FacePhys main raw model", model, xnnpack)) return 1;
  facephys::FacePhysEngine engine;
  if (!engine.initialize({model, state, 4, xnnpack}, &error)) { std::cerr << "ERROR main model: " << error << '\n'; return 1; }
  engine.describe_io();
  std::array<float, 36U * 36U * 3U> black{};
  facephys::FacePhysInference inference;
  std::vector<double> inference_ms;
  inference_ms.reserve(11);
  for (int frame = 0; frame < 11; ++frame) {
    if (!engine.invoke(black, 1.0F / 30.0F, &inference, &error)) {
      std::cerr << "ERROR main Invoke: " << error << '\n'; return 1;
    }
    inference_ms.push_back(inference.inference_ms);
  }
  const double steady_ms = std::accumulate(inference_ms.begin() + 1, inference_ms.end(), 0.0) / 10.0;
  std::cout << "FacePhys main Invoke: OK; BVP=" << inference.bvp
            << " first_ms=" << inference_ms.front() << " steady_avg_ms=" << steady_ms << "\n";
  if (!run_generic("FacePhys SQI raw model", (model_dir / "sqi_model.tflite").string(), false)) return 1;
  if (!run_generic("FacePhys PSD raw model", (model_dir / "psd_model.tflite").string(), false)) return 1;
  facephys::SignalProcessor signal;
  if (!signal.initialize({(model_dir / "sqi_model.tflite").string(), (model_dir / "psd_model.tflite").string(), 1, 1, 1000, 1000}, &error)) { std::cerr << "ERROR signal models: " << error << '\n'; return 1; }
  signal.describe_io(); signal.push_bvp(inference.bvp, 1.0F / 30.0F);
  facephys::SignalResult result;
  if (!signal.process_now(&result, &error)) { std::cerr << "ERROR SQI/PSD Invoke: " << error << '\n'; return 1; }
  std::cout << "SQI/PSD Invoke: OK; SQI=" << result.sqi << " BPM=" << result.bpm << "\n";
  if (!run_generic("Projection model", (model_dir / "proj.tflite").string(), false)) return 1;
  if (!run_generic("BlazeFace model", (model_dir / "blaze_face_short_range.tflite").string(), false)) return 1;
  facephys::BlazeFaceDetector detector;
  if (!detector.initialize({(model_dir / "blaze_face_short_range.tflite").string(), 1}, &error)) { std::cerr << "ERROR face detector: " << error << '\n'; return 1; }
  facephys::RgbFrame blank; blank.resize(128, 128);
  std::vector<facephys::RectF> faces;
  if (!detector.detect(blank, &faces, &error)) { std::cerr << "ERROR face detector Invoke: " << error << '\n'; return 1; }
  std::cout << "BlazeFace Invoke: OK; blank-frame detections=" << faces.size() << '\n';
}
