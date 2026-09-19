#include <Arduino.h>
#include <LittleFS.h>

#include "applicationInternal/omote_log.h"
#include "applicationInternal/storage/configFileSystem.h"

/*
  LittleFS backed file system for the configuration storage.

  The partition is called "littlefs" in ota_16MB_custom.csv. On the first boot
  after flashing the new partition table it is empty and gets formatted once.

  Note on rename(): LittleFS refuses to rename onto an existing file, so the
  destination is removed first. That window is exactly why configStorage keeps
  a backup - see the tests in test/test_config_storage.
*/

class LittleFsConfigFileSystem : public ConfigFileSystem {
public:
  bool mount() {
    if (mounted) return true;
    // second parameter: format if mounting fails (first boot after erase)
    if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
      omote_log_e("configFileSystem: could not mount littlefs\r\n");
      return false;
    }
    mounted = true;
    omote_log_i("configFileSystem: littlefs mounted, %u of %u bytes used\r\n",
                (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
    return true;
  }

  bool exists(const std::string &path) override {
    if (!mount()) return false;
    return LittleFS.exists(path.c_str());
  }

  bool read(const std::string &path, std::string &content) override {
    if (!mount()) return false;
    File file = LittleFS.open(path.c_str(), FILE_READ);
    if (!file) return false;
    content.clear();
    content.reserve(file.size());
    while (file.available()) {
      content += (char)file.read();
    }
    file.close();
    return true;
  }

  bool write(const std::string &path, const std::string &content) override {
    if (!mount()) return false;
    createParentDirs(path);
    File file = LittleFS.open(path.c_str(), FILE_WRITE);
    if (!file) {
      omote_log_e("configFileSystem: could not open %s for writing\r\n", path.c_str());
      return false;
    }
    size_t written = file.write((const uint8_t *)content.data(), content.size());
    file.flush();
    file.close();
    if (written != content.size()) {
      omote_log_e("configFileSystem: short write on %s (%u of %u)\r\n", path.c_str(),
                  (unsigned)written, (unsigned)content.size());
      return false;
    }
    return true;
  }

  bool rename(const std::string &from, const std::string &to) override {
    if (!mount()) return false;
    if (LittleFS.exists(to.c_str())) LittleFS.remove(to.c_str());
    return LittleFS.rename(from.c_str(), to.c_str());
  }

  bool remove(const std::string &path) override {
    if (!mount()) return false;
    return LittleFS.remove(path.c_str());
  }

  std::vector<std::string> list(const std::string &directory) override {
    std::vector<std::string> paths;
    if (!mount()) return paths;

    File folder = LittleFS.open(directory.c_str());
    if (!folder || !folder.isDirectory()) return paths;

    File entry = folder.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        // depending on the core version, name() is either the bare file name or
        // the full path. Normalise it, the caller wants something it can open.
        std::string name = entry.name();
        if (name.find('/') == std::string::npos) name = directory + "/" + name;
        paths.push_back(name);
      }
      entry.close();
      entry = folder.openNextFile();
    }
    folder.close();
    return paths;
  }

private:
  bool mounted = false;

  // LittleFS needs the directory to exist before a file can be created in it
  void createParentDirs(const std::string &path) {
    size_t pos = path.find('/', 1);
    while (pos != std::string::npos) {
      std::string dir = path.substr(0, pos);
      if (!LittleFS.exists(dir.c_str())) LittleFS.mkdir(dir.c_str());
      pos = path.find('/', pos + 1);
    }
  }
};

static LittleFsConfigFileSystem littleFsConfigFileSystem;

ConfigFileSystem *get_configFileSystem() {
  littleFsConfigFileSystem.mount();
  return &littleFsConfigFileSystem;
}
