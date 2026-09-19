#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "applicationInternal/keys.h"

/*
  /cfg/scenes.json and /cfg/keys.json.

  Part of configModel (same namespace), in its own file because scenes and the
  keypad matrix together are as much code again as the devices.

  ## scenes.json

      {
        "schemaVersion": 1,
        "type": "omote.scenes",
        "scenes": [{
          "name": "TV",
          "activateCommand": "SCENE_TV",
          "guiList": ["Numpad"],
          "keys": {
            "KEY_UP": { "repeatMode": "SHORT_REPEATED", "short": "SAMSUNG_UP" },
            "KEY_OK": { "repeatMode": "SHORT", "short": "SAMSUNG_SELECT",
                        "long": "SAMSUNG_MENU" }
          },
          "startSequence": [
            { "command": "SAMSUNG_POWER_ON", "delayAfterMs": 500 },
            { "command": "YAMAHA_INPUT_DVD", "delayAfterMs": 3000 }
          ],
          "endSequence": []
        }]
      }

  Commands and keys are referenced by name, never by id - see commandHandler.h
  and keyNames.h for why.

  The sequence is already the data structure the engine of step 9 will run:
  a list of {command, payload, delayAfter}. Today the scenes execute this
  hard coded with delay(); the format is defined here so the file does not have
  to change again when the engine arrives.

  ## keys.json

      {
        "schemaVersion": 1,
        "type": "omote.keys",
        "hardwareRev": 5,
        "matrix": [
          ["", "KEY_PLAY", "KEY_CONF", "KEY_REWI", "KEY_STOP"],
          ...
        ]
      }

  Five rows of five columns, "" for a position without a key. Careful with
  hardwareRev: Rev5 and Rev1-4 have the same keys but in *reversed row order*
  (see keypad_keys_hal_esp32.cpp). A file from one revision would mirror the
  keypad on the other, so the revision travels with the file and a mismatch is
  reported instead of quietly applied.
*/

namespace configModel {

const uint16_t SCENES_SCHEMA_VERSION = 1;
const uint16_t KEYS_SCHEMA_VERSION = 1;

// --- scenes ------------------------------------------------------------------

struct SequenceStep {
  std::string commandName;
  std::string payload;       // optional, passed as additionalPayload
  uint32_t delayAfterMs = 0; // wait before the next step
};

struct SceneKeyBinding {
  std::string keyName; // "KEY_OK"
  repeatModes repeatMode = SHORT;
  std::string commandShort;
  std::string commandLong; // empty unless repeatMode is SHORTorLONG
};

struct SceneConfig {
  std::string name;
  std::string activateCommand;
  std::vector<std::string> guiList;
  std::vector<SceneKeyBinding> keys;
  std::vector<SequenceStep> startSequence;
  std::vector<SequenceStep> endSequence;
};

struct ScenesConfig {
  std::vector<SceneConfig> scenes;
};

// enum <-> string, so the file does not depend on the order of the enum
std::string repeatModeToString(repeatModes mode);
bool repeatModeFromString(const std::string &text, repeatModes &mode);

std::string serializeScenes(const ScenesConfig &config);
bool parseScenes(const std::string &json, ScenesConfig &config, std::string &error);

// --- keypad matrix -----------------------------------------------------------

const uint8_t MATRIX_ROWS = 5;
const uint8_t MATRIX_COLS = 5;

struct KeysConfig {
  uint8_t hardwareRev = 0; // 0 means "not stated", accepted on any revision
  // [row][col], empty string for a position without a key
  std::string matrix[MATRIX_ROWS][MATRIX_COLS];
};

/*
  The revision this firmware was built for, or 0 if it does not have one.

  The simulator is the 0 case: linux_64bit deliberately leaves
  OMOTE_HARDWARE_REV undefined, every "#if (OMOTE_HARDWARE_REV >= 5)" in the
  code therefore takes the Rev1-4 branch, and its keypad comes from an image map
  rather than a matrix. It has no revision to compare against, so it never
  reports a mismatch.
*/
uint8_t thisHardwareRevision();

std::string serializeKeys(const KeysConfig &config);
/*
  Rejects an unknown key name, a matrix of the wrong shape and a key that
  appears twice - two positions sending the same key is never intentional and
  would be maddening to debug on the device.

  A hardwareRev that does not match this build is reported through
  revisionMismatch instead of failing: the caller decides whether to refuse the
  import or to offer flipping the rows.
*/
bool parseKeys(const std::string &json, KeysConfig &config, bool &revisionMismatch, std::string &error);

} // namespace configModel
