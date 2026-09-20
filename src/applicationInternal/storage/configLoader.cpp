#include "applicationInternal/storage/configLoader.h"

#include "applicationInternal/bootGuard.h"
#include "applicationInternal/commandHandler.h"
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/omote_log.h"
#include "applicationInternal/storage/configFile.h"
#include "applicationInternal/storage/configFileSystem.h"
#include "applicationInternal/storage/configModel.h"
#include "applicationInternal/storage/configStorage.h"

namespace configLoader {

static Report report;

const Report &lastReport() { return report; }

// .bak and .tmp belong to configStorage, not to the configuration. Loading a
// .bak would quietly bring back the previous version of a device next to the
// current one.
static bool isConfigurationFile(const std::string &path) {
  const std::string suffix = ".json";
  if (path.size() < suffix.size()) return false;
  return path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static bool registerPack(const configModel::DevicePack &pack, FileResult &result) {
  for (size_t i = 0; i < pack.commands.size(); i++) {
    const configModel::CommandDef &command = pack.commands[i];

    // Does this name already belong to something? Then the file takes it over.
    // The old id keeps working for whoever still holds it, it just loses the
    // name - which is exactly what "the web UI replaced this device" means.
    if (get_commandID_byName(command.name) != COMMAND_UNKNOWN) {
      result.overrides++;
    }

    // The id is handed out by the registry; nothing in a file ever refers to
    // it, so a local is all we need to keep it.
    uint16_t id = 0;
    register_command_withName(&id, makeCommandData(command.handler, command.payloads), command.name);
    result.commandCount++;
  }
  return true;
}

static void loadOneFile(const std::string &path, ConfigFileSystem *fileSystem) {
  FileResult result;
  result.path = path;

  configStorage::LoadedConfig stored = configStorage::load(path);
  std::string payload;

  if (stored.usable()) {
    if (stored.result == configStorage::LoadResult::OkFromBackup) {
      omote_log_w("configLoader: %s was broken, used the backup\r\n", path.c_str());
    }
    payload = stored.payload;
  } else {
    /*
      No usable envelope. Two very different situations end up here, and mixing
      them up would send the user hunting for the wrong problem:

      * the file never had an envelope - copied onto the device by hand, or
        written by a future transport. That is plain JSON and perfectly fine.
        The envelope is added when *we* write a file, not demanded when we read.
      * the file has a header but fails verification - genuinely damaged, and
        the backup did not help either.
    */
    if (!fileSystem->read(path, payload)) {
      result.error = "could not be read";
      report.filesFailed++;
      report.files.push_back(result);
      omote_log_e("configLoader: %s could not be read\r\n", path.c_str());
      return;
    }
    if (configStorage::looksLikeEnvelope(payload)) {
      result.error = "damaged: the checksum does not match and the backup is unusable either";
      report.filesFailed++;
      report.files.push_back(result);
      omote_log_e("configLoader: %s is damaged\r\n", path.c_str());
      return;
    }
  }

  configModel::DevicePack pack;
  std::string error;
  if (!configModel::parseDevicePack(payload, pack, error)) {
    result.error = error;
    report.filesFailed++;
    report.files.push_back(result);
    // Not fatal on purpose: every other device still loads. The message is kept
    // so the web UI can tell the user which file to fix.
    omote_log_e("configLoader: %s was skipped: %s\r\n", path.c_str(), error.c_str());
    return;
  }

  result.deviceId = pack.id;
  registerPack(pack, result);
  result.loaded = true;

  report.devicesLoaded++;
  report.commandsRegistered += result.commandCount;
  report.files.push_back(result);

  if (result.overrides > 0) {
    omote_log_w("configLoader: device '%s' from %s replaced %u command(s) that were already "
                "registered\r\n",
                pack.id.c_str(), path.c_str(), (unsigned)result.overrides);
  }
  omote_log_i("configLoader: device '%s' with %u commands from %s\r\n", pack.id.c_str(),
              (unsigned)result.commandCount, path.c_str());
}

Report loadDevices() {
  report = Report();

  if (bootGuard::isSafeMode()) {
    report.skippedBecauseOfSafeMode = true;
    omote_log_w("configLoader: safe mode, the stored configuration is not loaded\r\n");
    return report;
  }

  ConfigFileSystem *fileSystem = configStorage::fileSystem();
  if (fileSystem == NULL) {
    omote_log_e("configLoader: no file system, nothing loaded\r\n");
    return report;
  }

  std::vector<std::string> paths = fileSystem->list(configFile::DEVICES_DIRECTORY);
  for (size_t i = 0; i < paths.size(); i++) {
    if (!isConfigurationFile(paths[i])) continue;
    loadOneFile(paths[i], fileSystem);
  }

  if (report.devicesLoaded == 0 && report.filesFailed == 0) {
    omote_log_i("configLoader: no stored devices, using the configuration compiled in\r\n");
  } else {
    omote_log_i("configLoader: %u device(s), %u command(s), %u file(s) skipped\r\n",
                (unsigned)report.devicesLoaded, (unsigned)report.commandsRegistered,
                (unsigned)report.filesFailed);
  }
  return report;
}

// --- system.json -------------------------------------------------------------

static configModel::SystemConfig activeSystemConfig;
static bool systemConfigLoaded = false;

const configModel::SystemConfig &systemConfig() {
  if (!systemConfigLoaded) {
    activeSystemConfig = configModel::defaultSystemConfig();
    systemConfigLoaded = true;
  }
  return activeSystemConfig;
}

/*
  Reads the file into the values the device is running with, rather than into a
  fresh struct. parseSystemConfig leaves anything the file does not mention
  untouched, so a system.json holding only a brightness does exactly that one
  thing and nothing else.
*/
static bool readSystemFile(configModel::SystemConfig &into, std::string &error) {
  ConfigFileSystem *fileSystem = configStorage::fileSystem();
  if (fileSystem == NULL) return false;

  configStorage::LoadedConfig stored = configStorage::load(configFile::PATH_SYSTEM);
  std::string payload;

  if (stored.usable()) {
    if (stored.result == configStorage::LoadResult::OkFromBackup) {
      omote_log_w("configLoader: %s was broken, used the backup\r\n", configFile::PATH_SYSTEM);
    }
    payload = stored.payload;
  } else {
    if (!fileSystem->read(configFile::PATH_SYSTEM, payload)) return false; // simply not there
    if (configStorage::looksLikeEnvelope(payload)) {
      error = "damaged: the checksum does not match and the backup is unusable either";
      return true; // the file is there, it is just unusable
    }
  }

  std::string parseError;
  if (!configModel::parseSystemConfig(payload, into, parseError)) {
    error = parseError;
    return true;
  }
  return true;
}

SystemResult loadSystem() {
  SystemResult result;

  activeSystemConfig = configModel::defaultSystemConfig();
  systemConfigLoaded = true;

  if (bootGuard::isSafeMode()) {
    omote_log_w("configLoader: safe mode, %s is not read\r\n", configFile::PATH_SYSTEM);
    return result;
  }

  // Start from what the device is actually running with, not from the compiled
  // defaults: the preferences hold what the user set on the device, and a file
  // that says nothing about brightness must not reset it.
  activeSystemConfig.backlightBrightness = get_backlightBrightness();
#if (OMOTE_HARDWARE_REV >= 5)
  activeSystemConfig.keyboardBrightness = get_keyboardBrightness();
#endif
  activeSystemConfig.sleepTimeoutMs = get_sleepTimeout();
  activeSystemConfig.wakeupByIMU = get_wakeupByIMUEnabled();
  activeSystemConfig.motionThreshold = get_motionThreshold();

  result.fileFound = readSystemFile(activeSystemConfig, result.error);
  if (!result.fileFound) {
    omote_log_i("configLoader: no %s, using the settings on the device\r\n", configFile::PATH_SYSTEM);
    return result;
  }
  if (!result.error.empty()) {
    // Nothing is applied. A file nobody can read must not leave the device with
    // half of it in place.
    omote_log_e("configLoader: %s was skipped: %s\r\n", configFile::PATH_SYSTEM, result.error.c_str());
    activeSystemConfig = configModel::defaultSystemConfig();
    return result;
  }

  set_backlightBrightness(activeSystemConfig.backlightBrightness);
#if (OMOTE_HARDWARE_REV >= 5)
  set_keyboardBrightness(activeSystemConfig.keyboardBrightness);
#endif
  set_sleepTimeout(activeSystemConfig.sleepTimeoutMs);
  set_wakeupByIMUEnabled(activeSystemConfig.wakeupByIMU);
  set_motionThreshold(activeSystemConfig.motionThreshold);

  result.applied = true;
  omote_log_i("configLoader: %s applied, device name '%s'\r\n", configFile::PATH_SYSTEM,
              activeSystemConfig.deviceName.c_str());
  return result;
}

} // namespace configLoader
