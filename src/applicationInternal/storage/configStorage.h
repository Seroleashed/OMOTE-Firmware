#pragma once

#include <stdint.h>
#include <string>

#include "applicationInternal/storage/configFileSystem.h"

/*
  Crash safe storage for configuration files.

  A saved file looks like this:

      OMOTECFG1 v=3 len=42 crc=0x1c291ca3
      {"schemaVersion":3, ... }

  The header lets us detect a half written file after a power loss, and the
  schema version travels with the payload so a migration can run on load.

  save() never overwrites the only good copy:
      1. write everything to <name>.tmp
      2. read <name>.tmp back and verify header + crc
      3. move the current <name> to <name>.bak
      4. rename <name>.tmp to <name>

  Whenever the power fails in between, load() still finds either the new or
  the previous configuration - never a truncated one.

  load() therefore returns Ok, OkFromBackup, NotFound or Corrupt. The caller
  decides what to do: OkFromBackup is worth a warning, Corrupt and NotFound
  mean "fall back to the configuration compiled into the firmware".
*/
namespace configStorage {

enum class LoadResult {
  Ok,           // main file was read and verified
  OkFromBackup, // main file was missing or broken, backup was used
  NotFound,     // nothing stored yet
  Corrupt,      // both copies exist but neither verifies
};

struct LoadedConfig {
  LoadResult result = LoadResult::NotFound;
  uint16_t schemaVersion = 0;
  std::string payload;

  bool usable() const { return result == LoadResult::Ok || result == LoadResult::OkFromBackup; }
};

// Must be called once at startup with the file system of the active hardware.
void setFileSystem(ConfigFileSystem *fileSystem);

// name is a plain file name like "/cfg/system.json"
bool save(const std::string &name, const std::string &payload, uint16_t schemaVersion);
LoadedConfig load(const std::string &name);

// Removes the file and its backup. Used by "reset to defaults".
bool remove(const std::string &name);

bool hasStoredConfig(const std::string &name);

// exposed for tests and for the later transport protocol (chunk checksums)
uint32_t crc32(const std::string &data);

// exposed so tests can build a file with a deliberately wrong crc
std::string buildEnvelope(const std::string &payload, uint16_t schemaVersion);

} // namespace configStorage
