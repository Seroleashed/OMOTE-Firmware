/*
  Unit tests for applicationInternal/storage/configExport.cpp

  The export is the migration path for everybody who has their devices in C++
  today, and it is where the realistic test data comes from. Both make it worth
  proving that what comes out goes back in: every test here that exports
  something parses it again, because an export nobody can import is worse than
  no export at all.
*/

#include <unity.h>

#include <map>
#include <string>

#include "applicationInternal/commandHandler.h"
#include "applicationInternal/hardware/IRremoteProtocols.h"
#include "applicationInternal/scenes/sceneRegistry.h"
#include "applicationInternal/storage/configExport.h"
#include "omote_fakes.h"

static uint16_t TESTDEV_POWER;
static uint16_t TESTDEV_UP;
static uint16_t TESTDEV_MENU;
static uint16_t OTHER_POWER;
static uint16_t SCENE_ACTIVATE;

static std::map<char, repeatModes> repeatModesOfScene;
static std::map<char, uint16_t> commandsShortOfScene;
static std::map<char, uint16_t> commandsLongOfScene;
static t_gui_list guiListOfScene;
static bool setKeysWasCalled = false;

static void sceneSetKeys() {
  setKeysWasCalled = true;
  repeatModesOfScene = {{KEY_UP, SHORT_REPEATED}, {KEY_OK, SHORTorLONG}};
  commandsShortOfScene = {{KEY_UP, TESTDEV_UP}, {KEY_OK, TESTDEV_POWER}};
  commandsLongOfScene = {{KEY_OK, TESTDEV_MENU}};
}
static void sceneStartSequence() {}
static void sceneEndSequence() {}

void setUp(void) {
  fakes::reset();
  fakes::clearKeypadMatrix();
  setKeysWasCalled = false;
  registered_scenes.clear();

  register_command(&TESTDEV_POWER,
                   makeCommandData(IR, {std::to_string(IR_PROTOCOL_SAMSUNG), "0xE0E040BF"}));
  register_command(&TESTDEV_UP, makeCommandData(IR, {std::to_string(IR_PROTOCOL_SAMSUNG), "0xE0E006F9"}));
  register_command(&TESTDEV_MENU, makeCommandData(IR, {std::to_string(IR_PROTOCOL_SAMSUNG), "0xE0E058A7"}));
  register_command(&OTHER_POWER, makeCommandData(IR, {std::to_string(IR_PROTOCOL_NEC), "0x77E1"}));
  register_command(&SCENE_ACTIVATE, makeCommandData(SCENE, {"Testszene"}));

  guiListOfScene = {"Numpad"};
}

void tearDown(void) { registered_scenes.clear(); }

// --- devices -----------------------------------------------------------------

void test_a_prefix_selects_only_the_commands_of_that_device(void) {
  configExport::DeviceSelector selector;
  selector.deviceId = "testdev";
  selector.displayName = "Test device";
  selector.namePrefix = "TESTDEV_";
  selector.manufacturer = "ACME";
  selector.model = "X1";

  configModel::DevicePack pack = configExport::devicePack(selector);

  TEST_ASSERT_EQUAL_size_t(3, pack.commands.size());
  TEST_ASSERT_EQUAL_STRING("testdev", pack.id.c_str());
  TEST_ASSERT_EQUAL_STRING("ACME", pack.manufacturer.c_str());
  TEST_ASSERT_EQUAL_STRING("X1", pack.model.c_str());
  for (size_t i = 0; i < pack.commands.size(); i++) {
    TEST_ASSERT_TRUE(pack.commands[i].name.rfind("TESTDEV_", 0) == 0);
  }
}

void test_an_exported_device_can_be_imported_again(void) {
  configExport::DeviceSelector selector;
  selector.deviceId = "testdev";
  selector.namePrefix = "TESTDEV_";

  std::string json = configModel::serializeDevicePack(configExport::devicePack(selector));

  configModel::DevicePack reparsed;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseDevicePack(json, reparsed, error), error.c_str());
  TEST_ASSERT_EQUAL_size_t(3, reparsed.commands.size());
  // the payloads have to survive exactly - this is what a user's IR codes are
  TEST_ASSERT_EQUAL_size_t(2, reparsed.commands[0].payloads.size());
}

// --- scenes ------------------------------------------------------------------

static void registerTestScene() {
  register_scene("Testszene", &sceneSetKeys, &sceneStartSequence, &sceneEndSequence,
                 &repeatModesOfScene, &commandsShortOfScene, &commandsLongOfScene, &guiListOfScene,
                 SCENE_ACTIVATE);
}

void test_exporting_a_scene_fills_its_key_maps_first(void) {
  // the key maps of a scene are only filled once setKeys() has run, which
  // normally happens on activation. Without this the export would silently
  // produce scenes without any keys.
  repeatModesOfScene.clear();
  commandsShortOfScene.clear();
  commandsLongOfScene.clear();
  registerTestScene();

  configModel::ScenesConfig config = configExport::scenes();

  TEST_ASSERT_TRUE(setKeysWasCalled);
  TEST_ASSERT_EQUAL_size_t(1, config.scenes.size());
  TEST_ASSERT_EQUAL_size_t(2, config.scenes[0].keys.size());
}

void test_scene_keys_are_exported_with_names_not_ids(void) {
  registerTestScene();
  configModel::ScenesConfig config = configExport::scenes();

  const configModel::SceneConfig &scene = config.scenes[0];
  TEST_ASSERT_EQUAL_STRING("Testszene", scene.name.c_str());
  TEST_ASSERT_EQUAL_STRING("SCENE_ACTIVATE", scene.activateCommand.c_str());
  TEST_ASSERT_EQUAL_size_t(1, scene.guiList.size());
  TEST_ASSERT_EQUAL_STRING("Numpad", scene.guiList[0].c_str());

  bool foundOk = false;
  for (size_t i = 0; i < scene.keys.size(); i++) {
    if (scene.keys[i].keyName != "KEY_OK") continue;
    foundOk = true;
    TEST_ASSERT_EQUAL_INT((int)SHORTorLONG, (int)scene.keys[i].repeatMode);
    TEST_ASSERT_EQUAL_STRING("TESTDEV_POWER", scene.keys[i].commandShort.c_str());
    TEST_ASSERT_EQUAL_STRING("TESTDEV_MENU", scene.keys[i].commandLong.c_str());
  }
  TEST_ASSERT_TRUE_MESSAGE(foundOk, "KEY_OK missing from the export");
}

void test_an_exported_scene_can_be_imported_again(void) {
  registerTestScene();
  std::string json = configModel::serializeScenes(configExport::scenes());

  configModel::ScenesConfig reparsed;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseScenes(json, reparsed, error), error.c_str());
  TEST_ASSERT_EQUAL_size_t(1, reparsed.scenes.size());
  TEST_ASSERT_EQUAL_size_t(2, reparsed.scenes[0].keys.size());
}

void test_a_long_command_without_shortorlong_is_dropped_not_exported(void) {
  // parseScenes refuses such a file, so the export must not produce one. A key
  // map like this is a bug in the C++ scene, and the export must not turn it
  // into a file that cannot be read back.
  repeatModesOfScene = {{KEY_OK, SHORT}};
  commandsShortOfScene = {{KEY_OK, TESTDEV_POWER}};
  commandsLongOfScene = {{KEY_OK, TESTDEV_MENU}};
  register_scene("Kaputt", NULL, NULL, NULL, &repeatModesOfScene, &commandsShortOfScene,
                 &commandsLongOfScene, NULL, 0);

  std::string json = configModel::serializeScenes(configExport::scenes());

  configModel::ScenesConfig reparsed;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseScenes(json, reparsed, error), error.c_str());
  TEST_ASSERT_EQUAL_STRING("", reparsed.scenes[0].keys[0].commandLong.c_str());
}

void test_a_scene_with_sequences_is_reported_as_incomplete(void) {
  // the sequences are C++ functions with delay() in them, not data. Step 9
  // changes that; until then the export has to say so rather than pretend the
  // scene came out whole.
  registerTestScene();
  configExport::scenes();
  TEST_ASSERT_TRUE(configExport::scenesHaveUnexportableSequences());
}

void test_a_scene_without_sequences_is_not_reported(void) {
  register_scene("Ohne", NULL, NULL, NULL, &repeatModesOfScene, &commandsShortOfScene,
                 &commandsLongOfScene, NULL, 0);
  configExport::scenes();
  TEST_ASSERT_FALSE(configExport::scenesHaveUnexportableSequences());
}

// --- keypad matrix -----------------------------------------------------------

void test_the_matrix_is_exported_with_key_names(void) {
  char matrix[keypadROWS][keypadCOLS] = {
      {'?', 'p', 'c', '<', '='}, {'>', 'o', 'b', 'u', 'l'}, {'4', 'v', '1', '3', '2'},
      {'i', 'r', '+', 'k', 'd'}, {'s', '^', '-', 'm', 'e'},
  };
  fakes::setKeypadMatrix(matrix);

  configModel::KeysConfig config = configExport::keys();

  TEST_ASSERT_EQUAL_STRING("KEY_OK", config.matrix[3][3].c_str());
  TEST_ASSERT_EQUAL_STRING("KEY_PLAY", config.matrix[0][1].c_str());
  // '?' has no name and becomes an empty position, not a broken one
  TEST_ASSERT_EQUAL_STRING("", config.matrix[0][0].c_str());
}

void test_an_exported_matrix_can_be_imported_again(void) {
  char matrix[keypadROWS][keypadCOLS] = {
      {'?', 'p', 'c', '<', '='}, {'>', 'o', 'b', 'u', 'l'}, {'4', 'v', '1', '3', '2'},
      {'i', 'r', '+', 'k', 'd'}, {'s', '^', '-', 'm', 'e'},
  };
  fakes::setKeypadMatrix(matrix);

  std::string json = configModel::serializeKeys(configExport::keys());

  configModel::KeysConfig reparsed;
  bool mismatch = true;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseKeys(json, reparsed, mismatch, error), error.c_str());
  TEST_ASSERT_FALSE(mismatch);
  TEST_ASSERT_EQUAL_STRING("KEY_OK", reparsed.matrix[3][3].c_str());
}

void test_without_a_matrix_the_export_is_empty_not_wrong(void) {
  // the simulator case
  fakes::clearKeypadMatrix();
  configModel::KeysConfig config = configExport::keys();
  TEST_ASSERT_EQUAL_STRING("", config.matrix[3][3].c_str());
}

// --- the whole dump ----------------------------------------------------------

void test_the_dump_marks_every_file_with_its_path(void) {
  // a host side script has to be able to cut the files back out of a serial log
  registerTestScene();

  std::vector<configExport::DeviceSelector> devices;
  configExport::DeviceSelector selector;
  selector.deviceId = "testdev";
  selector.namePrefix = "TESTDEV_";
  devices.push_back(selector);

  std::string dump = configExport::dumpConfigAsJson(devices);

  TEST_ASSERT_TRUE(dump.find("===== BEGIN /cfg/system.json =====") != std::string::npos);
  TEST_ASSERT_TRUE(dump.find("===== END /cfg/system.json =====") != std::string::npos);
  TEST_ASSERT_TRUE(dump.find("===== BEGIN /cfg/devices/testdev.json =====") != std::string::npos);
  TEST_ASSERT_TRUE(dump.find("===== BEGIN /cfg/scenes.json =====") != std::string::npos);
  TEST_ASSERT_TRUE(dump.find("===== BEGIN /cfg/keys.json =====") != std::string::npos);
}

void test_a_device_without_commands_is_left_out_of_the_dump(void) {
  std::vector<configExport::DeviceSelector> devices;
  configExport::DeviceSelector selector;
  selector.deviceId = "leer";
  selector.namePrefix = "NOTHING_MATCHES_THIS_";
  devices.push_back(selector);

  std::string dump = configExport::dumpConfigAsJson(devices);
  TEST_ASSERT_TRUE(dump.find("/cfg/devices/leer.json") == std::string::npos);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_prefix_selects_only_the_commands_of_that_device);
  RUN_TEST(test_an_exported_device_can_be_imported_again);
  RUN_TEST(test_exporting_a_scene_fills_its_key_maps_first);
  RUN_TEST(test_scene_keys_are_exported_with_names_not_ids);
  RUN_TEST(test_an_exported_scene_can_be_imported_again);
  RUN_TEST(test_a_long_command_without_shortorlong_is_dropped_not_exported);
  RUN_TEST(test_a_scene_with_sequences_is_reported_as_incomplete);
  RUN_TEST(test_a_scene_without_sequences_is_not_reported);
  RUN_TEST(test_the_matrix_is_exported_with_key_names);
  RUN_TEST(test_an_exported_matrix_can_be_imported_again);
  RUN_TEST(test_without_a_matrix_the_export_is_empty_not_wrong);
  RUN_TEST(test_the_dump_marks_every_file_with_its_path);
  RUN_TEST(test_a_device_without_commands_is_left_out_of_the_dump);
  return UNITY_END();
}
