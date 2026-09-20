#include "applicationInternal/storage/configModel.h"

#include <ArduinoJson.h>

#include "applicationInternal/omote_log.h"
#include "applicationInternal/storage/configFile.h"
#include "secrets.h"

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
  configFile::writeEnvelope(doc, configFile::TYPE_DEVICE_PACK, SCHEMA_VERSION);

  JsonObject device = doc["device"].to<JsonObject>();
  device["id"] = pack.id;
  device["name"] = pack.name;
  device["manufacturer"] = pack.manufacturer;
  device["model"] = pack.model;
  // Written only when set. An empty "author": "" in every shared file would be
  // noise, and a reader cannot tell it from "the author is unknown" anyway.
  if (!pack.author.empty()) device["author"] = pack.author;
  if (!pack.protocol.empty()) device["protocol"] = pack.protocol;
  if (!pack.notes.empty()) device["notes"] = pack.notes;
  if (pack.packRevision > 0) device["packRevision"] = pack.packRevision;

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
  uint16_t version = 0;
  if (!configFile::parseAndCheckEnvelope(json, configFile::TYPE_DEVICE_PACK, SCHEMA_VERSION, doc, version,
                                         error)) {
    return false;
  }
  // version is 1, the only one that exists. When a version 2 arrives, this is
  // where the fields of an older file get translated.

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
  // metadata for whoever receives the pack. Missing is normal, not an error:
  // a pack written before these fields existed still has to load.
  pack.author = device["author"].is<const char *>() ? device["author"].as<std::string>() : "";
  pack.protocol = device["protocol"].is<const char *>() ? device["protocol"].as<std::string>() : "";
  pack.notes = device["notes"].is<const char *>() ? device["notes"].as<std::string>() : "";
  pack.packRevision =
      device["packRevision"].is<uint16_t>() ? device["packRevision"].as<uint16_t>() : 0;

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

// --- system configuration ----------------------------------------------------

SystemConfig defaultSystemConfig() {
  SystemConfig config;
  // secrets.h is and stays the compile time default. The broker address is not
  // a secret, so it belongs in the exportable file; user and password do not
  // and stay in NVS (step 10).
  config.mqttBroker = MQTT_SERVER;
  config.mqttPort = MQTT_SERVER_PORT;
  config.mqttClientName = MQTT_CLIENTNAME;
#if (ENABLE_WIFI_AND_MQTT == 1)
  config.mqttEnabled = true;
#else
  config.mqttEnabled = false;
#endif
  return config;
}

std::string serializeSystemConfig(const SystemConfig &config) {
  JsonDocument doc;
  configFile::writeEnvelope(doc, configFile::TYPE_SYSTEM, SYSTEM_SCHEMA_VERSION);

  doc["deviceName"] = config.deviceName;

  JsonObject display = doc["display"].to<JsonObject>();
  display["backlightBrightness"] = config.backlightBrightness;
  display["keyboardBrightness"] = config.keyboardBrightness;

  JsonObject sleep = doc["sleep"].to<JsonObject>();
  sleep["timeoutMs"] = config.sleepTimeoutMs;
  sleep["wakeupByIMU"] = config.wakeupByIMU;
  sleep["motionThreshold"] = config.motionThreshold;

  JsonObject mqtt = doc["mqtt"].to<JsonObject>();
  mqtt["enabled"] = config.mqttEnabled;
  mqtt["broker"] = config.mqttBroker;
  mqtt["port"] = config.mqttPort;
  mqtt["clientName"] = config.mqttClientName;
  // no user, no password: those are in NVS and must not end up in an export

  std::string result;
  serializeJsonPretty(doc, result);
  return result;
}

/*
  Reads one value, but only if the file actually has it and it has the right
  type. Anything else leaves the existing value alone - a typo in one field must
  not silently reset the device to a default.
*/
template <typename T> static void readIfPresent(JsonVariantConst source, T &target) {
  if (!source.isNull() && source.template is<T>()) target = source.template as<T>();
}

static void readStringIfPresent(JsonVariantConst source, std::string &target) {
  if (source.is<const char *>()) target = source.as<std::string>();
}

bool parseSystemConfig(const std::string &json, SystemConfig &config, std::string &error) {
  JsonDocument doc;
  uint16_t version = 0;
  if (!configFile::parseAndCheckEnvelope(json, configFile::TYPE_SYSTEM, SYSTEM_SCHEMA_VERSION, doc, version,
                                         error)) {
    return false;
  }
  // version is 1, the only one that exists. When a version 2 arrives, this is
  // where the fields of an older file get translated.

  readStringIfPresent(doc["deviceName"], config.deviceName);

  JsonVariantConst display = doc["display"];
  readIfPresent(display["backlightBrightness"], config.backlightBrightness);
  readIfPresent(display["keyboardBrightness"], config.keyboardBrightness);

  JsonVariantConst sleep = doc["sleep"];
  readIfPresent(sleep["timeoutMs"], config.sleepTimeoutMs);
  readIfPresent(sleep["wakeupByIMU"], config.wakeupByIMU);
  readIfPresent(sleep["motionThreshold"], config.motionThreshold);

  JsonVariantConst mqtt = doc["mqtt"];
  readIfPresent(mqtt["enabled"], config.mqttEnabled);
  readStringIfPresent(mqtt["broker"], config.mqttBroker);
  readIfPresent(mqtt["port"], config.mqttPort);
  readStringIfPresent(mqtt["clientName"], config.mqttClientName);

  // An empty device name would leave mDNS (step 17) without a hostname and the
  // web UI without a title, so it is worth refusing rather than repairing.
  if (config.deviceName.empty()) {
    error = "deviceName must not be empty";
    return false;
  }

  return true;
}

} // namespace configModel
