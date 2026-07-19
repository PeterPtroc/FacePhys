#include <array>
#include <cassert>

#include "facephys/state_mapping.h"

int main() {
  std::array<bool, 48> inputs{};
  std::array<bool, 47> outputs{};
  for (const auto entry : facephys::kBrowserLiteRtStateMap) {
    assert(entry.input_position >= 2 && entry.input_position < 48);
    assert(entry.output_position >= 1 && entry.output_position < 47);
    assert(!inputs[entry.input_position]);
    inputs[entry.input_position] = true;
    assert(!outputs[entry.output_position]);
    outputs[entry.output_position] = true;
  }
  for (int i = 2; i < 48; ++i) assert(inputs[i]);
  for (const auto entry : facephys::kNativeTfliteStateMap) {
    assert(entry.input_position >= 2 && entry.input_position < 48);
    assert(entry.output_position == entry.input_position - 1);
  }
}
