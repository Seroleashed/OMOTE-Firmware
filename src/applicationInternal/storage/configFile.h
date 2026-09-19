#pragma once

#include <stdint.h>
#include <string>

#include <ArduinoJson.h>

/*
  What every configuration file has in common.

  The configuration is split into several files instead of one big one, because
  that is what makes partial import and export possible in the first place: a
  single device can be shared without handing over the WiFi settings, and a
  broken scenes file does not take the devices down with it.

      /cfg/system.json          device name, sleep, brightness, MQTT broker
      /cfg/devices/<id>.json    one device with all its commands
      /cfg/scenes.json          scenes, their key maps and start sequences
      /cfg/keys.json            the 5x5 matrix of the keypad
      /cfg/ui.json              screens (defined in step 15, the renderer)

  Credentials are deliberately NOT in these files. WiFi and MQTT passwords live
  in NVS, so an exported configuration can be shared without leaking them.

  Every file starts with the same two fields:

      { "schemaVersion": 1, "type": "omote.system", ... }

  * "type" so a file cannot be imported as the wrong kind. Picking the wrong
    file in the web UI is a mistake worth catching by name.
  * "schemaVersion" so a file written by an older OMOTE can be migrated, and one
    written by a newer OMOTE is rejected with something readable instead of
    being half understood.

  ## Envelope vs. payload

  Do not confuse the fields above with the envelope that configStorage writes
  around the file on the flash. That envelope (magic, length, crc32) is a pure
  storage detail and exists to survive a power loss. It is added when writing
  and stripped when reading.

  Everything that leaves the device carries the payload only: the transport
  protocol (step 11), the USB and BLE transfers (steps 12/13) and the HTTP API
  (step 18) all hand over plain JSON. The storage envelope is rebuilt on the
  device when the file is written. A file pulled off the device is therefore
  valid JSON that any editor can open.
*/

namespace configFile {

// --- where the files live ----------------------------------------------------
extern const char *const DIRECTORY;
extern const char *const DEVICES_DIRECTORY;
extern const char *const PATH_SYSTEM;
extern const char *const PATH_SCENES;
extern const char *const PATH_KEYS;
extern const char *const PATH_UI;

// "/cfg/devices/<deviceId>.json"
std::string pathForDevice(const std::string &deviceId);

// --- what kind of file it is -------------------------------------------------
extern const char *const TYPE_SYSTEM;
extern const char *const TYPE_DEVICE_PACK;
extern const char *const TYPE_SCENES;
extern const char *const TYPE_KEYS;
extern const char *const TYPE_UI;

/*
  Parses the JSON and checks the two common fields.

  On success, version contains the schemaVersion the file was written with -
  which may be older than currentVersion. Handing it to the caller is what makes
  a migration possible: the parser reads the old field names for an old version
  and the new ones for the current one.

  On failure, error holds a sentence meant for the user, not for a log file.
*/
bool parseAndCheckEnvelope(const std::string &json, const char *expectedType, uint16_t currentVersion,
                           JsonDocument &doc, uint16_t &version, std::string &error);

// Writes schemaVersion and type. Always writes the current version - we only
// ever produce files in the format this firmware speaks.
void writeEnvelope(JsonDocument &doc, const char *type, uint16_t version);

} // namespace configFile
