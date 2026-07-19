#include "facephys/camera.h"

#ifdef FACEPHYS_ENABLE_V4L2
#include "facephys/openmv_serial_camera.h"
#include "facephys/v4l2_camera.h"
#endif

namespace facephys {

std::unique_ptr<Camera> create_camera(const CameraConfig& config, std::string* error) {
#ifdef FACEPHYS_ENABLE_V4L2
  if (config.backend == "v4l2") return std::make_unique<V4L2Camera>();
  if (config.backend == "openmv_serial") return std::make_unique<OpenMvSerialCamera>();
#else
  (void)config;
#endif
  if (error) *error = "camera backend is unavailable in this build: " + config.backend;
  return nullptr;
}

void LatestFrame::publish(const RgbFrame& frame) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (latest_.width != frame.width || latest_.height != frame.height) latest_.resize(frame.width, frame.height);
  latest_.timestamp_ns = frame.timestamp_ns;
  latest_.sequence = frame.sequence;
  std::copy(frame.rgb.begin(), frame.rgb.end(), latest_.rgb.begin());
  available_ = true;
  condition_.notify_one();
}

bool LatestFrame::wait_copy_after(std::uint64_t sequence, RgbFrame* destination,
                                  std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (!condition_.wait_for(lock, timeout, [this, sequence] { return available_ && latest_.sequence > sequence; })) return false;
  if (destination->width != latest_.width || destination->height != latest_.height) destination->resize(latest_.width, latest_.height);
  destination->timestamp_ns = latest_.timestamp_ns;
  destination->sequence = latest_.sequence;
  std::copy(latest_.rgb.begin(), latest_.rgb.end(), destination->rgb.begin());
  return true;
}

void LatestFrame::wake_all() { condition_.notify_all(); }

}  // namespace facephys
