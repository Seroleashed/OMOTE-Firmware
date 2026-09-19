#pragma once

#include <stdint.h>

#include <string>

/*
  WiFi and MQTT credentials.

  They are the one part of the configuration that must not end up in the JSON
  files: those get exported, shared in forum threads and committed to git. So
  credentials live in NVS instead, and system.json carries only the broker
  address - see configFile.h.

  ## Where a value comes from

  secrets.h stays the compile time default and is never taken away: somebody who
  has always flashed their WiFi into the firmware keeps doing exactly that. On
  top of it, NVS wins:

      NVS  ->  secrets.h  ->  nothing (access point)

  "Nothing" also covers the case where secrets.h still holds the placeholder it
  ships with. A fresh OMOTE would otherwise spend forever trying to connect to a
  network called "YourWifiSSID".

  ## Passwords

  A password can be set and cleared, but the functions that hand one out are
  named passwordForConnecting() on purpose. When one of those turns up in the
  web API of step 18, the name is there to make the reviewer stop. Everything
  the UI needs is hasWifiPassword() - "set" or "not set".
*/

class CredentialStorage {
public:
  virtual ~CredentialStorage() {}

  virtual bool read(const std::string &key, std::string &value) = 0;
  virtual bool write(const std::string &key, const std::string &value) = 0;
  virtual bool remove(const std::string &key) = 0;
  virtual bool exists(const std::string &key) = 0;
};

// provided by the active hardware layer
CredentialStorage *get_credentialStorage();

namespace credentials {

enum class Source {
  Stored,     // NVS: set through the UI or the API
  CompiledIn, // secrets.h
  None,       // nothing usable anywhere
};

// Call once at startup.
void begin(CredentialStorage *storage);

// --- WiFi --------------------------------------------------------------------
std::string wifiSsid();
Source wifiSource();
bool hasWifi(); // is there anything at all to connect with?
bool hasWifiPassword();
// Hands out the actual password. See the note above before calling this.
std::string wifiPasswordForConnecting();

bool setWifi(const std::string &ssid, const std::string &password);
bool clearWifi(); // falls back to secrets.h, or to nothing

// --- MQTT --------------------------------------------------------------------
std::string mqttUser();
Source mqttSource();
bool hasMqttPassword();
std::string mqttPasswordForConnecting();

bool setMqtt(const std::string &user, const std::string &password);
bool clearMqtt();

// --- access point fallback ---------------------------------------------------
/*
  Without credentials there is nothing to connect to, and with wrong ones there
  never will be. Either way the device has to become reachable somehow, or the
  only way to fix a mistyped password is a USB cable.

  Three attempts, not one: a router rebooting, or a remote switched on before
  the access point is up, must not throw the user into a configuration mode
  they did not ask for.
*/
const uint8_t FAILED_CONNECTS_UNTIL_ACCESS_POINT = 3;

void reportConnectFailed();
void reportConnected(); // clears the counter
bool shouldStartAccessPoint();
uint8_t failedConnects();

// text for the log and the display, never contains a password
std::string statusText();

} // namespace credentials
