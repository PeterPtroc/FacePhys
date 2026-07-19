#include "facephys/application_state.h"

namespace facephys {
const char* run_status_name(RunStatus status) {
  switch (status) {
    case RunStatus::kStarting: return "START";
    case RunStatus::kRun: return "RUN";
    case RunStatus::kNoFace: return "NO FACE";
    case RunStatus::kCameraError: return "CAMERA ERROR";
    case RunStatus::kModelError: return "MODEL ERROR";
    case RunStatus::kStopping: return "STOPPING";
  }
  return "UNKNOWN";
}
}  // namespace facephys
