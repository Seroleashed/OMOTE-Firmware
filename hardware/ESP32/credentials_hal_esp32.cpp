#include <Preferences.h>

#include "applicationInternal/credentials.h"

/*
  Credentials in NVS.

  Own namespace, like the boot counter: these must survive "reset the settings
  to default", and a configuration export has no business anywhere near them.

  NVS is stored in the nvs partition, not in littlefs - so an export of the
  configuration files cannot pick them up even by accident, which is the whole
  reason they live here.
*/

static const char *const NAMESPACE = "credentials";

class NvsCredentialStorage : public CredentialStorage {
public:
  bool read(const std::string &key, std::string &value) override {
    Preferences preferences;
    if (!preferences.begin(NAMESPACE, true)) return false; // namespace does not exist yet
    if (!preferences.isKey(key.c_str())) {
      preferences.end();
      return false;
    }
    value = std::string(preferences.getString(key.c_str(), "").c_str());
    preferences.end();
    return true;
  }

  bool write(const std::string &key, const std::string &value) override {
    Preferences preferences;
    if (!preferences.begin(NAMESPACE, false)) return false;
    size_t written = preferences.putString(key.c_str(), value.c_str());
    preferences.end();
    // an empty value writes 0 bytes and is a legitimate password for an open
    // network, so only a shorter-than-asked write is a failure
    return written == value.size();
  }

  bool remove(const std::string &key) override {
    Preferences preferences;
    if (!preferences.begin(NAMESPACE, false)) return false;
    bool ok = preferences.remove(key.c_str());
    preferences.end();
    return ok;
  }

  bool exists(const std::string &key) override {
    Preferences preferences;
    if (!preferences.begin(NAMESPACE, true)) return false;
    bool found = preferences.isKey(key.c_str());
    preferences.end();
    return found;
  }
};

CredentialStorage *get_credentialStorage() {
  static NvsCredentialStorage storage;
  return &storage;
}
