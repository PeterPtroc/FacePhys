#pragma once

#include <array>
#include <string>
#include <vector>

#include "facephys/types.h"

namespace facephys {

// Browser-equivalent ROI clamp and bilinear RGB resize.  The output uses NHWC
// component order R,G,B and normalises each byte by 255.0f.
bool crop_resize_rgb36(const RgbFrame& frame, const RectF& requested_roi,
                       std::array<float, 36U * 36U * 3U>& output,
                       RectF* actual_roi, std::string* error);

}  // namespace facephys
