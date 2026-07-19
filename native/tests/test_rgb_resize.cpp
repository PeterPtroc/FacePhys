#include <array>
#include <cassert>
#include <cmath>
#include <string>

#include "facephys/image_preprocess.h"

int main() {
  facephys::RgbFrame frame;
  frame.resize(2, 2);
  // Red, green, blue, white in row-major RGB order.
  frame.rgb = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
  std::array<float, 36U * 36U * 3U> output{};
  facephys::RectF actual;
  std::string error;
  assert(facephys::crop_resize_rgb36(frame, {0, 0, 2, 2}, output, &actual, &error));
  assert(actual.width == 2.0F && actual.height == 2.0F);
  assert(std::fabs(output[0] - 1.0F) < 1e-5F);
  const std::size_t last = output.size() - 3U;
  assert(std::fabs(output[last] - 1.0F) < 1e-5F);
  assert(std::fabs(output[last + 1U] - 1.0F) < 1e-5F);
  assert(std::fabs(output[last + 2U] - 1.0F) < 1e-5F);
}
