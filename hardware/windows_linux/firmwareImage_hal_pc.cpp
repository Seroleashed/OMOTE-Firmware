#if defined(WIN32) || defined(__linux__) || defined(__APPLE__)

#include "applicationInternal/hardware/firmwareImage.h"

/*
  The simulator has no partitions and nothing to roll back to. It reports itself
  as such, so the settings screen shows something honest instead of pretending
  to be an ESP32.
*/

FirmwareImageInfo get_firmwareImageInfo() {
  FirmwareImageInfo info;
  info.state = FirmwareBootState::Unknown;
  info.runningPartition = "simulator";
  info.otaCapable = false;
  return info;
}

void confirm_firmwareIsWorking() {
  // nothing to confirm
}

#endif
