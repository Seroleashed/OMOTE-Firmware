#if defined(WIN32) || defined(__linux__) || defined(__APPLE__)

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "applicationInternal/omote_log.h"
#include "applicationInternal/storage/configFileSystem.h"

/*
  File system for the simulator. Everything the firmware would store in the
  littlefs partition ends up in ./omote_data/ next to the simulator binary, so
  a configuration can be edited, exported and re-imported without hardware.
*/

static const char *const SIM_ROOT = "omote_data";

class PcConfigFileSystem : public ConfigFileSystem {
public:
  bool exists(const std::string &path) override {
    return std::filesystem::exists(toHostPath(path));
  }

  bool read(const std::string &path, std::string &content) override {
    std::ifstream file(toHostPath(path), std::ios::binary);
    if (!file) return false;
    content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return true;
  }

  bool write(const std::string &path, const std::string &content) override {
    std::filesystem::path hostPath = toHostPath(path);
    std::error_code ec;
    std::filesystem::create_directories(hostPath.parent_path(), ec);
    std::ofstream file(hostPath, std::ios::binary | std::ios::trunc);
    if (!file) {
      omote_log_e("configFileSystem: could not open %s for writing\r\n", path.c_str());
      return false;
    }
    file.write(content.data(), (std::streamsize)content.size());
    file.flush();
    return file.good();
  }

  bool rename(const std::string &from, const std::string &to) override {
    std::error_code ec;
    // std::filesystem::rename overwrites an existing destination, which is
    // exactly the behaviour configStorage relies on
    std::filesystem::rename(toHostPath(from), toHostPath(to), ec);
    return !ec;
  }

  bool remove(const std::string &path) override {
    std::error_code ec;
    return std::filesystem::remove(toHostPath(path), ec);
  }

  std::vector<std::string> list(const std::string &directory) override {
    std::vector<std::string> paths;
    std::error_code ec;
    std::filesystem::directory_iterator entries(toHostPath(directory), ec);
    if (ec) return paths; // no such directory: nothing configured yet

    for (const std::filesystem::directory_entry &entry : entries) {
      if (!entry.is_regular_file()) continue;
      // hand back the device side path, not the host path
      paths.push_back(directory + "/" + entry.path().filename().string());
    }
    // directory_iterator has no defined order, and which device gets registered
    // first should not depend on the file system's mood
    std::sort(paths.begin(), paths.end());
    return paths;
  }

private:
  // "/cfg/system.json" -> "omote_data/cfg/system.json"
  std::filesystem::path toHostPath(const std::string &path) {
    std::string relative = path;
    while (!relative.empty() && (relative[0] == '/' || relative[0] == '\\')) {
      relative.erase(0, 1);
    }
    return std::filesystem::path(SIM_ROOT) / relative;
  }
};

static PcConfigFileSystem pcConfigFileSystem;

ConfigFileSystem *get_configFileSystem() { return &pcConfigFileSystem; }

#endif
