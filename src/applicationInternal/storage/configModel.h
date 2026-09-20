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

/*
  One version number per file type, not one for the whole configuration: a new
  field in the scenes file must not force every device pack in the wild to be
  rewritten. The paths and type strings live in configFile.h, which is what the
  files have in common.
*/
const uint16_t SCHEMA_VERSION = 1;          // device pack
const uint16_t SYSTEM_SCHEMA_VERSION = 1;   // system.json

// kept for existing callers, the canonical constant is configFile::TYPE_DEVICE_PACK
const char *const TYPE_DEVICE_PACK = "omote.devicePack";

struct CommandDef {
  std::string name;
  commandHandlers handler = IR;
  std::list<std::string> payloads;
};

/*
  A device pack is what gets shared. The metadata exists for the person at the
  other end, not for the firmware: nothing here changes what the device does,
  but "Samsung UE32EH5300, NEC protocol, by someone, revision 3" is the
  difference between a usable file and forty IR codes with no provenance.

  All of it is optional. A pack written before these fields existed still loads,
  and one written by hand with nothing but an id and its commands is valid.
*/
struct DevicePack {
  std::string id;
  std::string name;
  std::string manufacturer;
  std::string model;

  std::string author;      // whoever captured the codes
  std::string protocol;    // "NEC", "SAMSUNG" - for a human scanning a list
  std::string notes;       // "works on the 2019 models too"
  uint16_t packRevision = 0; // bumped by whoever edits the pack, not by us

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

/*
  /cfg/system.json - everything that is neither a device, a scene nor a screen.

      {
        "schemaVersion": 1,
        "type": "omote.system",
        "deviceName": "omote",
        "display":  { "backlightBrightness": 255, "keyboardBrightness": 255 },
        "sleep":    { "timeoutMs": 20000, "wakeupByIMU": true, "motionThreshold": 50 },
        "mqtt":     { "enabled": true, "broker": "192.168.1.2", "port": 1883,
                      "clientName": "OMOTE" }
      }

  No passwords and no WiFi SSID: those live in NVS (step 10), so this file can
  be exported and shared as it is.

  Every field is optional. A missing field keeps the value compiled into the
  firmware, which is what makes a partial file - say, only the brightness -
  useful and what keeps a half written file from resetting everything.
  defaultSystemConfig() provides those compiled-in values.
*/
struct SystemConfig {
  std::string deviceName = "omote";

  uint8_t backlightBrightness = 255;
  uint8_t keyboardBrightness = 255;

  uint32_t sleepTimeoutMs = 20000;
  bool wakeupByIMU = true;
  uint8_t motionThreshold = 50;

  bool mqttEnabled = true;
  std::string mqttBroker;
  uint16_t mqttPort = 1883;
  std::string mqttClientName = "OMOTE";
};

// the values compiled into the firmware, including those from secrets.h
SystemConfig defaultSystemConfig();

std::string serializeSystemConfig(const SystemConfig &config);
/*
  Parses into config. Fields that the file does not mention are left untouched,
  so the caller decides the fallback by what it passes in - normally
  defaultSystemConfig().
*/
bool parseSystemConfig(const std::string &json, SystemConfig &config, std::string &error);

} // namespace configModel
