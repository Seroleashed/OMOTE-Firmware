#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "applicationInternal/storage/configModel.h"

/*
  Registers devices from /cfg/devices/*.json at startup.

  This is the step where the remote becomes configurable without a compiler.

  Three rules decide everything here, and all three exist so that a bad file can
  never leave the user with a device that does nothing:

  1. **Runs after the C++ registrations.** What is compiled into the firmware is
     registered first and stays the fallback. JSON is loaded on top.
  2. **A name that already exists is taken over by the JSON file**, with a
     warning in the log. That is what makes "edit the Samsung TV in the browser"
     work: the file wins over the compiled-in definition of the same command.
     The old command id keeps working, it just loses its name.
  3. **A broken file is skipped, not fatal.** Every other file still loads, and
     the reason is kept for the web UI to show later. One file with a typo must
     not cost the user their whole configuration.

  Safe mode (bootGuard) skips this entirely - that is the way back when a file
  manages to crash the firmware during startup.
*/

namespace configLoader {

struct FileResult {
  std::string path;
  bool loaded = false;
  std::string deviceId;
  uint16_t commandCount = 0;
  std::string error;     // empty if loaded
  uint16_t overrides = 0; // commands that took a name from an earlier definition
};

struct Report {
  bool skippedBecauseOfSafeMode = false;
  uint16_t devicesLoaded = 0;
  uint16_t commandsRegistered = 0;
  uint16_t filesFailed = 0;
  std::vector<FileResult> files;

  bool hasErrors() const { return filesFailed > 0; }
};

/*
  Reads every *.json in /cfg/devices/ and registers its commands.

  Files that configStorage leaves behind (.bak, .tmp) are skipped: they are
  storage bookkeeping, not configuration, and loading a .bak would silently
  resurrect the previous version of a device.
*/
Report loadDevices();

// The report of the last loadDevices(), for the settings screen and later the
// web UI. A user needs to be able to find out *why* their device is missing.
const Report &lastReport();

/*
  Reads /cfg/system.json and applies what it says: display brightness, sleep
  behaviour, and the name and broker the network side uses.

  ## Who wins

  If the file exists, it owns the settings it names. They are applied on every
  start, after the preferences have been restored - so a `push` of a new
  system.json takes effect on the next boot, and stays.

  That has a rough edge worth knowing about: a brightness changed with the
  slider on the device survives until the next restart, and then the file wins
  again. It is the predictable rule of the two, and it only applies at all to
  somebody who deliberately put a file there. Step 18 closes the loop by having
  the web UI write the file rather than the preferences.

  Fields the file does not mention keep whatever the preferences had, so a
  system.json with only a brightness in it does exactly that one thing.
*/
struct SystemResult {
  bool fileFound = false;
  bool applied = false;
  std::string error; // empty unless the file was there and unusable
};

SystemResult loadSystem();

// What the last loadSystem() ended up with, file or compiled-in defaults.
// The network side asks this for the device name and the broker.
const configModel::SystemConfig &systemConfig();

} // namespace configLoader
