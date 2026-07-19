#include <chrono>
#include <iostream>
#include <thread>

#include "facephys/config.h"
#include "facephys/st7735.h"

int main(int argc, char** argv) {
  const std::string config_path = argc > 1 ? argv[1] : "../native/config/nanopi_neo_st7735.json";
  facephys::AppConfig config;
  std::string error;
  if (!facephys::load_config_file(config_path, &config, &error)) { std::cerr << "ERROR " << error << '\n'; return 1; }
  for (int index = 2; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg.rfind("--rotation=", 0) == 0) config.display.rotation = std::stoi(arg.substr(11));
    else if (arg.rfind("--x-offset=", 0) == 0) config.display.x_offset = std::stoi(arg.substr(11));
    else if (arg.rfind("--y-offset=", 0) == 0) config.display.y_offset = std::stoi(arg.substr(11));
    else if (arg.rfind("--speed=", 0) == 0) config.display.spi_speed_hz = std::stoi(arg.substr(8));
    else if (arg == "--rgb") config.display.bgr = false;
    else if (arg == "--invert") config.display.invert = true;
    else { std::cerr << "Unknown option: " << arg << '\n'; return 2; }
  }
  facephys::St7735 panel;
  if (!panel.open(config.display, &error)) { std::cerr << "ERROR " << error << '\n'; return 1; }
  const std::uint16_t colors[] = {facephys::rgb565(255,0,0), facephys::rgb565(0,255,0), facephys::rgb565(0,0,255), facephys::rgb565(255,255,255), facephys::rgb565(0,0,0)};
  for (const auto color : colors) { panel.clear(color); if (!panel.flush(&error)) { std::cerr << "ERROR " << error << '\n'; return 1; } std::this_thread::sleep_for(std::chrono::milliseconds(700)); }
  panel.clear(facephys::rgb565(0,0,0));
  const std::uint16_t bars[] = {facephys::rgb565(255,0,0), facephys::rgb565(255,255,0), facephys::rgb565(0,255,0), facephys::rgb565(0,255,255), facephys::rgb565(0,0,255), facephys::rgb565(255,0,255)};
  const int bar_width = panel.width() / 6;
  for (int i = 0; i < 6; ++i) panel.fill_rect(i * bar_width, 0, bar_width, panel.height() / 3, bars[i]);
  panel.text(4, panel.height() / 3 + 5, "ST7735 TEST", facephys::rgb565(255,255,255), facephys::rgb565(0,0,0), 1);
  panel.fill_rect(2, panel.height() / 3 + 18, panel.width() - 4, 20, facephys::rgb565(0,0,100));
  for (int x = 0; x < panel.width(); ++x) panel.line(x, panel.height() - 2, x, panel.height() - 25 - (x * 17 % 23), facephys::rgb565(0,255,255));
  if (!panel.flush(&error)) { std::cerr << "ERROR " << error << '\n'; return 1; }
  std::cout << "ST7735 test complete: rotation=" << config.display.rotation << " BGR=" << config.display.bgr << " SPI=" << config.display.spi_speed_hz << " Hz\n";
}
