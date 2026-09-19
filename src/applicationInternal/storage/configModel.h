#pragma once

#include <stdint.h>

#include <list>
#include <string>
#include <vector>

#include "applicationInternal/commandHandler.h"

/*
  Schema of the configuration files (version 1).

  A device pack is the exchange format for a single device. It is what the web
  UI edits, what gets exported over USB or BLE and what the community shares:

      {
        "schemaVersion": 1,
        "type": "omote.devicePack",
        "device": {
          "id": "samsungTV",
          "name": "Samsung TV",
          "manufacturer": "Samsung",
          "model": "UE55"
        },
        "commands": [
          { "name": "SAMSUNG_POWER", "handler": "IR",
            "payloads": ["7", "0xE0E040BF"] },
          { "name": "SAMSUNG_VOL_UP", "handler": "IR",
            "payloads": ["7", "0xE0E0E01F"] }
        ]
      }

  Rules that the tests pin down:
  * "name" is the stable reference, ids never appear in a file
  * "handler" is the enum name, not a number, so the file stays readable and
    survives a reordering of the enum
  * unknown handlers, a missing type or a newer schemaVersion are rejected
    with a message instead of being silently ignored
  * unknown *extra* fields are kept out of the way and ignored, so a file
    written by a newer web UI still loads
*/

namespace configModel {

const uint16_t SCHEMA_VERSION = 1;
const char *const TYPE_DEVICE_PACK = "omote.devicePack";

struct CommandDef {
  std::string name;
  commandHandlers handler = IR;
  std::list<std::string> payloads;
};

struct DevicePack {
  std::string id;
  std::string name;
  std::string manufacturer;
  std::string model;
  std::vector<CommandDef> commands;
};

// enum <-> string, so the file does not depend on the order of the enum
std::string handlerToString(commandHandlers handler);
bool handlerFromString(const std::string &text, commandHandlers &handler);

std::string serializeDevicePack(const DevicePack &pack);
// on failure, error contains a message meant for the user / the web UI
bool parseDevicePack(const std::string &json, DevicePack &pack, std::string &error);

/*
  Builds a device pack from the commands currently registered in the firmware.
  Only commands whose name starts with namePrefix are taken (empty prefix takes
  all of them). This is how the configuration compiled into the firmware is
  exported and turned into JSON files.
*/
DevicePack devicePackFromRegisteredCommands(const std::string &deviceId, const std::string &deviceName,
                                            const std::string &namePrefix);

} // namespace configModel
