#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "applicationInternal/hardware/firmwareImage.h"
#include "applicationInternal/omote_log.h"

/*
  Rollback protection on the ESP32.

  Rev5 uses ota_16MB_custom.csv with app0 and app1, so esp_ota_get_next_update_
  partition() finds a second slot and the device is OTA capable. Rev1-4 uses
  huge_app.csv with a single app partition: everything below still works, the
  state is simply reported as Unknown and otaCapable stays false.
*/

FirmwareImageInfo get_firmwareImageInfo() {
  FirmwareImageInfo info;

  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running == NULL) return info;

  info.runningPartition = running->label;
  info.otaCapable = (esp_ota_get_next_update_partition(running) != NULL);

  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
    // no otadata partition (Rev1-4) - not an error, just nothing to report
    return info;
  }

  if (state == ESP_OTA_IMG_PENDING_VERIFY) {
    info.state = FirmwareBootState::PendingVerify;
  } else {
    info.state = FirmwareBootState::Valid;
  }
  return info;
}

void confirm_firmwareIsWorking() {
  FirmwareImageInfo info = get_firmwareImageInfo();

  if (info.state != FirmwareBootState::PendingVerify) {
    omote_log_d("firmwareImage: running from '%s', state '%s', nothing to confirm\r\n",
                info.runningPartition.c_str(), firmwareBootStateToString(info.state).c_str());
    return;
  }

  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
    omote_log_i("firmwareImage: update in '%s' confirmed, rollback cancelled\r\n",
                info.runningPartition.c_str());
  } else {
    // the next reset will start the previous image again - which is the point
    omote_log_e("firmwareImage: could not confirm the update in '%s'\r\n",
                info.runningPartition.c_str());
  }
}
