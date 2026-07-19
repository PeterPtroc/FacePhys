#include "facephys/tft_display.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace facephys {
namespace {
constexpr auto kBlack = rgb565(0, 0, 0);
constexpr auto kWhite = rgb565(255, 255, 255);
constexpr auto kCyan = rgb565(0, 220, 255);
constexpr auto kGreen = rgb565(0, 255, 80);
constexpr auto kYellow = rgb565(255, 220, 0);
constexpr auto kRed = rgb565(255, 40, 40);
std::string fixed(float value, int decimals) { std::ostringstream out; out << std::fixed << std::setprecision(decimals) << value; return out.str(); }
}  // namespace

bool TftDisplay::open(const DisplayConfig& config, std::string* error) { return panel_.open(config, error); }
void TftDisplay::close() { panel_.close(); }
bool TftDisplay::render(const StateSnapshot& snapshot, std::string* error) {
  if (!panel_.is_open()) { if (error) *error = "TFT display not open"; return false; }
  const int width = panel_.width(), height = panel_.height();
  panel_.clear(kBlack);
  const auto status_color = snapshot.status == RunStatus::kRun ? kGreen : (snapshot.status == RunStatus::kNoFace ? kYellow : kRed);
  panel_.text(2, 2, run_status_name(snapshot.status), status_color, kBlack, 1);
  panel_.text(std::max(2, width - 48), 2, "FPS " + fixed(snapshot.fps, 0), kWhite, kBlack, 1);
  if (snapshot.has_face && snapshot.bpm > 0.0F) {
    panel_.text(std::max(4, width / 2 - 42), 20, fixed(snapshot.bpm, 0), kCyan, kBlack, 4);
    panel_.text(std::max(4, width / 2 - 16), 52, "BPM", kWhite, kBlack, 1);
  } else panel_.text(std::max(4, width / 2 - 20), 25, "-", kYellow, kBlack, 4);
  const std::string quality = snapshot.sqi > 0.65F ? "GOOD" : (snapshot.sqi > 0.38F ? "FAIR" : "POOR");
  panel_.text(2, 66, "SQI " + fixed(snapshot.sqi, 2) + " " + quality, snapshot.sqi > 0.38F ? kGreen : kYellow, kBlack, 1);
  panel_.text(2, 76, "INF " + fixed(snapshot.inference_ms, 0) + "MS", kWhite, kBlack, 1);
  const int graph_top = 90, graph_bottom = height - 3;
  panel_.line(0, graph_top, width - 1, graph_top, rgb565(40, 40, 40));
  if (snapshot.waveform.size() > 1U && graph_bottom > graph_top + 4) {
    const auto minmax = std::minmax_element(snapshot.waveform.begin(), snapshot.waveform.end());
    const float range = std::max(1e-6F, *minmax.second - *minmax.first);
    int previous_x = 0;
    int previous_y = graph_bottom - static_cast<int>((*snapshot.waveform.begin() - *minmax.first) / range * (graph_bottom - graph_top));
    for (int x = 1; x < width; ++x) {
      const std::size_t index = static_cast<std::size_t>(x) * (snapshot.waveform.size() - 1U) / static_cast<std::size_t>(width - 1);
      const int y = graph_bottom - static_cast<int>((snapshot.waveform[index] - *minmax.first) / range * (graph_bottom - graph_top));
      panel_.line(previous_x, previous_y, x, y, kCyan); previous_x = x; previous_y = y;
    }
  }
  return panel_.flush(error);
}

}  // namespace facephys
