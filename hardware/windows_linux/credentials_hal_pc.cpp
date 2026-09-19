#if defined(WIN32) || defined(__linux__) || defined(__APPLE__)

#include <filesystem>
#include <fstream>
#include <map>

#include "applicationInternal/credentials.h"

/*
  Credentials for the simulator.

  Kept deliberately separate from omote_data/cfg/, in a file whose name says
  what it is: the point of the whole exercise is that a configuration export
  cannot pick up a password, and that separation should be visible on the
  development machine too.

  Plain text, because this is a simulator on a development machine and
  pretending otherwise would be security theatre. The file is git-ignored along
  with the rest of omote_data/.
*/

static const char *const SIM_ROOT = "omote_data";
static const char *const CREDENTIALS_FILE = "omote_data/credentials.txt";

class FileCredentialStorage : public CredentialStorage {
public:
  bool read(const std::string &key, std::string &value) override {
    std::map<std::string, std::string> all = load();
    std::map<std::string, std::string>::const_iterator found = all.find(key);
    if (found == all.end()) return false;
    value = found->second;
    return true;
  }

  bool write(const std::string &key, const std::string &value) override {
    std::map<std::string, std::string> all = load();
    all[key] = value;
    return save(all);
  }

  bool remove(const std::string &key) override {
    std::map<std::string, std::string> all = load();
    if (all.erase(key) == 0) return false;
    return save(all);
  }

  bool exists(const std::string &key) override {
    std::map<std::string, std::string> all = load();
    return all.count(key) > 0;
  }

private:
  // "key=value" per line. A value may be empty, a key may not contain '='.
  std::map<std::string, std::string> load() {
    std::map<std::string, std::string> all;
    std::ifstream file(CREDENTIALS_FILE);
    if (!file) return all;

    std::string line;
    while (std::getline(file, line)) {
      size_t separator = line.find('=');
      if (separator == std::string::npos) continue;
      all[line.substr(0, separator)] = line.substr(separator + 1);
    }
    return all;
  }

  bool save(const std::map<std::string, std::string> &all) {
    std::error_code error;
    std::filesystem::create_directories(SIM_ROOT, error);
    std::ofstream file(CREDENTIALS_FILE, std::ios::trunc);
    if (!file) return false;
    for (std::map<std::string, std::string>::const_iterator it = all.begin(); it != all.end(); ++it) {
      file << it->first << "=" << it->second << "\n";
    }
    return file.good();
  }
};

CredentialStorage *get_credentialStorage() {
  static FileCredentialStorage storage;
  return &storage;
}

#endif
