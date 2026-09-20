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

/*
  Reads /cfg/scenes.json and registers what it holds.

  Runs after the devices, because a scene refers to commands by name and they
  have to exist first. A scene whose name matches one compiled into the firmware
  replaces it - same rule as for devices.

  A step naming a command that does not exist is dropped and counted, the rest
  of the scene still works: a scene that switches the television on and then
  selects an input it does not know should still switch the television on.

  The maps, gui lists and sequences of a JSON scene are owned here. They have to
  outlive the registration, which is why they are not built on the stack.
*/
struct ScenesResult {
  bool fileFound = false;
  uint16_t scenesLoaded = 0;
  uint16_t keysBound = 0;
  uint16_t stepsDropped = 0; // commands the file names but the device does not know
  std::string error;         // empty unless the file was there and unusable
};

ScenesResult loadScenes();

/*
  Reads /cfg/keys.json and replaces the keypad layout.

  Refused outright if the file was written for another hardware revision: Rev5
  and Rev1-4 hold the same keys in reversed row order, so applying the wrong one
  mirrors the keypad top to bottom - and the user would be left pressing "up" to
  go down, with nothing in the log to explain it.
*/
struct KeysResult {
  bool fileFound = false;
  bool applied = false;
  bool revisionMismatch = false;
  std::string error;
};

KeysResult loadKeys();

/*
  Reads /cfg/ui.json and registers its screens.

  A screen replaces one of the same name compiled into the firmware - which is
  how gui_numpad becomes a file somebody can edit without a compiler.

  Nothing is drawn here. The screens are registered and kept; LVGL objects are
  only made when guiMemoryOptimizer decides that tab is due, which is what keeps
  three tabs in memory instead of all of them.
*/
struct UiResult {
  bool fileFound = false;
  uint16_t screensLoaded = 0;
  uint16_t screensReplaced = 0; // took the name of a screen written in C++
  std::string error;
};

UiResult loadUi();

} // namespace configLoader
