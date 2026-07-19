#pragma once

#include <optional>
#include <string>

#include "facephys/types.h"

namespace facephys {

struct ModelsConfig {
  std::string main_model;
  std::string state;
  std::string sqi;
  std::string psd;
  std::string face_detector;
};
struct CameraConfig {
  // "v4l2" for UVC cameras, or "openmv_serial" for the USB CDC stream
  // emitted by openmv/openmv_facephys_stream.py.
  std::string backend = "v4l2";
  std::string device = "/dev/video0";
  int width = 320;
  int height = 240;
  int fps = 15;
  std::string pixel_format = "YUYV";
  int serial_timeout_ms = 1000;
  int max_frame_bytes = 262144;
  std::optional<RectF> fixed_roi;
};
struct InferenceConfig {
  int main_threads = 4;
  bool enable_xnnpack = true;
  int target_fps = 15;
  int face_detection_interval_ms = 1000;
};
struct SignalConfig {
  int window_samples = 450;
  int sqi_interval_ms = 1000;
  int psd_interval_ms = 1000;
  int psd_threads = 1;
  int sqi_threads = 1;
};
struct DisplayConfig {
  bool enabled = true;
  std::string driver = "st7735";
  std::string spi_device = "/dev/spidev0.0";
  int spi_speed_hz = 8000000;
  int width = 128;
  int height = 160;
  int rotation = 1;
  int x_offset = 0;
  int y_offset = 0;
  bool bgr = true;
  bool invert = false;
  std::string tab = "green_tab";
  std::string dc_gpiochip = "/dev/gpiochip0";
  int dc_gpio_line = 203;
  int dc_gpio_number = 203;
  std::string reset_gpiochip = "/dev/gpiochip0";
  int reset_gpio_line = 6;
  int reset_gpio_number = 6;
  int display_interval_ms = 100;
};
struct AppConfig {
  ModelsConfig models;
  CameraConfig camera;
  InferenceConfig inference;
  SignalConfig signal;
  DisplayConfig display;
};

bool load_config_file(const std::string& path, AppConfig* config, std::string* error);

}  // namespace facephys
