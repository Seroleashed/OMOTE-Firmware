#include "applicationInternal/storage/configModel.h"

#include <ArduinoJson.h>

#include "applicationInternal/omote_log.h"

namespace configModel {

// --- handler names -----------------------------------------------------------
std::string handlerToString(commandHandlers handler) {
  switch (handler) {
    case IR: return "IR";
    case SCENE: return "SCENE";
    case GUI: return "GUI";
    case SPECIAL: return "SPECIAL";
#if (ENABLE_WIFI_AND_MQTT == 1)
    case MQTT: return "MQTT";
#endif
#if (ENABLE_KEYBOARD_BLE == 1)
    case BLE_KEYBOARD: return "BLE_KEYBOARD";
#endif
  }
  return "";
}

bool handlerFromString(const std::string &text, commandHandlers &handler) {
  if (text == "IR") { handler = IR; return true; }
  if (text == "SCENE") { handler = SCENE; return true; }
  if (text == "GUI") { handler = GUI; return true; }
  if (text == "SPECIAL") { handler = SPECIAL; return true; }
#if (ENABLE_WIFI_AND_MQTT == 1)
  if (text == "MQTT") { handler = MQTT; return true; }
#endif
#if (ENABLE_KEYBOARD_BLE == 1)
  if (text == "BLE_KEYBOARD") { handler = BLE_KEYBOARD; return true; }
#endif
  return false;
}

// --- serialize ---------------------------------------------------------------
std::string serializeDevicePack(const DevicePack &pack) {
  JsonDocument doc;
  doc["schemaVersion"] = SCHEMA_VERSION;
  doc["type"] = TYPE_DEVICE_PACK;

  JsonObject device = doc["device"].to<JsonObject>();
  device["id"] = pack.id;
  device["name"] = pack.name;
  device["manufacturer"] = pack.manufacturer;
  device["model"] = pack.model;

  JsonArray commands = doc["commands"].to<JsonArray>();
  for (const CommandDef &command : pack.commands) {
    JsonObject entry = commands.add<JsonObject>();
    entry["name"] = command.name;
    entry["handler"] = handlerToString(command.handler);
    JsonArray payloads = entry["payloads"].to<JsonArray>();
    for (const std::string &payload : command.payloads) {
      payloads.add(payload);
    }
  }

  std::string result;
  serializeJsonPretty(doc, result);
  return result;
}

// --- parse -------------------------------------------------------------------
bool parseDevicePack(const std::string &json, DevicePack &pack, std::string &error) {
  error.clear();
  pack = DevicePack();

  JsonDocument doc;
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
  uint16_t version = doc["schemaVersion"].as<uint16_t>();
  if (version > SCHEMA_VERSION) {
    error = "file was written by a newer version of OMOTE (schemaVersion " +
            std::to_string(version) + ")";
    return false;
  }

  if (doc["type"].as<std::string>() != TYPE_DEVICE_PACK) {
    error = "this is not a device pack";
    return false;
  }

  JsonObjectConst device = doc["device"];
  if (device.isNull() || !device["id"].is<const char *>() ||
      std::string(device["id"].as<const char *>()).empty()) {
    error = "device.id is missing";
    return false;
  }
  pack.id = device["id"].as<std::string>();
  pack.name = device["name"].is<const char *>() ? device["name"].as<std::string>() : "";
  // an empty or missing display name falls back to the id, so the web UI
  // always has something to show
  if (pack.name.empty()) pack.name = pack.id;
  pack.manufacturer =
      device["manufacturer"].is<const char *>() ? device["manufacturer"].as<std::string>() : "";
  pack.model = device["model"].is<const char *>() ? device["model"].as<std::string>() : "";

  JsonArrayConst commands = doc["commands"];
  if (commands.isNull()) {
    error = "commands array is missing";
    return false;
  }

  for (JsonObjectConst entry : commands) {
    CommandDef command;

    if (!entry["name"].is<const char *>() || std::string(entry["name"].as<const char *>()).empty()) {
      error = "a command without a name";
      return false;
    }
    command.name = entry["name"].as<std::string>();

    std::string handlerText =
        entry["handler"].is<const char *>() ? entry["handler"].as<std::string>() : "";
    if (!handlerFromString(handlerText, command.handler)) {
      error = "command '" + command.name + "' uses unknown handler '" + handlerText + "'";
      return false;
    }

    JsonArrayConst payloads = entry["payloads"];
    if (payloads.isNull()) {
      error = "command '" + command.name + "' has no payloads";
      return false;
    }
    for (JsonVariantConst payload : payloads) {
      if (!payload.is<const char *>()) {
        error = "command '" + command.name + "' has a payload that is not a string";
        return false;
      }
      command.payloads.push_back(payload.as<std::string>());
    }

    pack.commands.push_back(command);
  }

  return true;
}

// --- export of the configuration compiled into the firmware -------------------
DevicePack devicePackFromRegisteredCommands(const std::string &deviceId, const std::string &deviceName,
                                            const std::string &namePrefix) {
  DevicePack pack;
  pack.id = deviceId;
  pack.name = deviceName.empty() ? deviceId : deviceName;

  const std::map<uint16_t, std::string> &names = get_all_commandNames();
  for (std::map<uint16_t, std::string>::const_iterator it = names.begin(); it != names.end(); ++it) {
    if (!namePrefix.empty() && it->second.rfind(namePrefix, 0) != 0) continue;

    commandData data;
    if (!get_commandData_byID(it->first, data)) continue;
    if (handlerToString(data.commandHandler).empty()) continue;

    CommandDef command;
    command.name = it->second;
    command.handler = data.commandHandler;
    command.payloads = data.commandPayloads;
    pack.commands.push_back(command);
  }

  omote_log_i("configModel: exported %u commands for device '%s'\r\n",
              (unsigned)pack.commands.size(), pack.id.c_str());
  return pack;
}

} // namespace configModel
