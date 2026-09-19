#include "applicationInternal/credentials.h"

#include "applicationInternal/omote_log.h"
#include "secrets.h"

namespace credentials {

namespace {

CredentialStorage *storage = NULL;
uint8_t failedConnectCount = 0;

// NVS keys. Short on purpose: the ESP32 NVS limit is 15 characters.
const char *const KEY_WIFI_SSID = "wifiSsid";
const char *const KEY_WIFI_PASSWORD = "wifiPass";
const char *const KEY_MQTT_USER = "mqttUser";
const char *const KEY_MQTT_PASSWORD = "mqttPass";

/*
  secrets.h ships with placeholders. Treating them as real credentials would
  send a brand new remote off trying to join a network called "YourWifiSSID",
  failing three times, and only then offering its access point - with three
  pointless minutes in between.
*/
bool isPlaceholder(const std::string &value) {
  return value == "YourWifiSSID" || value == "YourWifiPassword" ||
         value == "IPAddressOfYourBroker" || value.empty();
}

std::string readStored(const char *key) {
  if (storage == NULL) return "";
  std::string value;
  if (!storage->read(key, value)) return "";
  return value;
}

bool hasStored(const char *key) { return storage != NULL && storage->exists(key); }

} // namespace

void begin(CredentialStorage *aStorage) {
  storage = aStorage;
  failedConnectCount = 0;
}

// --- WiFi --------------------------------------------------------------------

Source wifiSource() {
  if (!readStored(KEY_WIFI_SSID).empty()) return Source::Stored;
  if (!isPlaceholder(WIFI_SSID)) return Source::CompiledIn;
  return Source::None;
}

std::string wifiSsid() {
  switch (wifiSource()) {
    case Source::Stored: return readStored(KEY_WIFI_SSID);
    case Source::CompiledIn: return WIFI_SSID;
    case Source::None: return "";
  }
  return "";
}

bool hasWifi() { return wifiSource() != Source::None; }

bool hasWifiPassword() {
  switch (wifiSource()) {
    // an open network is legitimate, so "stored but empty" is a real answer
    case Source::Stored: return !readStored(KEY_WIFI_PASSWORD).empty();
    case Source::CompiledIn: return !isPlaceholder(WIFI_PASSWORD);
    case Source::None: return false;
  }
  return false;
}

std::string wifiPasswordForConnecting() {
  switch (wifiSource()) {
    case Source::Stored: return readStored(KEY_WIFI_PASSWORD);
    case Source::CompiledIn: return isPlaceholder(WIFI_PASSWORD) ? "" : WIFI_PASSWORD;
    case Source::None: return "";
  }
  return "";
}

bool setWifi(const std::string &ssid, const std::string &password) {
  if (storage == NULL) return false;
  if (ssid.empty()) {
    omote_log_e("credentials: refusing to store an empty WiFi name\r\n");
    return false;
  }
  if (!storage->write(KEY_WIFI_SSID, ssid)) return false;
  if (!storage->write(KEY_WIFI_PASSWORD, password)) return false;

  // new credentials deserve a fresh set of attempts
  failedConnectCount = 0;
  omote_log_i("credentials: WiFi set to '%s'\r\n", ssid.c_str());
  return true;
}

bool clearWifi() {
  if (storage == NULL) return false;
  storage->remove(KEY_WIFI_SSID);
  storage->remove(KEY_WIFI_PASSWORD);
  failedConnectCount = 0;
  omote_log_i("credentials: stored WiFi removed, falling back to %s\r\n",
              wifiSource() == Source::CompiledIn ? "secrets.h" : "nothing");
  return true;
}

// --- MQTT --------------------------------------------------------------------

Source mqttSource() {
  if (hasStored(KEY_MQTT_USER)) return Source::Stored;
  if (std::string(MQTT_USER).empty() && std::string(MQTT_PASS).empty()) return Source::None;
  return Source::CompiledIn;
}

std::string mqttUser() {
  switch (mqttSource()) {
    case Source::Stored: return readStored(KEY_MQTT_USER);
    case Source::CompiledIn: return MQTT_USER;
    case Source::None: return "";
  }
  return "";
}

bool hasMqttPassword() {
  switch (mqttSource()) {
    case Source::Stored: return !readStored(KEY_MQTT_PASSWORD).empty();
    case Source::CompiledIn: return !std::string(MQTT_PASS).empty();
    case Source::None: return false;
  }
  return false;
}

std::string mqttPasswordForConnecting() {
  switch (mqttSource()) {
    case Source::Stored: return readStored(KEY_MQTT_PASSWORD);
    case Source::CompiledIn: return MQTT_PASS;
    case Source::None: return "";
  }
  return "";
}

bool setMqtt(const std::string &user, const std::string &password) {
  if (storage == NULL) return false;
  // an empty user with a password is a broker configuration nobody uses, but an
  // empty *both* is how you say "this broker needs no login"
  if (!storage->write(KEY_MQTT_USER, user)) return false;
  if (!storage->write(KEY_MQTT_PASSWORD, password)) return false;
  omote_log_i("credentials: MQTT login set\r\n");
  return true;
}

bool clearMqtt() {
  if (storage == NULL) return false;
  storage->remove(KEY_MQTT_USER);
  storage->remove(KEY_MQTT_PASSWORD);
  omote_log_i("credentials: stored MQTT login removed\r\n");
  return true;
}

// --- access point fallback ---------------------------------------------------

void reportConnectFailed() {
  if (failedConnectCount < 255) failedConnectCount++;
  omote_log_w("credentials: WiFi connect failed (%u of %u before the access point)\r\n",
              (unsigned)failedConnectCount, (unsigned)FAILED_CONNECTS_UNTIL_ACCESS_POINT);
}

void reportConnected() { failedConnectCount = 0; }

uint8_t failedConnects() { return failedConnectCount; }

bool shouldStartAccessPoint() {
  // nothing to try at all - no point in counting to three first
  if (!hasWifi()) return true;
  return failedConnectCount >= FAILED_CONNECTS_UNTIL_ACCESS_POINT;
}

std::string statusText() {
  switch (wifiSource()) {
    case Source::Stored: return "WiFi '" + wifiSsid() + "' (stored)";
    case Source::CompiledIn: return "WiFi '" + wifiSsid() + "' (from secrets.h)";
    case Source::None: return "no WiFi configured";
  }
  return "no WiFi configured";
}

} // namespace credentials
