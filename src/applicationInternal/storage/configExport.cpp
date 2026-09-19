#include "applicationInternal/storage/configExport.h"

#include "applicationInternal/commandHandler.h"
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/keyNames.h"
#include "applicationInternal/omote_log.h"
#include "applicationInternal/scenes/sceneRegistry.h"
#include "applicationInternal/storage/configFile.h"

namespace configExport {

const char *const BEGIN_MARKER = "===== BEGIN ";
const char *const END_MARKER = "===== END ";

static bool unexportableSequences = false;

configModel::DevicePack devicePack(const DeviceSelector &selector) {
  configModel::DevicePack pack = configModel::devicePackFromRegisteredCommands(
      selector.deviceId, selector.displayName, selector.namePrefix);
  pack.manufacturer = selector.manufacturer;
  pack.model = selector.model;
  return pack;
}

configModel::ScenesConfig scenes() {
  configModel::ScenesConfig config;
  unexportableSequences = false;

  for (std::map<std::string, scene_definition>::const_iterator it = registered_scenes.begin();
       it != registered_scenes.end(); ++it) {
    const scene_definition &definition = it->second;

    configModel::SceneConfig scene;
    scene.name = it->first;

    // The key maps of a scene are only filled once setKeys() has run, which
    // normally happens when the scene is activated. For an export we have to do
    // it ourselves, otherwise every scene comes out without any keys.
    if (definition.this_scene_setKeys != NULL) definition.this_scene_setKeys();

    if (definition.this_activate_scene_command != 0) {
      scene.activateCommand = get_commandName_byID(definition.this_activate_scene_command);
    }

    if (definition.this_gui_list != NULL) {
      for (size_t i = 0; i < definition.this_gui_list->size(); i++) {
        scene.guiList.push_back(definition.this_gui_list->at(i));
      }
    }

    // The three key maps are keyed by char; walk the named keys instead, so a
    // char without a name simply does not appear in the file.
    std::vector<keyNames::KeyName> allKeys = keyNames::all();
    for (size_t i = 0; i < allKeys.size(); i++) {
      char character = allKeys[i].character;

      bool hasRepeatMode = definition.this_key_repeatModes != NULL &&
                           definition.this_key_repeatModes->count(character) > 0;
      bool hasShort = definition.this_key_commands_short != NULL &&
                      definition.this_key_commands_short->count(character) > 0;
      bool hasLong = definition.this_key_commands_long != NULL &&
                     definition.this_key_commands_long->count(character) > 0;
      if (!hasRepeatMode && !hasShort && !hasLong) continue;

      configModel::SceneKeyBinding binding;
      binding.keyName = allKeys[i].name;
      binding.repeatMode = hasRepeatMode ? definition.this_key_repeatModes->at(character) : SHORT;
      if (hasShort) {
        binding.commandShort = get_commandName_byID(definition.this_key_commands_short->at(character));
      }
      if (hasLong) {
        binding.commandLong = get_commandName_byID(definition.this_key_commands_long->at(character));
      }

      // parseScenes refuses a long press command on a key that is not
      // SHORTorLONG, so an export must not produce one either. A key map that
      // has both without the mode is a bug in the C++ scene, not in the export.
      if (!binding.commandLong.empty() && binding.repeatMode != SHORTorLONG) {
        omote_log_w("configExport: scene '%s' key '%s' has a long press command but repeat mode %s, "
                    "dropping the long press command\r\n",
                    scene.name.c_str(), binding.keyName.c_str(),
                    configModel::repeatModeToString(binding.repeatMode).c_str());
        binding.commandLong.clear();
      }

      scene.keys.push_back(binding);
    }

    // Sequences are code, not data. Step 9 changes that.
    if (definition.this_scene_start_sequence != NULL || definition.this_scene_end_sequence != NULL) {
      unexportableSequences = true;
    }

    config.scenes.push_back(scene);
  }

  return config;
}

bool scenesHaveUnexportableSequences() { return unexportableSequences; }

configModel::KeysConfig keys() {
  configModel::KeysConfig config;
  config.hardwareRev = configModel::thisHardwareRevision();

  char matrix[keypadROWS][keypadCOLS];
  if (!get_keypadMatrix(matrix)) {
    // the simulator has no matrix; an empty one is the honest answer
    return config;
  }

  for (uint8_t row = 0; row < configModel::MATRIX_ROWS; row++) {
    for (uint8_t col = 0; col < configModel::MATRIX_COLS; col++) {
      config.matrix[row][col] = keyNames::nameFromChar(matrix[row][col]);
    }
  }
  return config;
}

static void appendFile(std::string &target, const std::string &path, const std::string &content) {
  target += BEGIN_MARKER + path + " =====\n";
  target += content;
  if (!content.empty() && content[content.size() - 1] != '\n') target += "\n";
  target += END_MARKER + path + " =====\n";
}

std::string dumpConfigAsJson(const std::vector<DeviceSelector> &devices) {
  std::string dump;

  appendFile(dump, configFile::PATH_SYSTEM,
             configModel::serializeSystemConfig(configModel::defaultSystemConfig()));

  for (size_t i = 0; i < devices.size(); i++) {
    configModel::DevicePack pack = devicePack(devices[i]);
    if (pack.commands.empty()) {
      omote_log_w("configExport: device '%s' has no commands, prefix '%s' matched nothing\r\n",
                  devices[i].deviceId.c_str(), devices[i].namePrefix.c_str());
      continue;
    }
    appendFile(dump, configFile::pathForDevice(pack.id), configModel::serializeDevicePack(pack));
  }

  appendFile(dump, configFile::PATH_SCENES, configModel::serializeScenes(scenes()));
  appendFile(dump, configFile::PATH_KEYS, configModel::serializeKeys(keys()));

  if (scenesHaveUnexportableSequences()) {
    dump += "\nNote: the start and end sequences of the scenes are C++ functions and are not part of\n"
            "this export. They become data in step 9; until then they stay compiled in.\n";
  }

  return dump;
}

} // namespace configExport
