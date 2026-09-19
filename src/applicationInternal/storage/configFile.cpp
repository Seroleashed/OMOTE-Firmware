#include "applicationInternal/storage/configFile.h"

namespace configFile {

const char *const DIRECTORY = "/cfg";
const char *const DEVICES_DIRECTORY = "/cfg/devices";
const char *const PATH_SYSTEM = "/cfg/system.json";
const char *const PATH_SCENES = "/cfg/scenes.json";
const char *const PATH_KEYS = "/cfg/keys.json";
const char *const PATH_UI = "/cfg/ui.json";

const char *const TYPE_SYSTEM = "omote.system";
const char *const TYPE_DEVICE_PACK = "omote.devicePack";
const char *const TYPE_SCENES = "omote.scenes";
const char *const TYPE_KEYS = "omote.keys";
const char *const TYPE_UI = "omote.ui";

std::string pathForDevice(const std::string &deviceId) {
  return std::string(DEVICES_DIRECTORY) + "/" + deviceId + ".json";
}

bool parseAndCheckEnvelope(const std::string &json, const char *expectedType, uint16_t currentVersion,
                           JsonDocument &doc, uint16_t &version, std::string &error) {
  error.clear();
  version = 0;

  DeserializationError jsonError = deserializeJson(doc, json);
  if (jsonError) {
    error = std::string("not valid JSON: ") + jsonError.c_str();
    return false;
  }
  if (!doc.is<JsonObject>()) {
    error = "expected a JSON object at the top level";
    return false;
  }

  if (!doc["schemaVersion"].is<uint16_t>()) {
    error = "schemaVersion is missing";
    return false;
  }
  version = doc["schemaVersion"].as<uint16_t>();
  if (version == 0) {
    error = "schemaVersion 0 is not a valid version";
    return false;
  }
  if (version > currentVersion) {
    error = "file was written by a newer version of OMOTE (schemaVersion " + std::to_string(version) +
            ", this firmware understands up to " + std::to_string(currentVersion) + ")";
    return false;
  }

  std::string type = doc["type"].is<const char *>() ? doc["type"].as<std::string>() : "";
  if (type != expectedType) {
    if (type.empty()) {
      error = std::string("type is missing, expected '") + expectedType + "'";
    } else {
      error = "this is a '" + type + "' file, expected '" + expectedType + "'";
    }
    return false;
  }

  return true;
}

void writeEnvelope(JsonDocument &doc, const char *type, uint16_t version) {
  doc["schemaVersion"] = version;
  doc["type"] = type;
}

} // namespace configFile
