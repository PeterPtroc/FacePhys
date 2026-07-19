#include <cassert>
#include <vector>

#include "facephys/ring_buffer.h"

int main() {
  facephys::RingBuffer<int> ring(4);
  std::vector<int> values;
  ring.push(3);
  ring.push(4);
  ring.copy_chronological_padded(values);
  assert((values == std::vector<int>{0, 0, 3, 4}));
  ring.push(5);
  ring.push(6);
  ring.push(7);
  ring.copy_chronological_padded(values);
  assert((values == std::vector<int>{4, 5, 6, 7}));
}
