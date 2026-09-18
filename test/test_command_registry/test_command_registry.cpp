/*
  Unit tests for applicationInternal/commandHandler.cpp

  These tests pin down the behaviour that the JSON configuration (phase 1) has
  to reproduce exactly: how commands are registered, which handler they are
  dispatched to and how payloads are passed on.
*/

#include <unity.h>

#include <string>
#include <list>

#include "applicationInternal/commandHandler.h"
#include "applicationInternal/hardware/IRremoteProtocols.h"
#include "omote_fakes.h"

static uint16_t CMD_IR_SAMSUNG_POWER;
static uint16_t CMD_MQTT_BULB;
static uint16_t CMD_SCENE_TV;
static uint16_t CMD_GUI_NUMPAD;

void setUp(void) {
  fakes::reset();
  register_command(&CMD_IR_SAMSUNG_POWER,
                   makeCommandData(IR, {std::to_string(IR_PROTOCOL_SAMSUNG), "0xE0E040BF"}));
  register_command(&CMD_MQTT_BULB, makeCommandData(MQTT, {"bulb1_set", "ON"}));
  register_command(&CMD_SCENE_TV, makeCommandData(SCENE, {"TV"}));
  register_command(&CMD_GUI_NUMPAD, makeCommandData(GUI, {"Numpad"}));
}

void tearDown(void) {}

// --- registration -----------------------------------------------------------

void test_register_command_assigns_unique_ids(void) {
  TEST_ASSERT_NOT_EQUAL(CMD_IR_SAMSUNG_POWER, CMD_MQTT_BULB);
  TEST_ASSERT_NOT_EQUAL(CMD_MQTT_BULB, CMD_SCENE_TV);
  TEST_ASSERT_NOT_EQUAL(CMD_SCENE_TV, CMD_GUI_NUMPAD);
}

void test_get_uniqueCommandID_does_not_collide_with_registered_commands(void) {
  uint16_t idOne = 0;
  uint16_t idTwo = 0;
  get_uniqueCommandID(&idOne);
  get_uniqueCommandID(&idTwo);
  TEST_ASSERT_NOT_EQUAL(idOne, idTwo);
  TEST_ASSERT_NOT_EQUAL(idOne, CMD_IR_SAMSUNG_POWER);
}

// --- dispatch ---------------------------------------------------------------

void test_ir_command_is_sent_with_protocol_and_payload(void) {
  executeCommand(CMD_IR_SAMSUNG_POWER);

  TEST_ASSERT_EQUAL_size_t(1, fakes::irSends.size());
  TEST_ASSERT_EQUAL_INT(IR_PROTOCOL_SAMSUNG, fakes::irSends[0].protocol);
  // the protocol is stripped off, only the data payload is handed to the HAL
  TEST_ASSERT_EQUAL_size_t(1, fakes::irSends[0].payloads.size());
  TEST_ASSERT_EQUAL_STRING("0xE0E040BF", fakes::irSends[0].payloads.front().c_str());
}

void test_mqtt_command_uses_configured_payload(void) {
  executeCommand(CMD_MQTT_BULB);

  TEST_ASSERT_EQUAL_size_t(1, fakes::mqttPublishes.size());
  TEST_ASSERT_EQUAL_STRING("bulb1_set", fakes::mqttPublishes[0].topic.c_str());
  TEST_ASSERT_EQUAL_STRING("ON", fakes::mqttPublishes[0].payload.c_str());
}

void test_mqtt_command_additional_payload_wins(void) {
  executeCommand(CMD_MQTT_BULB, "OFF");

  TEST_ASSERT_EQUAL_size_t(1, fakes::mqttPublishes.size());
  TEST_ASSERT_EQUAL_STRING("OFF", fakes::mqttPublishes[0].payload.c_str());
}

void test_scene_and_gui_commands_are_delegated(void) {
  executeCommand(CMD_SCENE_TV);
  executeCommand(CMD_GUI_NUMPAD);

  TEST_ASSERT_EQUAL_size_t(1, fakes::sceneCalls.size());
  TEST_ASSERT_EQUAL_STRING("TV", fakes::sceneCalls[0].payloads.front().c_str());
  TEST_ASSERT_EQUAL_size_t(1, fakes::guiCalls.size());
  TEST_ASSERT_EQUAL_STRING("Numpad", fakes::guiCalls[0].payloads.front().c_str());
}

void test_unknown_command_is_ignored_without_crash(void) {
  executeCommand(60000);

  TEST_ASSERT_EQUAL_size_t(0, fakes::irSends.size());
  TEST_ASSERT_EQUAL_size_t(0, fakes::mqttPublishes.size());
  TEST_ASSERT_EQUAL_size_t(0, fakes::sceneCalls.size());
}

void test_received_ir_message_is_forwarded_to_gui(void) {
  receiveNewIRmessage_cb("SAMSUNG E0E040BF");

  TEST_ASSERT_EQUAL_size_t(1, fakes::shownIRMessages.size());
  TEST_ASSERT_EQUAL_STRING("SAMSUNG E0E040BF", fakes::shownIRMessages[0].c_str());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_register_command_assigns_unique_ids);
  RUN_TEST(test_get_uniqueCommandID_does_not_collide_with_registered_commands);
  RUN_TEST(test_ir_command_is_sent_with_protocol_and_payload);
  RUN_TEST(test_mqtt_command_uses_configured_payload);
  RUN_TEST(test_mqtt_command_additional_payload_wins);
  RUN_TEST(test_scene_and_gui_commands_are_delegated);
  RUN_TEST(test_unknown_command_is_ignored_without_crash);
  RUN_TEST(test_received_ir_message_is_forwarded_to_gui);
  return UNITY_END();
}
