/*
  Unit tests for applicationInternal/keys.cpp

  keypad_loop() reads the raw key matrix, adds the HOLD state and turns that
  into commands. The fake clock makes hold (500 ms) and repeat rate (125 ms)
  deterministic - no sleeping, no hardware.

  This is the behaviour the visual key mapper (phase 4, step 22) will expose
  in the browser, so it needs to be nailed down first.
*/

#include <unity.h>

#include <map>
#include <string>

#include "applicationInternal/commandHandler.h"
#include "applicationInternal/keys.h"
#include "applicationInternal/scenes/sceneRegistry.h"
#include "scenes/scene__default.h"
#include "omote_fakes.h"

static uint16_t CMD_SHORT;
static uint16_t CMD_REPEATED;
static uint16_t CMD_SHORT_OF_LONGKEY;
static uint16_t CMD_LONG_OF_LONGKEY;

static std::map<char, repeatModes> repeatModes_scene;
static std::map<char, uint16_t> commandsShort_scene;
static std::map<char, uint16_t> commandsLong_scene;

static void keyTestScene_setKeys(void) {}
static void scene_noop(void) {}

// where the keys sit in the 5x5 matrix for these tests
static const uint8_t ROW_SHORT = 0, COL_SHORT = 0;
static const uint8_t ROW_REPEAT = 1, COL_REPEAT = 1;
static const uint8_t ROW_LONG = 2, COL_LONG = 2;

void setUp(void) {
  fakes::reset();

  register_command(&CMD_SHORT, makeCommandData(IR, {"7", "0x11"}));
  register_command(&CMD_REPEATED, makeCommandData(IR, {"7", "0x22"}));
  register_command(&CMD_SHORT_OF_LONGKEY, makeCommandData(IR, {"7", "0x33"}));
  register_command(&CMD_LONG_OF_LONGKEY, makeCommandData(IR, {"7", "0x44"}));

  repeatModes_scene = {
      {KEY_PLAY, SHORT},
      {KEY_VOLUP, SHORT_REPEATED},
      {KEY_FORW, SHORTorLONG},
  };
  commandsShort_scene = {
      {KEY_PLAY, CMD_SHORT},
      {KEY_VOLUP, CMD_REPEATED},
      {KEY_FORW, CMD_SHORT_OF_LONGKEY},
  };
  commandsLong_scene = {
      {KEY_FORW, CMD_LONG_OF_LONGKEY},
  };
  key_repeatModes_default = {};
  key_commands_short_default = {};
  key_commands_long_default = {};

  register_scene("keyTestScene", &keyTestScene_setKeys, &scene_noop, &scene_noop, &repeatModes_scene,
                 &commandsShort_scene, &commandsLong_scene);
  fakes::setActiveSceneName("keyTestScene");
  fakes::setActiveGUIname("");
}

void tearDown(void) {}

static size_t sentCommandsWithPayload(const char *payload) {
  size_t count = 0;
  for (const auto &send : fakes::irSends) {
    if (!send.payloads.empty() && send.payloads.front() == payload) count++;
  }
  return count;
}

// --- SHORT ------------------------------------------------------------------

void test_short_key_sends_exactly_once_while_held(void) {
  fakes::pressKey(ROW_SHORT, COL_SHORT, KEY_PLAY);
  keypad_loop();
  TEST_ASSERT_EQUAL_size_t(1, sentCommandsWithPayload("0x11"));

  // keep it pressed well beyond the hold time - still only one command
  for (int i = 0; i < 10; i++) {
    fakes::advanceMillis(200);
    keypad_loop();
  }
  TEST_ASSERT_EQUAL_size_t(1, sentCommandsWithPayload("0x11"));

  fakes::releaseKey(ROW_SHORT, COL_SHORT);
  keypad_loop();
  TEST_ASSERT_EQUAL_size_t(1, sentCommandsWithPayload("0x11"));
}

void test_short_key_sends_again_after_release_and_press(void) {
  fakes::pressKey(ROW_SHORT, COL_SHORT, KEY_PLAY);
  keypad_loop();
  fakes::advanceMillis(50);
  fakes::releaseKey(ROW_SHORT, COL_SHORT);
  keypad_loop();

  // beyond the 125 ms rate limit
  fakes::advanceMillis(200);
  fakes::pressKey(ROW_SHORT, COL_SHORT, KEY_PLAY);
  keypad_loop();

  TEST_ASSERT_EQUAL_size_t(2, sentCommandsWithPayload("0x11"));
}

void test_short_key_is_rate_limited(void) {
  fakes::pressKey(ROW_SHORT, COL_SHORT, KEY_PLAY);
  keypad_loop();
  fakes::advanceMillis(20);
  fakes::releaseKey(ROW_SHORT, COL_SHORT);
  keypad_loop();
  fakes::advanceMillis(20); // still inside the 125 ms window
  fakes::pressKey(ROW_SHORT, COL_SHORT, KEY_PLAY);
  keypad_loop();

  TEST_ASSERT_EQUAL_size_t(1, sentCommandsWithPayload("0x11"));
}

// --- SHORT_REPEATED ---------------------------------------------------------

void test_repeated_key_repeats_while_held(void) {
  fakes::pressKey(ROW_REPEAT, COL_REPEAT, KEY_VOLUP);
  keypad_loop();
  TEST_ASSERT_EQUAL_size_t(1, sentCommandsWithPayload("0x22"));

  // hold for 1 s, polling every 150 ms (above the 125 ms repeat rate)
  for (int i = 0; i < 6; i++) {
    fakes::advanceMillis(150);
    keypad_loop();
  }

  TEST_ASSERT_GREATER_THAN_size_t(2, sentCommandsWithPayload("0x22"));
}

// --- SHORTorLONG ------------------------------------------------------------

void test_shortorlong_key_sends_short_command_on_release(void) {
  fakes::pressKey(ROW_LONG, COL_LONG, KEY_FORW);
  keypad_loop();
  // nothing yet - we do not know if it becomes a long press
  TEST_ASSERT_EQUAL_size_t(0, sentCommandsWithPayload("0x33"));

  fakes::advanceMillis(100);
  fakes::releaseKey(ROW_LONG, COL_LONG);
  keypad_loop();

  TEST_ASSERT_EQUAL_size_t(1, sentCommandsWithPayload("0x33"));
  TEST_ASSERT_EQUAL_size_t(0, sentCommandsWithPayload("0x44"));
}

void test_shortorlong_key_sends_long_command_when_held(void) {
  fakes::pressKey(ROW_LONG, COL_LONG, KEY_FORW);
  keypad_loop();

  fakes::advanceMillis(600); // beyond KEY_HOLD_TIME of 500 ms
  keypad_loop();

  TEST_ASSERT_EQUAL_size_t(1, sentCommandsWithPayload("0x44"));

  // releasing after a long press must not additionally fire the short command
  fakes::releaseKey(ROW_LONG, COL_LONG);
  keypad_loop();
  TEST_ASSERT_EQUAL_size_t(0, sentCommandsWithPayload("0x33"));
  TEST_ASSERT_EQUAL_size_t(1, sentCommandsWithPayload("0x44"));
}

void test_long_command_is_sent_only_once_while_held(void) {
  fakes::pressKey(ROW_LONG, COL_LONG, KEY_FORW);
  keypad_loop();
  for (int i = 0; i < 10; i++) {
    fakes::advanceMillis(200);
    keypad_loop();
  }
  TEST_ASSERT_EQUAL_size_t(1, sentCommandsWithPayload("0x44"));
}

// --- unmapped keys ----------------------------------------------------------

void test_key_without_command_sends_nothing(void) {
  fakes::pressKey(4, 4, KEY_BLUE);
  keypad_loop();
  fakes::advanceMillis(600);
  keypad_loop();
  fakes::releaseKey(4, 4);
  keypad_loop();

  TEST_ASSERT_EQUAL_size_t(0, fakes::irSends.size());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_short_key_sends_exactly_once_while_held);
  RUN_TEST(test_short_key_sends_again_after_release_and_press);
  RUN_TEST(test_short_key_is_rate_limited);
  RUN_TEST(test_repeated_key_repeats_while_held);
  RUN_TEST(test_shortorlong_key_sends_short_command_on_release);
  RUN_TEST(test_shortorlong_key_sends_long_command_when_held);
  RUN_TEST(test_long_command_is_sent_only_once_while_held);
  RUN_TEST(test_key_without_command_sends_nothing);
  return UNITY_END();
}
