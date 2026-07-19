#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "facephys/application_state.h"
#include "facephys/camera.h"
#include "facephys/config.h"
#include "facephys/face_detector.h"
#include "facephys/facephys_engine.h"
#include "facephys/image_preprocess.h"
#include "facephys/log.h"
#include "facephys/signal_processor.h"
#include "facephys/tft_display.h"

namespace {
std::atomic<bool>* g_running = nullptr;
void on_signal(int) { if (g_running != nullptr) g_running->store(false); }

std::vector<float> display_waveform(const std::vector<float>& all) {
  std::vector<float> result;
  constexpr std::size_t kVisible = 160;
  result.reserve(kVisible);
  if (all.empty()) return result;
  const std::size_t start = all.size() > kVisible ? all.size() - kVisible : 0U;
  result.insert(result.end(), all.begin() + static_cast<std::ptrdiff_t>(start), all.end());
  return result;
}
}

int main(int argc, char** argv) {
  const std::string config_path = argc > 1 ? argv[1] : "../native/config/nanopi_neo_st7735.json";
  facephys::AppConfig config;
  std::string error;
  if (!facephys::load_config_file(config_path, &config, &error)) { FP_ERROR(error); return 1; }
  facephys::FacePhysEngine engine;
  if (!engine.initialize({config.models.main_model, config.models.state, config.inference.main_threads, config.inference.enable_xnnpack}, &error)) { FP_ERROR("main model: " + error); return 1; }
  facephys::SignalProcessor signal;
  if (!signal.initialize({config.models.sqi, config.models.psd, config.signal.sqi_threads, config.signal.psd_threads, config.signal.sqi_interval_ms, config.signal.psd_interval_ms}, &error)) { FP_ERROR("signal models: " + error); return 1; }
  facephys::FaceTracker tracker;
  if (!tracker.initialize({config.models.face_detector, 1, 0.55F, 0.3F}, config.inference.face_detection_interval_ms, config.camera.fixed_roi, &error)) { FP_ERROR("face tracker: " + error); return 1; }

  std::atomic<bool> running{true};
  g_running = &running;
  std::signal(SIGINT, on_signal); std::signal(SIGTERM, on_signal);
  facephys::ApplicationState state;
  facephys::LatestFrame latest_frame;
  std::mutex signal_mutex;

  std::thread capture_thread([&] {
    facephys::RgbFrame frame;
    std::string thread_error;
    while (running.load()) {
      auto camera = facephys::create_camera(config.camera, &thread_error);
      if (!camera || !camera->open(config.camera, &thread_error)) {
        auto snapshot = state.snapshot(); snapshot.status = facephys::RunStatus::kCameraError; snapshot.detail = thread_error; state.update(snapshot); FP_WARN("camera open: " + thread_error);
        std::this_thread::sleep_for(std::chrono::seconds(1)); continue;
      }
      FP_INFO("camera backend " + config.camera.backend + " opened at " + std::to_string(camera->width()) + "x" + std::to_string(camera->height()));
      while (running.load()) {
        std::string capture_error;
        if (!camera->capture(&frame, &capture_error)) { FP_WARN("camera capture: " + capture_error); break; }
        latest_frame.publish(frame);
      }
      camera->close();
    }
  });

  std::thread inference_thread([&] {
    facephys::RgbFrame frame;
    std::uint64_t last_sequence = 0;
    std::uint64_t last_timestamp = 0;
    std::uint64_t fps_frames = 0;
    auto fps_start = std::chrono::steady_clock::now();
    bool had_face = false;
    std::string inference_error;
    while (running.load()) {
      const auto loop_start = std::chrono::steady_clock::now();
      if (!latest_frame.wait_copy_after(last_sequence, &frame, std::chrono::milliseconds(250))) continue;
      last_sequence = frame.sequence;
      facephys::RectF roi;
      std::string tracker_error;
      if (!tracker.roi_for_frame(frame, loop_start, &roi, &tracker_error)) {
        if (had_face) { std::lock_guard<std::mutex> lock(signal_mutex); engine.reset(&inference_error); signal.reset(); }
        had_face = false;
        last_timestamp = 0;
        auto snapshot = state.snapshot(); snapshot.status = facephys::RunStatus::kNoFace; snapshot.has_face = false; snapshot.bpm = 0.0F; snapshot.sqi = 0.0F; snapshot.detail = tracker_error; state.update(snapshot);
        continue;
      }
      had_face = true;
      // The browser computes dt between model invocations, not between camera
      // callbacks.  Measure it only after a usable ROI exists so a low-rate
      // detector pass while no face is present cannot reset recurrent state.
      float dt = 1.0F / 30.0F;
      if (last_timestamp != 0U) dt = static_cast<float>(frame.timestamp_ns - last_timestamp) / 1'000'000'000.0F;
      last_timestamp = frame.timestamp_ns;
      if (!std::isfinite(dt) || dt < 0.005F || dt > 1.0F) {
        FP_WARN("abnormal model-input dt; resetting recurrent state");
        std::lock_guard<std::mutex> lock(signal_mutex); engine.reset(&inference_error); signal.reset(); dt = 1.0F / 30.0F;
      }
      std::array<float, 36U * 36U * 3U> input{};
      if (!facephys::crop_resize_rgb36(frame, roi, input, nullptr, &inference_error)) { FP_WARN("ROI preprocessing: " + inference_error); continue; }
      facephys::FacePhysInference inference;
      if (!engine.invoke(input, dt, &inference, &inference_error)) {
        auto snapshot = state.snapshot(); snapshot.status = facephys::RunStatus::kModelError; snapshot.has_face = false; snapshot.detail = inference_error; state.update(snapshot); FP_ERROR(inference_error); engine.reset(&inference_error); continue;
      }
      { std::lock_guard<std::mutex> lock(signal_mutex); signal.push_bvp(inference.bvp, dt); }
      ++fps_frames;
      const auto now = std::chrono::steady_clock::now();
      const double fps_elapsed = std::chrono::duration<double>(now - fps_start).count();
      const float fps = fps_elapsed >= 1.0 ? static_cast<float>(fps_frames / fps_elapsed) : state.snapshot().fps;
      if (fps_elapsed >= 1.0) { fps_start = now; fps_frames = 0; }
      auto snapshot = state.snapshot(); snapshot.status = facephys::RunStatus::kRun; snapshot.has_face = true; snapshot.inference_ms = static_cast<float>(inference.inference_ms); snapshot.fps = fps; snapshot.detail.clear(); state.update(snapshot);
      const auto target = std::chrono::milliseconds(1000 / std::max(1, config.inference.target_fps));
      const auto elapsed = std::chrono::steady_clock::now() - loop_start;
      if (elapsed < target) std::this_thread::sleep_for(target - elapsed);
    }
  });

  std::thread signal_thread([&] {
    (void)setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);  // PSD stays low priority and single-threaded.
    while (running.load()) {
      facephys::SignalResult result;
      std::string signal_error;
      std::vector<float> waveform;
      { std::lock_guard<std::mutex> lock(signal_mutex);
        if (!signal.process_if_due(std::chrono::steady_clock::now(), &result, &signal_error)) FP_WARN("signal processing: " + signal_error);
        waveform = display_waveform(signal.waveform_chronological());
      }
      auto snapshot = state.snapshot();
      if (snapshot.has_face) { snapshot.sqi = result.sqi; snapshot.bpm = (result.has_bpm && result.sqi > 0.38F) ? result.bpm : 0.0F; snapshot.waveform = std::move(waveform); state.update(snapshot); }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
  });

  std::thread display_thread;
  if (config.display.enabled) {
    display_thread = std::thread([&] {
      facephys::TftDisplay display;
      std::string display_error;
      if (!display.open(config.display, &display_error)) { FP_WARN("TFT disabled: " + display_error); return; }
      while (running.load()) {
        if (!display.render(state.snapshot(), &display_error)) { FP_WARN("TFT update: " + display_error); break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(config.display.display_interval_ms));
      }
      display.close();
    });
  }
  while (running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  latest_frame.wake_all();
  capture_thread.join(); inference_thread.join(); signal_thread.join();
  if (display_thread.joinable()) display_thread.join();
  state.update({facephys::RunStatus::kStopping});
  FP_INFO("FacePhys native stopped cleanly");
}
