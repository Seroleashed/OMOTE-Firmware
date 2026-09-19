#pragma once

#include <stdint.h>

#include <string>
#include <vector>

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

} // namespace configLoader
