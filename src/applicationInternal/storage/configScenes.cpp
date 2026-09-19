#include "applicationInternal/storage/configScenes.h"

#include <ArduinoJson.h>

#include "applicationInternal/keyNames.h"
#include "applicationInternal/omote_log.h"
#include "applicationInternal/storage/configFile.h"

namespace configModel {

// --- repeat modes ------------------------------------------------------------

std::string repeatModeToString(repeatModes mode) {
  switch (mode) {
    case SHORT: return "SHORT";
    case SHORT_REPEATED: return "SHORT_REPEATED";
    case SHORTorLONG: return "SHORTorLONG";
    case REPEAT_MODE_UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

bool repeatModeFromString(const std::string &text, repeatModes &mode) {
  if (text == "SHORT") { mode = SHORT; return true; }
  if (text == "SHORT_REPEATED") { mode = SHORT_REPEATED; return true; }
  if (text == "SHORTorLONG") { mode = SHORTorLONG; return true; }
  return false;
}

// --- scenes: serialize -------------------------------------------------------

static void writeSequence(JsonArray target, const std::vector<SequenceStep> &steps) {
  for (size_t i = 0; i < steps.size(); i++) {
    JsonObject step = target.add<JsonObject>();
    step["command"] = steps[i].commandName;
    if (!steps[i].payload.empty()) step["payload"] = steps[i].payload;
    step["delayAfterMs"] = steps[i].delayAfterMs;
  }
}

std::string serializeScenes(const ScenesConfig &config) {
  JsonDocument doc;
  configFile::writeEnvelope(doc, configFile::TYPE_SCENES, SCENES_SCHEMA_VERSION);

  JsonArray scenes = doc["scenes"].to<JsonArray>();
  for (size_t s = 0; s < config.scenes.size(); s++) {
    const SceneConfig &scene = config.scenes[s];
    JsonObject entry = scenes.add<JsonObject>();
    entry["name"] = scene.name;
    if (!scene.activateCommand.empty()) entry["activateCommand"] = scene.activateCommand;

    JsonArray guiList = entry["guiList"].to<JsonArray>();
    for (size_t i = 0; i < scene.guiList.size(); i++) guiList.add(scene.guiList[i]);

    JsonObject keys = entry["keys"].to<JsonObject>();
    for (size_t i = 0; i < scene.keys.size(); i++) {
      const SceneKeyBinding &binding = scene.keys[i];
      JsonObject key = keys[binding.keyName].to<JsonObject>();
      key["repeatMode"] = repeatModeToString(binding.repeatMode);
      if (!binding.commandShort.empty()) key["short"] = binding.commandShort;
      if (!binding.commandLong.empty()) key["long"] = binding.commandLong;
    }

    writeSequence(entry["startSequence"].to<JsonArray>(), scene.startSequence);
    writeSequence(entry["endSequence"].to<JsonArray>(), scene.endSequence);
  }

  std::string result;
  serializeJsonPretty(doc, result);
  return result;
}

// --- scenes: parse -----------------------------------------------------------

static bool readSequence(JsonArrayConst source, const std::string &sceneName, const char *which,
                         std::vector<SequenceStep> &target, std::string &error) {
  if (source.isNull()) return true; // a scene without a sequence is fine

  for (JsonObjectConst entry : source) {
    SequenceStep step;
    if (!entry["command"].is<const char *>() ||
        std::string(entry["command"].as<const char *>()).empty()) {
      error = "scene '" + sceneName + "': a step of the " + which + " has no command";
      return false;
    }
    step.commandName = entry["command"].as<std::string>();
    if (entry["payload"].is<const char *>()) step.payload = entry["payload"].as<std::string>();
    if (entry["delayAfterMs"].is<uint32_t>()) step.delayAfterMs = entry["delayAfterMs"].as<uint32_t>();
    target.push_back(step);
  }
  return true;
}

bool parseScenes(const std::string &json, ScenesConfig &config, std::string &error) {
  config = ScenesConfig();

  JsonDocument doc;
  uint16_t version = 0;
  if (!configFile::parseAndCheckEnvelope(json, configFile::TYPE_SCENES, SCENES_SCHEMA_VERSION, doc, version,
                                         error)) {
    return false;
  }

  JsonArrayConst scenes = doc["scenes"];
  if (scenes.isNull()) {
    error = "scenes array is missing";
    return false;
  }

  for (JsonObjectConst entry : scenes) {
    SceneConfig scene;

    if (!entry["name"].is<const char *>() || std::string(entry["name"].as<const char *>()).empty()) {
      error = "a scene without a name";
      return false;
    }
    scene.name = entry["name"].as<std::string>();

    // Two scenes of the same name would make the lookup in sceneRegistry depend
    // on insertion order - a bug that only shows up on the device.
    for (size_t i = 0; i < config.scenes.size(); i++) {
      if (config.scenes[i].name == scene.name) {
        error = "scene '" + scene.name + "' appears twice";
        return false;
      }
    }

    if (entry["activateCommand"].is<const char *>()) {
      scene.activateCommand = entry["activateCommand"].as<std::string>();
    }

    JsonArrayConst guiList = entry["guiList"];
    if (!guiList.isNull()) {
      for (JsonVariantConst gui : guiList) {
        if (gui.is<const char *>()) scene.guiList.push_back(gui.as<std::string>());
      }
    }

    JsonObjectConst keys = entry["keys"];
    if (!keys.isNull()) {
      for (JsonPairConst pair : keys) {
        SceneKeyBinding binding;
        binding.keyName = pair.key().c_str();

        char unused = 0;
        if (!keyNames::charFromName(binding.keyName, unused)) {
          error = "scene '" + scene.name + "': '" + binding.keyName + "' is not a key of this remote";
          return false;
        }

        JsonObjectConst value = pair.value();
        std::string modeText =
            value["repeatMode"].is<const char *>() ? value["repeatMode"].as<std::string>() : "SHORT";
        if (!repeatModeFromString(modeText, binding.repeatMode)) {
          error = "scene '" + scene.name + "', key '" + binding.keyName + "': unknown repeatMode '" +
                  modeText + "'";
          return false;
        }

        if (value["short"].is<const char *>()) binding.commandShort = value["short"].as<std::string>();
        if (value["long"].is<const char *>()) binding.commandLong = value["long"].as<std::string>();

        // A long press command that can never fire is a configuration mistake
        // worth naming, not something to silently drop.
        if (!binding.commandLong.empty() && binding.repeatMode != SHORTorLONG) {
          error = "scene '" + scene.name + "', key '" + binding.keyName +
                  "': a long press command needs repeatMode SHORTorLONG";
          return false;
        }

        scene.keys.push_back(binding);
      }
    }

    if (!readSequence(entry["startSequence"], scene.name, "start sequence", scene.startSequence, error)) {
      return false;
    }
    if (!readSequence(entry["endSequence"], scene.name, "end sequence", scene.endSequence, error)) {
      return false;
    }

    config.scenes.push_back(scene);
  }

  omote_log_i("configScenes: read %u scenes\r\n", (unsigned)config.scenes.size());
  return true;
}

// --- keypad matrix -----------------------------------------------------------

uint8_t thisHardwareRevision() {
#ifdef OMOTE_HARDWARE_REV
  return OMOTE_HARDWARE_REV;
#else
  // the simulator: no keypad matrix, nothing to compare against
  return 0;
#endif
}

std::string serializeKeys(const KeysConfig &config) {
  JsonDocument doc;
  configFile::writeEnvelope(doc, configFile::TYPE_KEYS, KEYS_SCHEMA_VERSION);

  doc["hardwareRev"] = config.hardwareRev;

  JsonArray matrix = doc["matrix"].to<JsonArray>();
  for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
    JsonArray columns = matrix.add<JsonArray>();
    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
      columns.add(config.matrix[row][col]);
    }
  }

  std::string result;
  serializeJsonPretty(doc, result);
  return result;
}

bool parseKeys(const std::string &json, KeysConfig &config, bool &revisionMismatch, std::string &error) {
  config = KeysConfig();
  revisionMismatch = false;

  JsonDocument doc;
  uint16_t version = 0;
  if (!configFile::parseAndCheckEnvelope(json, configFile::TYPE_KEYS, KEYS_SCHEMA_VERSION, doc, version,
                                         error)) {
    return false;
  }

  if (doc["hardwareRev"].is<uint8_t>()) config.hardwareRev = doc["hardwareRev"].as<uint8_t>();

  JsonArrayConst matrix = doc["matrix"];
  if (matrix.isNull()) {
    error = "matrix is missing";
    return false;
  }
  if (matrix.size() != MATRIX_ROWS) {
    error = "matrix has " + std::to_string(matrix.size()) + " rows, expected " +
            std::to_string(MATRIX_ROWS);
    return false;
  }

  uint8_t row = 0;
  for (JsonArrayConst columns : matrix) {
    if (columns.isNull() || columns.size() != MATRIX_COLS) {
      error = "row " + std::to_string(row) + " has " + std::to_string(columns.size()) +
              " columns, expected " + std::to_string(MATRIX_COLS);
      return false;
    }

    uint8_t col = 0;
    for (JsonVariantConst cell : columns) {
      std::string keyName = cell.is<const char *>() ? cell.as<std::string>() : "";
      if (!keyName.empty()) {
        char unused = 0;
        if (!keyNames::charFromName(keyName, unused)) {
          error = "row " + std::to_string(row) + ", column " + std::to_string(col) + ": '" + keyName +
                  "' is not a key of this remote";
          return false;
        }
        // The same key on two positions is never intentional and is horrible to
        // track down once the firmware is on the device.
        for (uint8_t r = 0; r <= row; r++) {
          for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if (r == row && c >= col) break;
            if (config.matrix[r][c] == keyName) {
              error = "'" + keyName + "' is used twice, at row " + std::to_string(r) + " column " +
                      std::to_string(c) + " and row " + std::to_string(row) + " column " +
                      std::to_string(col);
              return false;
            }
          }
        }
      }
      config.matrix[row][col] = keyName;
      col++;
    }
    row++;
  }

  // Rev5 and Rev1-4 carry the same keys in reversed row order. Applying a file
  // from the other revision would mirror the keypad top to bottom, so say so
  // and let the caller decide.
  if (config.hardwareRev != 0 && thisHardwareRevision() != 0 &&
      config.hardwareRev != thisHardwareRevision()) {
    revisionMismatch = true;
    omote_log_w("configScenes: keys.json was written for hardware revision %u, this is revision %u\r\n",
                (unsigned)config.hardwareRev, (unsigned)thisHardwareRevision());
  }

  return true;
}

} // namespace configModel
