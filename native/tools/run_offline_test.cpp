#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "facephys/config.h"
#include "facephys/facephys_engine.h"
#include "facephys/image_io.h"
#include "facephys/image_preprocess.h"
#include "facephys/signal_processor.h"

int main(int argc, char** argv) {
  if (argc < 5) { std::cerr << "Usage: run_offline_test CONFIG PPM_DIRECTORY CSV_OUTPUT FPS [x y width height]\n"; return 2; }
  facephys::AppConfig config; std::string error;
  if (!facephys::load_config_file(argv[1], &config, &error)) { std::cerr << "ERROR " << error << '\n'; return 1; }
  const float dt = 1.0F / std::stof(argv[4]);
  std::optional<facephys::RectF> roi = config.camera.fixed_roi;
  if (argc == 9) roi = facephys::RectF{std::stof(argv[5]), std::stof(argv[6]), std::stof(argv[7]), std::stof(argv[8])};
  if (!roi || !roi->valid()) { std::cerr << "ERROR fixed ROI required: add x y width height or set camera.fixed_roi\n"; return 2; }
  std::vector<std::filesystem::path> images;
  for (const auto& entry : std::filesystem::directory_iterator(argv[2])) if (entry.is_regular_file() && entry.path().extension() == ".ppm") images.push_back(entry.path());
  std::sort(images.begin(), images.end());
  if (images.empty()) { std::cerr << "ERROR no .ppm files found\n"; return 1; }
  facephys::FacePhysEngine engine;
  if (!engine.initialize({config.models.main_model, config.models.state, config.inference.main_threads, config.inference.enable_xnnpack}, &error)) { std::cerr << "ERROR " << error << '\n'; return 1; }
  facephys::SignalProcessor signal;
  if (!signal.initialize({config.models.sqi, config.models.psd, config.signal.sqi_threads, config.signal.psd_threads, config.signal.sqi_interval_ms, config.signal.psd_interval_ms}, &error)) { std::cerr << "ERROR " << error << '\n'; return 1; }
  std::ofstream csv(argv[3]);
  if (!csv) { std::cerr << "ERROR cannot create CSV\n"; return 1; }
  csv << "timestamp,dt,bvp,sqi,bpm,inference_ms\n";
  auto virtual_now = std::chrono::steady_clock::now();
  for (std::size_t index = 0; index < images.size(); ++index) {
    facephys::RgbFrame frame;
    if (!facephys::read_ppm(images[index].string(), &frame, &error)) { std::cerr << "ERROR " << images[index] << ": " << error << '\n'; return 1; }
    std::array<float, 36U * 36U * 3U> input{};
    if (!facephys::crop_resize_rgb36(frame, *roi, input, nullptr, &error)) { std::cerr << "ERROR preprocess " << images[index] << ": " << error << '\n'; return 1; }
    facephys::FacePhysInference inference;
    if (!engine.invoke(input, dt, &inference, &error)) { std::cerr << "ERROR main model frame " << index << ": " << error << '\n'; return 1; }
    signal.push_bvp(inference.bvp, dt);
    virtual_now += std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(dt));
    facephys::SignalResult result;
    if (!signal.process_if_due(virtual_now, &result, &error)) { std::cerr << "ERROR signal frame " << index << ": " << error << '\n'; return 1; }
    csv << static_cast<double>(index) * dt << ',' << dt << ',' << inference.bvp << ',' << result.sqi << ',' << (result.has_bpm ? result.bpm : 0.0F) << ',' << inference.inference_ms << '\n';
  }
  std::cout << "Offline test complete: frames=" << images.size() << " CSV=" << argv[3] << '\n';
}
