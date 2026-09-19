/*
  Unit tests for /cfg/scenes.json and /cfg/keys.json (configScenes.cpp) and for
  the stable key names they build on (keyNames.cpp).

  Both files describe things that are painful to debug once they are on the
  device: a scene whose long press never fires, a keypad whose rows are mirrored
  because the file came from another hardware revision. Everything that can be
  caught while reading the file is caught here, by name.
*/

#include <unity.h>

#include <string>

#include "applicationInternal/keyNames.h"
#include "applicationInternal/storage/configScenes.h"

void setUp(void) {}
void tearDown(void) {}

// --- key names ---------------------------------------------------------------

void test_key_names_map_both_ways(void) {
  char character = 0;
  TEST_ASSERT_TRUE(keyNames::charFromName("KEY_OK", character));
  TEST_ASSERT_EQUAL_CHAR('k', character);
  TEST_ASSERT_EQUAL_STRING("KEY_OK", keyNames::nameFromChar('k').c_str());

  TEST_ASSERT_TRUE(keyNames::charFromName("KEY_VOLUP", character));
  TEST_ASSERT_EQUAL_CHAR('+', character);
}

void test_every_named_key_round_trips(void) {
  std::vector<keyNames::KeyName> keys = keyNames::all();
  TEST_ASSERT_EQUAL_size_t(24, keys.size());

  for (size_t i = 0; i < keys.size(); i++) {
    char character = 0;
    TEST_ASSERT_TRUE(keyNames::charFromName(keys[i].name, character));
    TEST_ASSERT_EQUAL_CHAR(keys[i].character, character);
    TEST_ASSERT_EQUAL_STRING(keys[i].name.c_str(), keyNames::nameFromChar(character).c_str());
  }
}

void test_no_two_keys_share_a_character(void) {
  std::vector<keyNames::KeyName> keys = keyNames::all();
  for (size_t i = 0; i < keys.size(); i++) {
    for (size_t j = i + 1; j < keys.size(); j++) {
      TEST_ASSERT_NOT_EQUAL_MESSAGE(keys[i].character, keys[j].character,
                                    "two keys map to the same character");
    }
  }
}

void test_an_unnamed_character_returns_an_empty_name(void) {
  // '?' sits on a position of the matrix that has no key name
  TEST_ASSERT_EQUAL_STRING("", keyNames::nameFromChar('?').c_str());

  char character = 0;
  TEST_ASSERT_FALSE(keyNames::charFromName("KEY_DOES_NOT_EXIST", character));
}

// --- scenes ------------------------------------------------------------------

static configModel::ScenesConfig exampleScenes() {
  configModel::SceneConfig scene;
  scene.name = "TV";
  scene.activateCommand = "SCENE_TV";
  scene.guiList.push_back("Numpad");

  configModel::SceneKeyBinding up;
  up.keyName = "KEY_UP";
  up.repeatMode = SHORT_REPEATED;
  up.commandShort = "SAMSUNG_UP";
  scene.keys.push_back(up);

  configModel::SceneKeyBinding ok;
  ok.keyName = "KEY_OK";
  ok.repeatMode = SHORTorLONG;
  ok.commandShort = "SAMSUNG_SELECT";
  ok.commandLong = "SAMSUNG_MENU";
  scene.keys.push_back(ok);

  configModel::SequenceStep powerOn;
  powerOn.commandName = "SAMSUNG_POWER_ON";
  powerOn.delayAfterMs = 500;
  scene.startSequence.push_back(powerOn);

  configModel::SequenceStep input;
  input.commandName = "YAMAHA_INPUT_DVD";
  input.payload = "DVD";
  input.delayAfterMs = 3000;
  scene.startSequence.push_back(input);

  configModel::ScenesConfig config;
  config.scenes.push_back(scene);
  return config;
}

void test_scenes_survive_serialize_and_parse(void) {
  configModel::ScenesConfig written = exampleScenes();
  std::string json = configModel::serializeScenes(written);

  configModel::ScenesConfig read;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseScenes(json, read, error), error.c_str());

  TEST_ASSERT_EQUAL_size_t(1, read.scenes.size());
  TEST_ASSERT_EQUAL_STRING("TV", read.scenes[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("SCENE_TV", read.scenes[0].activateCommand.c_str());
  TEST_ASSERT_EQUAL_size_t(1, read.scenes[0].guiList.size());
  TEST_ASSERT_EQUAL_STRING("Numpad", read.scenes[0].guiList[0].c_str());
  TEST_ASSERT_EQUAL_size_t(2, read.scenes[0].keys.size());
  TEST_ASSERT_EQUAL_size_t(2, read.scenes[0].startSequence.size());
  TEST_ASSERT_EQUAL_size_t(0, read.scenes[0].endSequence.size());
}

void test_sequence_keeps_payload_and_delay(void) {
  std::string json = configModel::serializeScenes(exampleScenes());

  configModel::ScenesConfig read;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseScenes(json, read, error), error.c_str());

  const configModel::SequenceStep &first = read.scenes[0].startSequence[0];
  TEST_ASSERT_EQUAL_STRING("SAMSUNG_POWER_ON", first.commandName.c_str());
  TEST_ASSERT_EQUAL_UINT32(500, first.delayAfterMs);

  const configModel::SequenceStep &second = read.scenes[0].startSequence[1];
  TEST_ASSERT_EQUAL_STRING("YAMAHA_INPUT_DVD", second.commandName.c_str());
  TEST_ASSERT_EQUAL_STRING("DVD", second.payload.c_str());
  TEST_ASSERT_EQUAL_UINT32(3000, second.delayAfterMs);
}

void test_repeat_modes_round_trip_by_name(void) {
  repeatModes mode = SHORT;
  TEST_ASSERT_TRUE(configModel::repeatModeFromString("SHORT_REPEATED", mode));
  TEST_ASSERT_EQUAL_INT((int)SHORT_REPEATED, (int)mode);
  TEST_ASSERT_TRUE(configModel::repeatModeFromString("SHORTorLONG", mode));
  TEST_ASSERT_EQUAL_INT((int)SHORTorLONG, (int)mode);

  TEST_ASSERT_EQUAL_STRING("SHORT", configModel::repeatModeToString(SHORT).c_str());
  TEST_ASSERT_EQUAL_STRING("SHORTorLONG", configModel::repeatModeToString(SHORTorLONG).c_str());
}

void test_json_references_keys_and_commands_by_name(void) {
  std::string json = configModel::serializeScenes(exampleScenes());
  TEST_ASSERT_TRUE(json.find("KEY_OK") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("SAMSUNG_SELECT") != std::string::npos);
  // the char behind KEY_OK must not leak into the file
  TEST_ASSERT_TRUE(json.find("\"k\"") == std::string::npos);
}

void test_a_scene_without_keys_or_sequence_is_valid(void) {
  // scene_allOff has no key map worth mentioning, and a scene that only exists
  // to be activated is legitimate
  std::string json = "{\"schemaVersion\":1,\"type\":\"omote.scenes\","
                     "\"scenes\":[{\"name\":\"leer\"}]}";

  configModel::ScenesConfig config;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseScenes(json, config, error), error.c_str());
  TEST_ASSERT_EQUAL_size_t(1, config.scenes.size());
  TEST_ASSERT_EQUAL_size_t(0, config.scenes[0].keys.size());
}

// --- scenes: rejection -------------------------------------------------------

static void assertScenesRejected(const std::string &json, const char *expectedTextInError) {
  configModel::ScenesConfig config;
  std::string error;
  TEST_ASSERT_FALSE(configModel::parseScenes(json, config, error));
  TEST_ASSERT_FALSE(error.empty());
  if (expectedTextInError != NULL) {
    TEST_ASSERT_TRUE_MESSAGE(error.find(expectedTextInError) != std::string::npos, error.c_str());
  }
}

void test_an_unknown_key_is_rejected_by_name(void) {
  assertScenesRejected("{\"schemaVersion\":1,\"type\":\"omote.scenes\",\"scenes\":[{\"name\":\"TV\","
                       "\"keys\":{\"KEY_TURBO\":{\"short\":\"X\"}}}]}",
                       "KEY_TURBO");
}

void test_an_unknown_repeat_mode_is_rejected_by_name(void) {
  assertScenesRejected("{\"schemaVersion\":1,\"type\":\"omote.scenes\",\"scenes\":[{\"name\":\"TV\","
                       "\"keys\":{\"KEY_OK\":{\"repeatMode\":\"SOMETIMES\",\"short\":\"X\"}}}]}",
                       "SOMETIMES");
}

void test_a_long_command_without_shortorlong_is_rejected(void) {
  // otherwise the long press is configured but can never fire, and the user is
  // left wondering why
  assertScenesRejected("{\"schemaVersion\":1,\"type\":\"omote.scenes\",\"scenes\":[{\"name\":\"TV\","
                       "\"keys\":{\"KEY_OK\":{\"repeatMode\":\"SHORT\",\"short\":\"X\","
                       "\"long\":\"Y\"}}}]}",
                       "SHORTorLONG");
}

void test_a_duplicate_scene_name_is_rejected(void) {
  assertScenesRejected("{\"schemaVersion\":1,\"type\":\"omote.scenes\","
                       "\"scenes\":[{\"name\":\"TV\"},{\"name\":\"TV\"}]}",
                       "twice");
}

void test_a_sequence_step_without_a_command_is_rejected(void) {
  assertScenesRejected("{\"schemaVersion\":1,\"type\":\"omote.scenes\",\"scenes\":[{\"name\":\"TV\","
                       "\"startSequence\":[{\"delayAfterMs\":500}]}]}",
                       "start sequence");
}

void test_a_scenes_file_is_not_a_keys_file(void) {
  assertScenesRejected("{\"schemaVersion\":1,\"type\":\"omote.keys\",\"matrix\":[]}", "omote.scenes");
}

// --- keypad matrix -----------------------------------------------------------

static std::string validMatrixJson(const char *hardwareRev) {
  // the Rev5 layout from keypad_keys_hal_esp32.cpp
  return std::string("{\"schemaVersion\":1,\"type\":\"omote.keys\",\"hardwareRev\":") + hardwareRev +
         ",\"matrix\":["
         "[\"\",\"KEY_PLAY\",\"KEY_CONF\",\"KEY_REWI\",\"KEY_STOP\"],"
         "[\"KEY_FORW\",\"KEY_OFF\",\"KEY_BACK\",\"KEY_UP\",\"KEY_LEFT\"],"
         "[\"KEY_BLUE\",\"KEY_CHDOW\",\"KEY_RED\",\"KEY_YELLO\",\"KEY_GREEN\"],"
         "[\"KEY_INFO\",\"KEY_RIGHT\",\"KEY_VOLUP\",\"KEY_OK\",\"KEY_DOWN\"],"
         "[\"KEY_SRC\",\"KEY_CHUP\",\"KEY_VOLDO\",\"KEY_MUTE\",\"KEY_REC\"]]}";
}

void test_keys_survive_serialize_and_parse(void) {
  configModel::KeysConfig written;
  written.hardwareRev = configModel::thisHardwareRevision();
  written.matrix[0][1] = "KEY_PLAY";
  written.matrix[3][3] = "KEY_OK";

  std::string json = configModel::serializeKeys(written);

  configModel::KeysConfig read;
  bool mismatch = true;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseKeys(json, read, mismatch, error), error.c_str());
  TEST_ASSERT_FALSE(mismatch);
  TEST_ASSERT_EQUAL_STRING("KEY_PLAY", read.matrix[0][1].c_str());
  TEST_ASSERT_EQUAL_STRING("KEY_OK", read.matrix[3][3].c_str());
  TEST_ASSERT_EQUAL_STRING("", read.matrix[0][0].c_str());
}

void test_the_real_matrix_is_accepted(void) {
  configModel::KeysConfig config;
  bool mismatch = true;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseKeys(validMatrixJson("5"), config, mismatch, error),
                           error.c_str());
  TEST_ASSERT_EQUAL_STRING("KEY_OK", config.matrix[3][3].c_str());
}

void test_an_empty_position_is_allowed(void) {
  // the Rev5 matrix really does have a position without a key
  configModel::KeysConfig config;
  bool mismatch = false;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseKeys(validMatrixJson("5"), config, mismatch, error),
                           error.c_str());
  TEST_ASSERT_EQUAL_STRING("", config.matrix[0][0].c_str());
}

void test_a_foreign_hardware_revision_is_reported_not_refused(void) {
  // Rev5 and Rev1-4 have the same keys in reversed row order. Importing the
  // wrong file mirrors the keypad, so the caller has to be told.
  configModel::KeysConfig config;
  bool mismatch = false;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseKeys(validMatrixJson("4"), config, mismatch, error),
                           error.c_str());
  TEST_ASSERT_TRUE(mismatch);
}

void test_a_file_without_a_revision_is_accepted_quietly(void) {
  configModel::KeysConfig config;
  bool mismatch = true;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseKeys(validMatrixJson("0"), config, mismatch, error),
                           error.c_str());
  TEST_ASSERT_FALSE(mismatch);
}

static void assertKeysRejected(const std::string &json, const char *expectedTextInError) {
  configModel::KeysConfig config;
  bool mismatch = false;
  std::string error;
  TEST_ASSERT_FALSE(configModel::parseKeys(json, config, mismatch, error));
  TEST_ASSERT_FALSE(error.empty());
  if (expectedTextInError != NULL) {
    TEST_ASSERT_TRUE_MESSAGE(error.find(expectedTextInError) != std::string::npos, error.c_str());
  }
}

void test_too_few_rows_are_rejected(void) {
  assertKeysRejected("{\"schemaVersion\":1,\"type\":\"omote.keys\",\"matrix\":[[\"\",\"\",\"\",\"\",\"\"]]}",
                     "rows");
}

void test_a_short_row_is_rejected_with_its_number(void) {
  assertKeysRejected("{\"schemaVersion\":1,\"type\":\"omote.keys\",\"matrix\":["
                     "[\"\",\"\",\"\",\"\",\"\"],[\"\",\"\"],[\"\",\"\",\"\",\"\",\"\"],"
                     "[\"\",\"\",\"\",\"\",\"\"],[\"\",\"\",\"\",\"\",\"\"]]}",
                     "row 1");
}

void test_an_unknown_key_in_the_matrix_is_rejected(void) {
  assertKeysRejected("{\"schemaVersion\":1,\"type\":\"omote.keys\",\"matrix\":["
                     "[\"KEY_NOPE\",\"\",\"\",\"\",\"\"],[\"\",\"\",\"\",\"\",\"\"],"
                     "[\"\",\"\",\"\",\"\",\"\"],[\"\",\"\",\"\",\"\",\"\"],"
                     "[\"\",\"\",\"\",\"\",\"\"]]}",
                     "KEY_NOPE");
}

void test_the_same_key_on_two_positions_is_rejected(void) {
  assertKeysRejected("{\"schemaVersion\":1,\"type\":\"omote.keys\",\"matrix\":["
                     "[\"KEY_OK\",\"\",\"\",\"\",\"\"],[\"\",\"\",\"\",\"KEY_OK\",\"\"],"
                     "[\"\",\"\",\"\",\"\",\"\"],[\"\",\"\",\"\",\"\",\"\"],"
                     "[\"\",\"\",\"\",\"\",\"\"]]}",
                     "twice");
}

void test_a_missing_matrix_is_rejected(void) {
  assertKeysRejected("{\"schemaVersion\":1,\"type\":\"omote.keys\"}", "matrix");
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_key_names_map_both_ways);
  RUN_TEST(test_every_named_key_round_trips);
  RUN_TEST(test_no_two_keys_share_a_character);
  RUN_TEST(test_an_unnamed_character_returns_an_empty_name);
  RUN_TEST(test_scenes_survive_serialize_and_parse);
  RUN_TEST(test_sequence_keeps_payload_and_delay);
  RUN_TEST(test_repeat_modes_round_trip_by_name);
  RUN_TEST(test_json_references_keys_and_commands_by_name);
  RUN_TEST(test_a_scene_without_keys_or_sequence_is_valid);
  RUN_TEST(test_an_unknown_key_is_rejected_by_name);
  RUN_TEST(test_an_unknown_repeat_mode_is_rejected_by_name);
  RUN_TEST(test_a_long_command_without_shortorlong_is_rejected);
  RUN_TEST(test_a_duplicate_scene_name_is_rejected);
  RUN_TEST(test_a_sequence_step_without_a_command_is_rejected);
  RUN_TEST(test_a_scenes_file_is_not_a_keys_file);
  RUN_TEST(test_keys_survive_serialize_and_parse);
  RUN_TEST(test_the_real_matrix_is_accepted);
  RUN_TEST(test_an_empty_position_is_allowed);
  RUN_TEST(test_a_foreign_hardware_revision_is_reported_not_refused);
  RUN_TEST(test_a_file_without_a_revision_is_accepted_quietly);
  RUN_TEST(test_too_few_rows_are_rejected);
  RUN_TEST(test_a_short_row_is_rejected_with_its_number);
  RUN_TEST(test_an_unknown_key_in_the_matrix_is_rejected);
  RUN_TEST(test_the_same_key_on_two_positions_is_rejected);
  RUN_TEST(test_a_missing_matrix_is_rejected);
  return UNITY_END();
}
