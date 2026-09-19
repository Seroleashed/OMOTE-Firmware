#include <Preferences.h>

#include "applicationInternal/bootGuard.h"

/*
  Boot counter in NVS.

  Deliberately in its own namespace and not in "settings": it is written before
  the preferences are restored, it is written on every single boot, and it must
  survive a "reset the settings to default". Its own namespace also keeps the
  wear on those few NVS entries away from everything else.

  begin()/end() around every access, exactly like preferencesStorage does, so
  the handle is never left open while the rest of setup() runs.
*/

static const char *const NAMESPACE = "bootguard";

class NvsBootCounterStorage : public BootCounterStorage {
public:
  uint8_t readFailedBoots() override {
    Preferences preferences;
    if (!preferences.begin(NAMESPACE, true)) return 0; // namespace does not exist yet
    uint8_t value = (uint8_t)preferences.getUChar("failedBoots", 0);
    preferences.end();
    return value;
  }

  void writeFailedBoots(uint8_t count) override {
    Preferences preferences;
    if (!preferences.begin(NAMESPACE, false)) return;
    preferences.putUChar("failedBoots", count);
    preferences.end();
  }

  bool readSafeModeRequested() override {
    Preferences preferences;
    if (!preferences.begin(NAMESPACE, true)) return false;
    bool value = preferences.getBool("safeMode", false);
    preferences.end();
    return value;
  }

  void writeSafeModeRequested(bool requested) override {
    Preferences preferences;
    if (!preferences.begin(NAMESPACE, false)) return;
    preferences.putBool("safeMode", requested);
    preferences.end();
  }
};

BootCounterStorage *get_bootCounterStorage() {
  static NvsBootCounterStorage storage;
  return &storage;
}
