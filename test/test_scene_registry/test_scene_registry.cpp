/*
  Unit tests for applicationInternal/scenes/sceneRegistry.cpp

  The interesting part is the lookup chain for a key:
    active GUI  ->  active scene  ->  default keys  ->  COMMAND_UNKNOWN
  Phase 1 must keep this chain intact when scenes come from JSON.
*/

#include <unity.h>

#include <map>
#include <string>

#include "applicationInternal/commandHandler.h"
#include "applicationInternal/keys.h"
#include "applicationInternal/scenes/sceneRegistry.h"
#include "applicationInternal/gui/guiRegistry.h"
#include "scenes/scene__default.h"
#include "omote_fakes.h"

static uint16_t CMD_SCENE_KEY;
static uint16_t CMD_DEFAULT_KEY;
static uint16_t CMD_GUI_KEY;
static uint16_t CMD_ACTIVATE_SCENE;

static std::map<char, repeatModes> repeatModes_testScene;
static std::map<char, uint16_t> commandsShort_testScene;
static std::map<char, uint16_t> commandsLong_testScene;
static t_gui_list guiList_testScene;

static bool startSequenceRan;
static bool endSequenceRan;
static void testScene_setKeys(void) {}
static void testScene_start(void) { startSequenceRan = true; }
static void testScene_end(void) { endSequenceRan = true; }

void setUp(void) {
  fakes::reset();
  startSequenceRan = false;
  endSequenceRan = false;

  register_command(&CMD_SCENE_KEY, makeCommandData(IR, {"7", "0x01"}));
  register_command(&CMD_DEFAULT_KEY, makeCommandData(IR, {"7", "0x02"}));
  register_command(&CMD_GUI_KEY, makeCommandData(IR, {"7", "0x03"}));
  register_command(&CMD_ACTIVATE_SCENE, makeCommandData(SCENE, {"testScene"}));

  repeatModes_testScene = {{KEY_PLAY, SHORT}, {KEY_VOLUP, SHORT_REPEATED}};
  commandsShort_testScene = {{KEY_PLAY, CMD_SCENE_KEY}};
  commandsLong_testScene = {{KEY_PLAY, CMD_SCENE_KEY}};
  guiList_testScene = {"sceneGui"};

  key_repeatModes_default = {{KEY_OFF, SHORT}};
  key_commands_short_default = {{KEY_OFF, CMD_DEFAULT_KEY}};
  key_commands_long_default = {};
  main_gui_list = {"mainGui"};

  register_scene("testScene", &testScene_setKeys, &testScene_start, &testScene_end,
                 &repeatModes_testScene, &commandsShort_testScene, &commandsLong_testScene,
                 &guiList_testScene, CMD_ACTIVATE_SCENE);
  fakes::setActiveSceneName("testScene");
  fakes::setActiveGUIname("");
}

void tearDown(void) {}

void test_registered_scene_is_found(void) {
  TEST_ASSERT_TRUE(sceneExists("testScene"));
  TEST_ASSERT_FALSE(sceneExists("noSuchScene"));
}

void test_start_and_end_sequence_are_called(void) {
  scene_start_sequence_from_registry("testScene");
  TEST_ASSERT_TRUE(startSequenceRan);
  scene_end_sequence_from_registry("testScene");
  TEST_ASSERT_TRUE(endSequenceRan);
}

void test_unknown_scene_sequence_does_not_crash(void) {
  scene_start_sequence_from_registry("noSuchScene");
  TEST_ASSERT_FALSE(startSequenceRan);
}

void test_key_of_scene_is_preferred_over_default(void) {
  TEST_ASSERT_EQUAL_UINT16(CMD_SCENE_KEY, get_command_short("testScene", KEY_PLAY));
  TEST_ASSERT_EQUAL(SHORT, get_key_repeatMode("testScene", KEY_PLAY));
}

void test_default_key_is_used_when_scene_has_none(void) {
  TEST_ASSERT_EQUAL_UINT16(CMD_DEFAULT_KEY, get_command_short("testScene", KEY_OFF));
}

void test_unmapped_key_returns_command_unknown(void) {
  TEST_ASSERT_EQUAL_UINT16(COMMAND_UNKNOWN, get_command_short("testScene", KEY_BLUE));
  TEST_ASSERT_EQUAL_UINT16(COMMAND_UNKNOWN, get_command_long("testScene", KEY_BLUE));
}

void test_active_gui_overrides_scene_key(void) {
  static std::map<char, repeatModes> repeatModes_gui = {{KEY_PLAY, SHORT_REPEATED}};
  static std::map<char, uint16_t> commandsShort_gui = {{KEY_PLAY, CMD_GUI_KEY}};
  static std::map<char, uint16_t> commandsLong_gui = {};
  register_gui("testGui", NULL, NULL, NULL, &repeatModes_gui, &commandsShort_gui, &commandsLong_gui);
  fakes::setActiveGUIname("testGui");

  TEST_ASSERT_EQUAL_UINT16(CMD_GUI_KEY, get_command_short("testScene", KEY_PLAY));
  TEST_ASSERT_EQUAL(SHORT_REPEATED, get_key_repeatMode("testScene", KEY_PLAY));
}

void test_scene_gui_list_is_used_and_falls_back_to_main(void) {
  TEST_ASSERT_TRUE(get_scene_has_gui_list("testScene"));
  gui_list sceneList = get_gui_list_withFallback(SCENE_GUI_LIST);
  TEST_ASSERT_EQUAL_STRING("sceneGui", sceneList->at(0).c_str());

  gui_list mainList = get_gui_list_withFallback(MAIN_GUI_LIST);
  TEST_ASSERT_EQUAL_STRING("mainGui", mainList->at(0).c_str());

  fakes::setActiveSceneName("noSuchScene");
  gui_list fallbackList = get_gui_list_withFallback(SCENE_GUI_LIST);
  TEST_ASSERT_EQUAL_STRING("mainGui", fallbackList->at(0).c_str());
}

void test_activate_scene_command_is_returned(void) {
  TEST_ASSERT_EQUAL_UINT16(CMD_ACTIVATE_SCENE, get_activate_scene_command("testScene"));
  TEST_ASSERT_EQUAL_UINT16(0, get_activate_scene_command("noSuchScene"));
}

void test_scene_selection_list_can_be_overridden(void) {
  set_scenes_on_sceneSelectionGUI({"a", "b"});
  scene_list scenes = get_scenes_on_sceneSelectionGUI();
  TEST_ASSERT_EQUAL_size_t(2, scenes->size());
  TEST_ASSERT_EQUAL_STRING("a", scenes->at(0).c_str());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_registered_scene_is_found);
  RUN_TEST(test_start_and_end_sequence_are_called);
  RUN_TEST(test_unknown_scene_sequence_does_not_crash);
  RUN_TEST(test_key_of_scene_is_preferred_over_default);
  RUN_TEST(test_default_key_is_used_when_scene_has_none);
  RUN_TEST(test_unmapped_key_returns_command_unknown);
  RUN_TEST(test_active_gui_overrides_scene_key);
  RUN_TEST(test_scene_gui_list_is_used_and_falls_back_to_main);
  RUN_TEST(test_activate_scene_command_is_returned);
  RUN_TEST(test_scene_selection_list_can_be_overridden);
  return UNITY_END();
}
