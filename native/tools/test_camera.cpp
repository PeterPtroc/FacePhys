#include <chrono>
#include <iostream>

#include "facephys/camera.h"
#include "facephys/config.h"
#include "facephys/image_io.h"

int main(int argc, char** argv) {
  const std::string config_path = argc > 1 ? argv[1] : "../native/config/nanopi_neo_st7735.json";
  facephys::AppConfig config;
  std::string error;
  if (!facephys::load_config_file(config_path, &config, &error)) { std::cerr << "ERROR " << error << '\n'; return 1; }
  auto camera = facephys::create_camera(config.camera, &error);
  if (!camera || !camera->open(config.camera, &error)) { std::cerr << "ERROR " << error << '\n'; return 1; }
  std::cout << "Camera opened: backend=" << config.camera.backend << ' ' << camera->width() << 'x' << camera->height() << " requested=" << config.camera.pixel_format << '\n';
  facephys::RgbFrame frame;
  const auto start = std::chrono::steady_clock::now();
  constexpr int kFrames = 30;
  for (int i = 0; i < kFrames; ++i) {
    if (!camera->capture(&frame, &error)) { std::cerr << "ERROR capture " << i << ": " << error << '\n'; return 1; }
    if (i == 0 && !facephys::write_ppm("camera_frame.ppm", frame, &error)) { std::cerr << "ERROR " << error << '\n'; return 1; }
  }
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::cout << "Captured " << kFrames << " frames in " << seconds << " s; actual FPS=" << kFrames / seconds << "; saved camera_frame.ppm\n";
}
