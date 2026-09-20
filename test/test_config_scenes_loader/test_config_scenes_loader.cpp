/*
  Unit tests for configLoader::loadScenes() and loadKeys().

  These close the last gap of phase 1: until now scenes.json and keys.json were
  a format with a parser and no effect. What matters here is not that a scene
  loads - it is what happens when it names something the device does not have,
  which is the normal state of a file somebody moved between two remotes.
*/

#include <unity.h>

#include <string>

#include "applicationInternal/bootGuard.h"
#include "applicationInternal/commandHandler.h"
#include "applicationInternal/hardware/IRremoteProtocols.h"
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/gui/guiRegistry.h"
#include "applicationInternal/keyNames.h"
#include "applicationInternal/scenes/sceneRegistry.h"
#include "applicationInternal/scenes/sequenceEngine.h"
#include "applicationInternal/storage/configFile.h"
#include "applicationInternal/storage/configLoader.h"
#include "applicationInternal/storage/configStorage.h"
#include "scenes/scene__default.h"
#include "fake_filesystem.h"
#include "omote_fakes.h"

static FakeFileSystem fileSystem;
static uint16_t TV_POWER;
static uint16_t TV_UP;
static uint16_t TV_MENU;

class NoSafeModeStorage : public BootCounterStorage {
public:
  uint8_t readFailedBoots() override { return 0; }
  void writeFailedBoots(uint8_t) override {}
  bool readSafeModeRequested() override { return false; }
  void writeSafeModeRequested(bool) override {}
};
static NoSafeModeStorage bootStorage;

void setUp(void) {
  fakes::reset();
  fakes::clearKeypadMatrix();
  fileSystem.reset();
  registered_scenes.clear();
  registered_guis_byName_map.clear();
  main_gui_list.clear();
  sequenceEngine::abort();
  configStorage::setFileSystem(&fileSystem);
  bootGuard::begin(&bootStorage);

  register_command(&TV_POWER, makeCommandData(IR, {std::to_string(IR_PROTOCOL_SAMSUNG), "0xAAAA"}));
  register_command(&TV_UP, makeCommandData(IR, {std::to_string(IR_PROTOCOL_SAMSUNG), "0xBBBB"}));
  register_command(&TV_MENU, makeCommandData(IR, {std::to_string(IR_PROTOCOL_SAMSUNG), "0xCCCC"}));
}

void tearDown(void) {
  registered_scenes.clear();
  sequenceEngine::abort();
}

// --- scenes ------------------------------------------------------------------

static const char *const TV_SCENE =
    "{\"schemaVersion\":1,\"type\":\"omote.scenes\",\"scenes\":[{"
    "\"name\":\"Wohnzimmer\","
    "\"guiList\":[\"Numpad\"],"
    "\"keys\":{"
    "  \"KEY_OK\":{\"repeatMode\":\"SHORTorLONG\",\"short\":\"TV_POWER\",\"long\":\"TV_MENU\"},"
    "  \"KEY_UP\":{\"repeatMode\":\"SHORT_REPEATED\",\"short\":\"TV_UP\"}},"
    "\"startSequence\":[{\"command\":\"TV_POWER\",\"delayAfterMs\":500},"
    "                   {\"command\":\"TV_UP\",\"payload\":\"x\"}],"
    "\"endSequence\":[{\"command\":\"TV_MENU\"}]}]}";

void test_without_a_file_nothing_is_registered(void) {
  configLoader::ScenesResult result = configLoader::loadScenes();

  TEST_ASSERT_FALSE(result.fileFound);
  TEST_ASSERT_EQUAL_UINT16(0, result.scenesLoaded);
  TEST_ASSERT_FALSE(sceneExists("Wohnzimmer"));
}

void test_a_scene_from_json_is_registered_with_its_keys(void) {
  fileSystem.files[configFile::PATH_SCENES] = TV_SCENE;

  configLoader::ScenesResult result = configLoader::loadScenes();

  TEST_ASSERT_TRUE_MESSAGE(result.error.empty(), result.error.c_str());
  TEST_ASSERT_EQUAL_UINT16(1, result.scenesLoaded);
  TEST_ASSERT_TRUE(sceneExists("Wohnzimmer"));

  fakes::setActiveSceneName("Wohnzimmer");
  TEST_ASSERT_EQUAL_UINT16(TV_POWER, get_command_short("Wohnzimmer", KEY_OK));
  TEST_ASSERT_EQUAL_UINT16(TV_MENU, get_command_long("Wohnzimmer", KEY_OK));
  TEST_ASSERT_EQUAL_UINT16(TV_UP, get_command_short("Wohnzimmer", KEY_UP));
  TEST_ASSERT_EQUAL_INT((int)SHORTorLONG, (int)get_key_repeatMode("Wohnzimmer", KEY_OK));
}

void test_the_start_sequence_of_a_json_scene_actually_runs(void) {
  /*
    The point of the whole step. A scene out of a file has its steps as a list;
    a function pointer cannot carry one, so the registry holds both and prefers
    the data. From the sequence engine on, a JSON scene and a C++ scene are
    indistinguishable.
  */
  fileSystem.files[configFile::PATH_SCENES] = TV_SCENE;
  configLoader::loadScenes();

  scene_start_sequence_from_registry("Wohnzimmer");
  TEST_ASSERT_EQUAL_size_t(2, sequenceEngine::pendingSteps());

  unsigned long clockMs = 1000;
  for (int i = 0; i < 100; i++) {
    clockMs += 10;
    sequenceEngine::loop(clockMs);
  }

  TEST_ASSERT_EQUAL_size_t(2, fakes::irSends.size());
  TEST_ASSERT_EQUAL_STRING("0xAAAA", fakes::irSends[0].payloads.front().c_str());
  TEST_ASSERT_EQUAL_STRING("0xBBBB", fakes::irSends[1].payloads.front().c_str());
  TEST_ASSERT_EQUAL_STRING("x", fakes::irSends[1].additionalPayload.c_str());
}

void test_the_end_sequence_runs_too(void) {
  fileSystem.files[configFile::PATH_SCENES] = TV_SCENE;
  configLoader::loadScenes();

  scene_end_sequence_from_registry("Wohnzimmer");
  sequenceEngine::loop(2000);

  TEST_ASSERT_EQUAL_size_t(1, fakes::irSends.size());
  TEST_ASSERT_EQUAL_STRING("0xCCCC", fakes::irSends[0].payloads.front().c_str());
}

void test_a_json_scene_replaces_one_of_the_same_name(void) {
  // same rule as for devices: the file wins over what is compiled in
  static std::map<char, repeatModes> modes;
  static std::map<char, uint16_t> shortCommands;
  static std::map<char, uint16_t> longCommands;
  shortCommands[KEY_OK] = TV_MENU;
  register_scene("Wohnzimmer", NULL, NULL, NULL, &modes, &shortCommands, &longCommands, NULL, 0);
  TEST_ASSERT_EQUAL_UINT16(TV_MENU, get_command_short("Wohnzimmer", KEY_OK));

  fileSystem.files[configFile::PATH_SCENES] = TV_SCENE;
  configLoader::loadScenes();

  TEST_ASSERT_EQUAL_UINT16(TV_POWER, get_command_short("Wohnzimmer", KEY_OK));
}

void test_an_unknown_command_costs_only_that_one_step(void) {
  /*
    The normal state of a file moved from another remote. A scene that switches
    the television on and then selects an input this device does not know should
    still switch the television on.
  */
  fileSystem.files[configFile::PATH_SCENES] =
      "{\"schemaVersion\":1,\"type\":\"omote.scenes\",\"scenes\":[{\"name\":\"Teilweise\","
      "\"keys\":{\"KEY_OK\":{\"short\":\"NICHT_VORHANDEN\"}},"
      "\"startSequence\":[{\"command\":\"TV_POWER\"},{\"command\":\"AUCH_NICHT\"}]}]}";

  configLoader::ScenesResult result = configLoader::loadScenes();

  TEST_ASSERT_EQUAL_UINT16(1, result.scenesLoaded);
  TEST_ASSERT_EQUAL_UINT16(2, result.stepsDropped);

  scene_start_sequence_from_registry("Teilweise");
  sequenceEngine::loop(2000);
  TEST_ASSERT_EQUAL_size_t_MESSAGE(1, fakes::irSends.size(), "the known step has to survive");
}

void test_several_scenes_are_all_registered(void) {
  fileSystem.files[configFile::PATH_SCENES] =
      "{\"schemaVersion\":1,\"type\":\"omote.scenes\",\"scenes\":["
      "{\"name\":\"A\",\"startSequence\":[{\"command\":\"TV_POWER\"}]},"
      "{\"name\":\"B\",\"startSequence\":[{\"command\":\"TV_UP\"}]},"
      "{\"name\":\"C\"}]}";

  configLoader::ScenesResult result = configLoader::loadScenes();

  TEST_ASSERT_EQUAL_UINT16(3, result.scenesLoaded);
  TEST_ASSERT_TRUE(sceneExists("A"));
  TEST_ASSERT_TRUE(sceneExists("C"));

  // each scene keeps its own sequence - a shared or overwritten one would only
  // show up here
  scene_start_sequence_from_registry("B");
  sequenceEngine::loop(2000);
  TEST_ASSERT_EQUAL_STRING("0xBBBB", fakes::irSends[0].payloads.front().c_str());
}

void test_a_broken_scenes_file_registers_nothing(void) {
  fileSystem.files[configFile::PATH_SCENES] = "{ not json";

  configLoader::ScenesResult result = configLoader::loadScenes();

  TEST_ASSERT_TRUE(result.fileFound);
  TEST_ASSERT_EQUAL_UINT16(0, result.scenesLoaded);
  TEST_ASSERT_TRUE(result.error.size() > 0);
}

void test_safe_mode_skips_the_scenes(void) {
  class SafeMode : public BootCounterStorage {
  public:
    uint8_t readFailedBoots() override { return 0; }
    void writeFailedBoots(uint8_t) override {}
    bool readSafeModeRequested() override { return true; }
    void writeSafeModeRequested(bool) override {}
  };
  static SafeMode safeMode;
  bootGuard::begin(&safeMode);

  fileSystem.files[configFile::PATH_SCENES] = TV_SCENE;
  configLoader::ScenesResult result = configLoader::loadScenes();

  TEST_ASSERT_EQUAL_UINT16(0, result.scenesLoaded);
  TEST_ASSERT_FALSE(sceneExists("Wohnzimmer"));

  bootGuard::begin(&bootStorage);
}

// --- keys --------------------------------------------------------------------

static std::string keysJson(const char *hardwareRev) {
  return std::string("{\"schemaVersion\":1,\"type\":\"omote.keys\",\"hardwareRev\":") + hardwareRev +
         ",\"matrix\":["
         "[\"\",\"KEY_PLAY\",\"KEY_CONF\",\"KEY_REWI\",\"KEY_STOP\"],"
         "[\"KEY_FORW\",\"KEY_OFF\",\"KEY_BACK\",\"KEY_UP\",\"KEY_LEFT\"],"
         "[\"KEY_BLUE\",\"KEY_CHDOW\",\"KEY_RED\",\"KEY_YELLO\",\"KEY_GREEN\"],"
         "[\"KEY_INFO\",\"KEY_RIGHT\",\"KEY_VOLUP\",\"KEY_OK\",\"KEY_DOWN\"],"
         "[\"KEY_SRC\",\"KEY_CHUP\",\"KEY_VOLDO\",\"KEY_MUTE\",\"KEY_REC\"]]}";
}

// gives the fake a layout to replace, the way a device with a keypad has one
static void withAKeypad() {
  char matrix[keypadROWS][keypadCOLS] = {
      {'?', '?', '?', '?', '?'}, {'?', '?', '?', '?', '?'}, {'?', '?', '?', '?', '?'},
      {'?', '?', '?', '?', '?'}, {'?', '?', '?', '?', '?'},
  };
  fakes::setKeypadMatrix(matrix);
}

void test_a_layout_from_json_replaces_the_one_in_the_driver(void) {
  withAKeypad();
  fileSystem.files[configFile::PATH_KEYS] =
      keysJson(std::to_string(configModel::thisHardwareRevision()).c_str());

  configLoader::KeysResult result = configLoader::loadKeys();

  TEST_ASSERT_TRUE_MESSAGE(result.applied, result.error.c_str());
  char matrix[keypadROWS][keypadCOLS];
  TEST_ASSERT_TRUE(get_keypadMatrix(matrix));
  TEST_ASSERT_EQUAL_CHAR(KEY_OK, matrix[3][3]);
  TEST_ASSERT_EQUAL_CHAR(KEY_PLAY, matrix[0][1]);
}

void test_an_empty_position_becomes_no_key(void) {
  withAKeypad();
  fileSystem.files[configFile::PATH_KEYS] =
      keysJson(std::to_string(configModel::thisHardwareRevision()).c_str());

  configLoader::loadKeys();

  char matrix[keypadROWS][keypadCOLS];
  get_keypadMatrix(matrix);
  TEST_ASSERT_EQUAL_CHAR(NO_KEY, matrix[0][0]);
}

void test_a_file_for_another_revision_is_refused_not_applied(void) {
  /*
    Rev5 and Rev1-4 hold the same keys in reversed row order. Applying the wrong
    file mirrors the keypad, and the user presses "up" to go down with nothing
    in the log to explain it. Refusing is the kinder answer.
  */
  withAKeypad();
  fileSystem.files[configFile::PATH_KEYS] = keysJson("42");

  configLoader::KeysResult result = configLoader::loadKeys();

  TEST_ASSERT_TRUE(result.revisionMismatch);
  TEST_ASSERT_FALSE(result.applied);
  TEST_ASSERT_TRUE(result.error.size() > 0);

  char matrix[keypadROWS][keypadCOLS];
  get_keypadMatrix(matrix);
  TEST_ASSERT_EQUAL_CHAR_MESSAGE('?', matrix[3][3], "the layout must be untouched");
}

void test_without_a_keypad_the_file_is_reported_not_silently_ignored(void) {
  // the simulator case: there is no matrix to replace
  fakes::clearKeypadMatrix();
  fileSystem.files[configFile::PATH_KEYS] =
      keysJson(std::to_string(configModel::thisHardwareRevision()).c_str());

  configLoader::KeysResult result = configLoader::loadKeys();

  TEST_ASSERT_TRUE(result.fileFound);
  TEST_ASSERT_FALSE(result.applied);
  TEST_ASSERT_TRUE(result.error.size() > 0);
}

void test_a_broken_keys_file_leaves_the_layout_alone(void) {
  withAKeypad();
  fileSystem.files[configFile::PATH_KEYS] =
      "{\"schemaVersion\":1,\"type\":\"omote.keys\",\"matrix\":[[\"KEY_OK\",\"\",\"\",\"\",\"\"]]}";

  configLoader::KeysResult result = configLoader::loadKeys();

  TEST_ASSERT_FALSE(result.applied);
  char matrix[keypadROWS][keypadCOLS];
  get_keypadMatrix(matrix);
  TEST_ASSERT_EQUAL_CHAR('?', matrix[3][3]);
}

// --- ui.json ------------------------------------------------------------------

static const char *const ONE_SCREEN =
    "{\"schemaVersion\":1,\"type\":\"omote.ui\",\"screens\":[{"
    "\"name\":\"Numpad\",\"grid\":{\"columns\":3},"
    "\"widgets\":[{\"type\":\"button\",\"row\":0,\"column\":0,\"label\":\"1\","
    "\"command\":\"TV_POWER\"}]}]}";

void test_a_screen_from_json_is_registered(void) {
  fileSystem.files[configFile::PATH_UI] = ONE_SCREEN;

  configLoader::UiResult result = configLoader::loadUi();

  TEST_ASSERT_TRUE_MESSAGE(result.error.empty(), result.error.c_str());
  TEST_ASSERT_EQUAL_UINT16(1, result.screensLoaded);
  TEST_ASSERT_EQUAL_size_t(1, registered_guis_byName_map.count("Numpad"));
}

void test_a_screen_replaces_one_of_the_same_name(void) {
  /*
    How gui_numpad becomes a file somebody can edit without a compiler. The
    registry used to refuse a repeated name outright, because for two screens
    written in C++ it can only be a mistake.
  */
  register_gui("Numpad", NULL, NULL);
  TEST_ASSERT_NULL(registered_guis_byName_map.at("Numpad").this_create_tab_content_named);

  fileSystem.files[configFile::PATH_UI] = ONE_SCREEN;
  configLoader::UiResult result = configLoader::loadUi();

  TEST_ASSERT_EQUAL_UINT16(1, result.screensReplaced);
  // and now it is the file that draws it
  TEST_ASSERT_NOT_NULL(registered_guis_byName_map.at("Numpad").this_create_tab_content_named);
}

void test_a_replaced_screen_is_not_listed_twice(void) {
  // paging through the screens would otherwise show the same one two times
  size_t before = main_gui_list.size();
  register_gui("Numpad", NULL, NULL);
  TEST_ASSERT_EQUAL_size_t(before + 1, main_gui_list.size());

  fileSystem.files[configFile::PATH_UI] = ONE_SCREEN;
  configLoader::loadUi();

  TEST_ASSERT_EQUAL_size_t(before + 1, main_gui_list.size());
}

void test_a_broken_ui_file_registers_nothing(void) {
  fileSystem.files[configFile::PATH_UI] = "{ not json";

  configLoader::UiResult result = configLoader::loadUi();

  TEST_ASSERT_TRUE(result.fileFound);
  TEST_ASSERT_EQUAL_UINT16(0, result.screensLoaded);
  TEST_ASSERT_TRUE(result.error.size() > 0);
}

void test_safe_mode_skips_the_screens(void) {
  class SafeMode : public BootCounterStorage {
  public:
    uint8_t readFailedBoots() override { return 0; }
    void writeFailedBoots(uint8_t) override {}
    bool readSafeModeRequested() override { return true; }
    void writeSafeModeRequested(bool) override {}
  };
  static SafeMode safeMode;
  bootGuard::begin(&safeMode);

  fileSystem.files[configFile::PATH_UI] = ONE_SCREEN;
  configLoader::UiResult result = configLoader::loadUi();

  TEST_ASSERT_EQUAL_UINT16(0, result.screensLoaded);
  bootGuard::begin(&bootStorage);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_screen_from_json_is_registered);
  RUN_TEST(test_a_screen_replaces_one_of_the_same_name);
  RUN_TEST(test_a_replaced_screen_is_not_listed_twice);
  RUN_TEST(test_a_broken_ui_file_registers_nothing);
  RUN_TEST(test_safe_mode_skips_the_screens);
  RUN_TEST(test_without_a_file_nothing_is_registered);
  RUN_TEST(test_a_scene_from_json_is_registered_with_its_keys);
  RUN_TEST(test_the_start_sequence_of_a_json_scene_actually_runs);
  RUN_TEST(test_the_end_sequence_runs_too);
  RUN_TEST(test_a_json_scene_replaces_one_of_the_same_name);
  RUN_TEST(test_an_unknown_command_costs_only_that_one_step);
  RUN_TEST(test_several_scenes_are_all_registered);
  RUN_TEST(test_a_broken_scenes_file_registers_nothing);
  RUN_TEST(test_safe_mode_skips_the_scenes);
  RUN_TEST(test_a_layout_from_json_replaces_the_one_in_the_driver);
  RUN_TEST(test_an_empty_position_becomes_no_key);
  RUN_TEST(test_a_file_for_another_revision_is_refused_not_applied);
  RUN_TEST(test_without_a_keypad_the_file_is_reported_not_silently_ignored);
  RUN_TEST(test_a_broken_keys_file_leaves_the_layout_alone);
  return UNITY_END();
}
