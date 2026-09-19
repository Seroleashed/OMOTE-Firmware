#pragma once

#include <string>
#include <vector>

#include "applicationInternal/storage/configModel.h"
#include "applicationInternal/storage/configScenes.h"

/*
  Turns the configuration compiled into the firmware into the JSON files.

  Two reasons this exists before anything reads those files:

  * it produces realistic test data. Hand written JSON tests what the author
    imagined; an export of the real devices_pool tests what is actually there,
    including the payload formats nobody remembered.
  * it is the migration path. A user who has their devices in C++ today runs the
    export once and has them as files, instead of retyping 300 IR codes.

  What cannot be exported: the scene start and end sequences. They are C++
  functions with delay() in them, not data - see scene_TV.cpp. Step 9 turns them
  into the sequence structure that scenes.json already has a place for. Until
  then an exported scene has an empty sequence, and the export says so out loud
  rather than pretending the scene is complete.
*/

namespace configExport {

struct DeviceSelector {
  std::string deviceId;    // "samsungTV", becomes the file name
  std::string displayName; // "Samsung TV"
  std::string namePrefix;  // "SAMSUNG_", selects the commands
  std::string manufacturer;
  std::string model;
};

// One device pack per selector, taking the commands from the live registry.
configModel::DevicePack devicePack(const DeviceSelector &selector);

/*
  Every registered scene, with its key map, gui list and activation command.
  Calls each scene's setKeys() first, because the key maps of a scene are only
  filled once it has been activated at least once.
*/
configModel::ScenesConfig scenes();

// true if any exported scene had a start or end sequence that could not be
// represented - see the note above
bool scenesHaveUnexportableSequences();

// The keypad matrix as the hardware layer has it.
configModel::KeysConfig keys();

/*
  Everything at once, as one readable block for the serial console:

      ===== BEGIN /cfg/system.json =====
      { ... }
      ===== END /cfg/system.json =====

  The markers exist so a host side script can cut the files back out of a serial
  log without guessing where one ends and the next begins.
*/
std::string dumpConfigAsJson(const std::vector<DeviceSelector> &devices);

extern const char *const BEGIN_MARKER;
extern const char *const END_MARKER;

} // namespace configExport
