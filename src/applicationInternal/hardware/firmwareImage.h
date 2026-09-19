#pragma once

#include <string>

/*
  The running firmware image and the rollback protection that goes with it.

  With two app slots (see ota_16MB_custom.csv) the bootloader can start an
  updated image and fall back to the previous one if the update turns out to be
  broken. The deal is: after an OTA the new image boots in the state
  "pending verify" and has to confirm itself. Without that confirmation the
  bootloader starts the old image again on the next reset.

  confirm_firmwareIsWorking() is that confirmation. It is deliberately called at
  the very end of setup(), when hardware, storage and the gui are up - that is
  the cheapest self test available, and it is the one that catches the update
  that bricks the boot.

  The calls are harmless on a build without OTA (Rev1-4): there is no otadata
  partition, the state comes back as Unknown and confirming does nothing.

  Caveat for step 27: a true automatic rollback additionally needs
  CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE in the sdkconfig. The stock Arduino core
  is built without it, so today an image always boots as "valid" and the
  confirmation is a no-op. The code is here anyway, because the call has to be
  in the right place *before* the first OTA is ever installed.

  Implementations:
    hardware/ESP32/firmwareImage_hal_esp32.cpp          esp_ota_ops
    hardware/windows_linux/firmwareImage_hal_pc.cpp     reports the simulator
*/

enum class FirmwareBootState {
  Valid,         // confirmed image, the normal case
  PendingVerify, // freshly installed via OTA, waiting for confirmation
  Unknown,       // no OTA support in this build, or the state cannot be read
};

struct FirmwareImageInfo {
  FirmwareBootState state = FirmwareBootState::Unknown;
  std::string runningPartition; // e.g. "app0", "app1", "simulator"
  bool otaCapable = false;      // is there a second app slot to update into?
};

FirmwareImageInfo get_firmwareImageInfo();

// Confirms the running image. Does nothing if it is not pending verification.
void confirm_firmwareIsWorking();

// for the log and the diagnostics screen
std::string firmwareBootStateToString(FirmwareBootState state);
