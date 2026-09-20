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

  The scene sequences come out too, since step 9. They are still C++ functions,
  but all they do now is enqueue steps into sequenceEngine - so the export lets
  one enqueue into an empty queue and takes the result, without running a single
  step. Nothing is sent at the TV in the living room while a configuration is
  exported.
*/

namespace configExport {

struct DeviceSelector {
  std::string deviceId;    // "samsungTV", becomes the file name
  std::string displayName; // "Samsung TV"
  std::string namePrefix;  // "SAMSUNG_", selects the commands
  std::string manufacturer;
  std::string model;
  std::string author;
  std::string notes;
};

/*
  Which IR protocol a pack uses, for the metadata.

  Worked out from the commands rather than asked for: the protocol is already
  in every IR payload, and a field somebody has to fill in by hand is a field
  that ends up wrong. Returns an empty string for a device that is not IR, and
  "mixed" for one that uses more than one - which is rare but real, some
  receivers answer to two.
*/
std::string protocolOf(const configModel::DevicePack &pack);

// One device pack per selector, taking the commands from the live registry.
configModel::DevicePack devicePack(const DeviceSelector &selector);

/*
  Every registered scene, with its key map, gui list and activation command.
  Calls each scene's setKeys() first, because the key maps of a scene are only
  filled once it has been activated at least once.
*/
configModel::ScenesConfig scenes();

// true if a sequence step had to be left out because its command has no name.
// That means a scene refers to something the registry does not know by name,
// which is worth reporting rather than dropping quietly.
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
