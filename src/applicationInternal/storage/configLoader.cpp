#include "applicationInternal/storage/configLoader.h"

#include <list>
#include <map>

#include "applicationInternal/bootGuard.h"
#include "applicationInternal/commandHandler.h"
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/keyNames.h"
#include "applicationInternal/omote_log.h"
#include "applicationInternal/scenes/sceneRegistry.h"
#include "applicationInternal/scenes/sequenceEngine.h"
#include "applicationInternal/storage/configFile.h"
#include "applicationInternal/storage/configFileSystem.h"
#include "applicationInternal/storage/configModel.h"
#include "applicationInternal/storage/configScenes.h"
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

// --- reading one of the single configuration files ---------------------------

/*
  The three single files - system, scenes, keys - all arrive the same way, and
  all three have to tell the same three situations apart: not there, there but
  damaged, there and readable.
*/
static bool readConfigFile(const char *path, std::string &payload, std::string &error) {
  ConfigFileSystem *fileSystem = configStorage::fileSystem();
  if (fileSystem == NULL) return false;

  configStorage::LoadedConfig stored = configStorage::load(path);
  if (stored.usable()) {
    if (stored.result == configStorage::LoadResult::OkFromBackup) {
      omote_log_w("configLoader: %s was broken, used the backup\r\n", path);
    }
    payload = stored.payload;
    return true;
  }

  if (!fileSystem->read(path, payload)) return false; // simply not there
  if (configStorage::looksLikeEnvelope(payload)) {
    error = "damaged: the checksum does not match and the backup is unusable either";
  }
  return true;
}

// --- scenes.json -------------------------------------------------------------

/*
  Everything a scene read from a file needs to keep alive.

  register_scene stores pointers, so these have to stay put for as long as the
  scene is registered - which is until the next restart. A std::list rather than
  a vector on purpose: a vector reallocating would move every one of them and
  leave the registry pointing at freed memory.
*/
namespace {
struct OwnedScene {
  std::map<char, repeatModes> repeatModesByKey;
  std::map<char, uint16_t> commandsShort;
  std::map<char, uint16_t> commandsLong;
  t_gui_list guiList;
  std::vector<sequenceEngine::Step> startSequence;
  std::vector<sequenceEngine::Step> endSequence;
};

std::list<OwnedScene> ownedScenes;
} // namespace

static void buildSequence(const std::vector<configModel::SequenceStep> &source,
                          std::vector<sequenceEngine::Step> &target, const std::string &sceneName,
                          ScenesResult &result) {
  for (size_t i = 0; i < source.size(); i++) {
    uint16_t command = get_commandID_byName(source[i].commandName);
    if (command == COMMAND_UNKNOWN) {
      // One unknown command must not swallow the rest of the scene.
      result.stepsDropped++;
      omote_log_w("configLoader: scene '%s' names unknown command '%s', step dropped\r\n",
                  sceneName.c_str(), source[i].commandName.c_str());
      continue;
    }
    target.push_back(sequenceEngine::Step(command, source[i].payload, source[i].delayAfterMs));
  }
}

ScenesResult loadScenes() {
  ScenesResult result;

  if (bootGuard::isSafeMode()) {
    omote_log_w("configLoader: safe mode, %s is not read\r\n", configFile::PATH_SCENES);
    return result;
  }

  std::string payload;
  result.fileFound = readConfigFile(configFile::PATH_SCENES, payload, result.error);
  if (!result.fileFound || !result.error.empty()) {
    if (!result.error.empty()) {
      omote_log_e("configLoader: %s was skipped: %s\r\n", configFile::PATH_SCENES,
                  result.error.c_str());
    }
    return result;
  }

  configModel::ScenesConfig config;
  if (!configModel::parseScenes(payload, config, result.error)) {
    omote_log_e("configLoader: %s was skipped: %s\r\n", configFile::PATH_SCENES, result.error.c_str());
    return result;
  }

  for (size_t s = 0; s < config.scenes.size(); s++) {
    const configModel::SceneConfig &scene = config.scenes[s];

    ownedScenes.push_back(OwnedScene());
    OwnedScene &owned = ownedScenes.back();

    for (size_t k = 0; k < scene.keys.size(); k++) {
      const configModel::SceneKeyBinding &binding = scene.keys[k];
      char character = 0;
      if (!keyNames::charFromName(binding.keyName, character)) continue; // parseScenes checked this

      owned.repeatModesByKey[character] = binding.repeatMode;
      if (!binding.commandShort.empty()) {
        uint16_t command = get_commandID_byName(binding.commandShort);
        if (command == COMMAND_UNKNOWN) {
          result.stepsDropped++;
          omote_log_w("configLoader: scene '%s' key '%s' names unknown command '%s'\r\n",
                      scene.name.c_str(), binding.keyName.c_str(), binding.commandShort.c_str());
        } else {
          owned.commandsShort[character] = command;
          result.keysBound++;
        }
      }
      if (!binding.commandLong.empty()) {
        uint16_t command = get_commandID_byName(binding.commandLong);
        if (command != COMMAND_UNKNOWN) owned.commandsLong[character] = command;
      }
    }

    owned.guiList = scene.guiList;
    buildSequence(scene.startSequence, owned.startSequence, scene.name, result);
    buildSequence(scene.endSequence, owned.endSequence, scene.name, result);

    uint16_t activateCommand = 0;
    if (!scene.activateCommand.empty()) {
      uint16_t found = get_commandID_byName(scene.activateCommand);
      if (found != COMMAND_UNKNOWN) activateCommand = found;
    }

    /*
      No setKeys function: the maps are already filled. The gui list is only
      handed over if it has something in it, because an empty one would hide
      every screen for that scene rather than falling back to the main list.
    */
    register_scene(scene.name, NULL, NULL, NULL, &owned.repeatModesByKey, &owned.commandsShort,
                   &owned.commandsLong, owned.guiList.empty() ? NULL : &owned.guiList,
                   activateCommand, &owned.startSequence, &owned.endSequence);

    result.scenesLoaded++;
    omote_log_i("configLoader: scene '%s' with %u keys and %u start step(s)\r\n", scene.name.c_str(),
                (unsigned)owned.commandsShort.size(), (unsigned)owned.startSequence.size());
  }

  if (result.stepsDropped > 0) {
    omote_log_w("configLoader: %u reference(s) in %s named a command this device does not know\r\n",
                (unsigned)result.stepsDropped, configFile::PATH_SCENES);
  }
  return result;
}

// --- keys.json ---------------------------------------------------------------

KeysResult loadKeys() {
  KeysResult result;

  if (bootGuard::isSafeMode()) {
    omote_log_w("configLoader: safe mode, %s is not read\r\n", configFile::PATH_KEYS);
    return result;
  }

  std::string payload;
  result.fileFound = readConfigFile(configFile::PATH_KEYS, payload, result.error);
  if (!result.fileFound || !result.error.empty()) {
    if (!result.error.empty()) {
      omote_log_e("configLoader: %s was skipped: %s\r\n", configFile::PATH_KEYS, result.error.c_str());
    }
    return result;
  }

  configModel::KeysConfig keys;
  if (!configModel::parseKeys(payload, keys, result.revisionMismatch, result.error)) {
    omote_log_e("configLoader: %s was skipped: %s\r\n", configFile::PATH_KEYS, result.error.c_str());
    return result;
  }

  if (result.revisionMismatch) {
    /*
      Refused rather than applied. Rev5 and Rev1-4 hold the same keys in
      reversed row order, so the wrong file mirrors the keypad - and the user is
      left pressing "up" to go down with nothing to explain it. The web UI can
      offer to flip the rows; guessing here would not be a kindness.
    */
    result.error = "written for another hardware revision";
    omote_log_e("configLoader: %s was written for hardware revision %u, not applied\r\n",
                configFile::PATH_KEYS, (unsigned)keys.hardwareRev);
    return result;
  }

  char matrix[keypadROWS][keypadCOLS];
  for (uint8_t row = 0; row < configModel::MATRIX_ROWS; row++) {
    for (uint8_t col = 0; col < configModel::MATRIX_COLS; col++) {
      char character = NO_KEY;
      if (!keys.matrix[row][col].empty()) {
        keyNames::charFromName(keys.matrix[row][col], character);
      }
      matrix[row][col] = character;
    }
  }

  if (!set_keypadMatrix(matrix)) {
    result.error = "this build has no keypad matrix to replace";
    omote_log_w("configLoader: %s\r\n", result.error.c_str());
    return result;
  }

  result.applied = true;
  omote_log_i("configLoader: %s applied\r\n", configFile::PATH_KEYS);
  return result;
}

} // namespace configLoader
