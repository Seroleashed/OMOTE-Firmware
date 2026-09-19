#include "applicationInternal/storage/configExport.h"

#include "applicationInternal/commandHandler.h"
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/keyNames.h"
#include "applicationInternal/omote_log.h"
#include "applicationInternal/scenes/sceneRegistry.h"
#include "applicationInternal/scenes/sequenceEngine.h"
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

/*
  Runs a scene's sequence function against an empty queue and takes what it
  enqueued. Nothing is executed - loop() is never called, so no command leaves
  the device.

  A step whose command has no name cannot be written to a file. That would mean
  a scene referring to something the registry does not know by name, which is a
  bug worth naming rather than a step to drop quietly.
*/
static std::vector<configModel::SequenceStep> captureSequence(void (*sequenceFunction)(),
                                                              const std::string &sceneName) {
  std::vector<configModel::SequenceStep> steps;
  if (sequenceFunction == NULL) return steps;

  sequenceEngine::abort();
  sequenceFunction();
  std::vector<sequenceEngine::Step> captured = sequenceEngine::takePending();

  for (size_t i = 0; i < captured.size(); i++) {
    std::string name = get_commandName_byID(captured[i].command);
    if (name.empty()) {
      unexportableSequences = true;
      omote_log_w("configExport: scene '%s' has a sequence step whose command has no name, "
                  "it is not exported\r\n",
                  sceneName.c_str());
      continue;
    }
    configModel::SequenceStep step;
    step.commandName = name;
    step.payload = captured[i].payload;
    step.delayAfterMs = captured[i].delayAfterMs;
    steps.push_back(step);
  }
  return steps;
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

    /*
      The sequences are still C++ functions, but since step 9 all they do is
      enqueue steps into sequenceEngine. So we let them enqueue into an empty
      queue and take the result instead of running it - the sequence comes out
      as data without a single IR command being sent at the actual TV.
    */
    scene.startSequence = captureSequence(definition.this_scene_start_sequence, scene.name);
    scene.endSequence = captureSequence(definition.this_scene_end_sequence, scene.name);

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
    dump += "\nNote: at least one sequence step refers to a command that has no name and could not\n"
            "be exported. See the log for which scene.\n";
  }

  return dump;
}

} // namespace configExport
