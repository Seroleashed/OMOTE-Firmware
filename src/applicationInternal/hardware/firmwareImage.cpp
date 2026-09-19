#include "applicationInternal/hardware/firmwareImage.h"

// The platform independent part. The two other functions are implemented per
// platform in hardware/ESP32/ and hardware/windows_linux/.
std::string firmwareBootStateToString(FirmwareBootState state) {
  switch (state) {
    case FirmwareBootState::Valid: return "valid";
    case FirmwareBootState::PendingVerify: return "pending verify";
    case FirmwareBootState::Unknown: return "unknown";
  }
  return "unknown";
}
