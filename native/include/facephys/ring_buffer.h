#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace facephys {

template <typename T>
class RingBuffer {
 public:
  explicit RingBuffer(std::size_t capacity) : data_(capacity) {
    if (capacity == 0U) throw std::invalid_argument("RingBuffer capacity is zero");
  }

  void push(T value) {
    data_[write_] = value;
    write_ = (write_ + 1U) % data_.size();
    size_ = std::min(size_ + 1U, data_.size());
  }

  void clear() { write_ = 0U; size_ = 0U; }
  [[nodiscard]] std::size_t size() const { return size_; }
  [[nodiscard]] std::size_t capacity() const { return data_.size(); }
  [[nodiscard]] bool full() const { return size_ == data_.size(); }

  // Match main.js: samples are chronological and zeros occupy the oldest part
  // of the vector until the 450-point buffer becomes full.
  void copy_chronological_padded(std::vector<T>& destination) const {
    destination.assign(data_.size(), T{});
    if (size_ == 0U) return;
    const std::size_t first = full() ? write_ : 0U;
    const std::size_t offset = data_.size() - size_;
    for (std::size_t i = 0; i < size_; ++i) {
      destination[offset + i] = data_[(first + i) % data_.size()];
    }
  }

 private:
  std::vector<T> data_;
  std::size_t write_ = 0U;
  std::size_t size_ = 0U;
};

}  // namespace facephys
