#include "facephys/image_preprocess.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace facephys {
namespace {

float sample_channel(const RgbFrame& frame, float x, float y, int channel) {
  x = std::clamp(x, 0.0F, static_cast<float>(frame.width - 1));
  y = std::clamp(y, 0.0F, static_cast<float>(frame.height - 1));
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const int x1 = std::min(x0 + 1, frame.width - 1);
  const int y1 = std::min(y0 + 1, frame.height - 1);
  const float fx = x - static_cast<float>(x0);
  const float fy = y - static_cast<float>(y0);
  const auto at = [&frame, channel](int px, int py) {
    return static_cast<float>(frame.rgb[(static_cast<std::size_t>(py) * frame.width + px) *
                                         3U + static_cast<std::size_t>(channel)]);
  };
  const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
  const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
  return top + (bottom - top) * fy;
}

}  // namespace

bool crop_resize_rgb36(const RgbFrame& frame, const RectF& requested_roi,
                       std::array<float, 36U * 36U * 3U>& output,
                       RectF* actual_roi, std::string* error) {
  if (!frame.valid()) {
    if (error) *error = "invalid RGB frame";
    return false;
  }
  if (!requested_roi.valid()) {
    if (error) *error = "invalid face ROI";
    return false;
  }

  // This mirrors processFrame(): sx/sy are clamped, then sw/sh are clamped to
  // the remaining canvas.  It intentionally is not an arbitrary centre crop.
  RectF roi;
  roi.x = std::max(0.0F, requested_roi.x);
  roi.y = std::max(0.0F, requested_roi.y);
  roi.width = std::min(requested_roi.width, static_cast<float>(frame.width) - roi.x);
  roi.height = std::min(requested_roi.height, static_cast<float>(frame.height) - roi.y);
  if (!roi.valid()) {
    if (error) *error = "face ROI lies outside RGB frame";
    return false;
  }
  if (actual_roi) *actual_roi = roi;

  constexpr int kSide = 36;
  for (int y = 0; y < kSide; ++y) {
    const float source_y = roi.y + ((static_cast<float>(y) + 0.5F) * roi.height / kSide) - 0.5F;
    for (int x = 0; x < kSide; ++x) {
      const float source_x = roi.x + ((static_cast<float>(x) + 0.5F) * roi.width / kSide) - 0.5F;
      const std::size_t index = (static_cast<std::size_t>(y) * kSide + x) * 3U;
      output[index] = sample_channel(frame, source_x, source_y, 0) / 255.0F;
      output[index + 1U] = sample_channel(frame, source_x, source_y, 1) / 255.0F;
      output[index + 2U] = sample_channel(frame, source_x, source_y, 2) / 255.0F;
    }
  }
  return true;
}

}  // namespace facephys
