#pragma once

#include <stdint.h>
#include <string>

/*
  Safe mode: a way back when a stored configuration keeps the device from
  starting.

  As soon as devices, scenes and screens come from JSON files, a bad file can
  make the firmware crash during setup(). The device would then reboot, crash
  again, and the only way out would be a USB cable - for a remote control that
  is meant to be configured from a browser, that is not acceptable.

  So every boot is counted. The counter is written *before* anything risky
  happens and cleared once setup() has finished. Three boots in a row that never
  reach the end of setup() therefore mean: the configuration is what kills us.
  The next boot then skips the stored configuration and comes up with what is
  compiled into the firmware.

  The counter is reset when safe mode is entered, so the device tries the stored
  configuration again on the boot after that. Safe mode is a rescue, not a
  one-way street.

  Why not "hold a key while switching on": on Rev5 the keypad sits on the I2C
  bus, and that bus is only powered by init_gui() - long after the configuration
  would have been read. requestSafeModeOnNextBoot() exists for that case: the
  settings screen (and later the web UI) can ask for safe mode and restart.
*/

class BootCounterStorage {
public:
  virtual ~BootCounterStorage() {}

  virtual uint8_t readFailedBoots() = 0;
  virtual void writeFailedBoots(uint8_t count) = 0;
  virtual bool readSafeModeRequested() = 0;
  virtual void writeSafeModeRequested(bool requested) = 0;
};

// provided by the active hardware layer
BootCounterStorage *get_bootCounterStorage();

namespace bootGuard {

// three, not two: a single crash can also come from a flaky power supply or a
// half finished flash, and that should not throw away a working configuration
const uint8_t FAILED_BOOTS_UNTIL_SAFE_MODE = 3;

enum class SafeModeReason {
  None,
  Requested,     // somebody asked for it
  RepeatedCrash, // setup() did not finish often enough in a row
};

/*
  Call this as early as possible in setup(), before anything that could read a
  stored configuration. Decides whether this boot runs in safe mode and counts
  the attempt.
*/
void begin(BootCounterStorage *storage);

bool isSafeMode();
SafeModeReason safeModeReason();
// short text for the log and the settings screen, e.g. "safe mode (repeated crash)"
std::string statusText();

// Call at the very end of setup(): this boot made it, clear the counter.
void markBootSuccessful();

// Next boot ignores the stored configuration. Takes effect after a restart.
void requestSafeModeOnNextBoot();
void cancelSafeModeOnNextBoot();
bool isSafeModeRequestedForNextBoot();

} // namespace bootGuard
