/*
  Unit tests for applicationInternal/commandHandler.cpp

  These tests pin down the behaviour that the JSON configuration (phase 1) has
  to reproduce exactly: how commands are registered, which handler they are
  dispatched to and how payloads are passed on.
*/

#include <unity.h>

#include <string>
#include <list>
#include <map>

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

void test_registering_the_same_command_twice_reuses_the_id(void) {
  /*
    setKeysForAllRegisteredGUIsAndScenes() calls register_scene_defaultKeys() on
    every gui and scene registration, so a handful of commands were registered
    ten times over during startup. Every repeat used to hand out a fresh id and
    orphan the previous commandData in the map for good.
  */
  uint16_t first = 0;
  uint16_t second = 0;
  register_command_withName(&first, makeCommandData(SCENE, {"Selection"}), "REPEATED");
  register_command_withName(&second, makeCommandData(SCENE, {"Selection"}), "REPEATED");

  TEST_ASSERT_EQUAL_UINT16(first, second);
  TEST_ASSERT_EQUAL_UINT16(first, get_commandID_byName("REPEATED"));
}

void test_a_repeated_registration_does_not_grow_the_command_table(void) {
  uint16_t id = 0;
  register_command_withName(&id, makeCommandData(SCENE, {"Selection"}), "COUNTED");
  size_t afterFirst = get_all_commands().size();

  for (int i = 0; i < 10; i++) {
    register_command_withName(&id, makeCommandData(SCENE, {"Selection"}), "COUNTED");
  }
  TEST_ASSERT_EQUAL_size_t(afterFirst, get_all_commands().size());
}

void test_a_registration_with_different_data_still_overrides(void) {
  // this is what happens when a JSON file replaces a compiled-in device, and it
  // has to keep working
  uint16_t original = 0;
  uint16_t replacement = 0;
  register_command_withName(&original, makeCommandData(IR, {"7", "0xAAAA"}), "REPLACED");
  register_command_withName(&replacement, makeCommandData(IR, {"7", "0xBBBB"}), "REPLACED");

  TEST_ASSERT_NOT_EQUAL(original, replacement);
  TEST_ASSERT_EQUAL_UINT16(replacement, get_commandID_byName("REPLACED"));
  // and the old id keeps working for whoever still holds it
  commandData data;
  TEST_ASSERT_TRUE(get_commandData_byID(original, data));
}

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

// --- stable names -----------------------------------------------------------

void test_command_can_be_found_by_its_variable_name(void) {
  // the register_command macro uses the variable name, so no call site had to
  // be touched when names were introduced
  TEST_ASSERT_EQUAL_UINT16(CMD_IR_SAMSUNG_POWER, get_commandID_byName("CMD_IR_SAMSUNG_POWER"));
  TEST_ASSERT_EQUAL_STRING("CMD_MQTT_BULB", get_commandName_byID(CMD_MQTT_BULB).c_str());
}

void test_unknown_name_returns_command_unknown(void) {
  TEST_ASSERT_EQUAL_UINT16(COMMAND_UNKNOWN, get_commandID_byName("DOES_NOT_EXIST"));
  TEST_ASSERT_EQUAL_STRING("", get_commandName_byID(60000).c_str());
}

void test_explicit_name_is_used_when_given(void) {
  uint16_t runtimeCommand = 0;
  register_command_withName(&runtimeCommand, makeCommandData(IR, {"7", "0xAB"}),
                            "livingroom.tv.power");

  TEST_ASSERT_EQUAL_UINT16(runtimeCommand, get_commandID_byName("livingroom.tv.power"));
  TEST_ASSERT_EQUAL_STRING("livingroom.tv.power", get_commandName_byID(runtimeCommand).c_str());
}

void test_re_registering_a_name_points_it_at_the_new_command(void) {
  // this is what happens when a device is replaced by a JSON definition at
  // runtime: the newest registration owns the name
  uint16_t first = 0, second = 0;
  register_command_withName(&first, makeCommandData(IR, {"7", "0x01"}), "duplicate.name");
  register_command_withName(&second, makeCommandData(IR, {"7", "0x02"}), "duplicate.name");

  TEST_ASSERT_NOT_EQUAL(first, second);
  TEST_ASSERT_EQUAL_UINT16(second, get_commandID_byName("duplicate.name"));
  // the old command keeps working but is no longer reachable by name
  TEST_ASSERT_EQUAL_STRING("", get_commandName_byID(first).c_str());
  executeCommand(first);
  TEST_ASSERT_EQUAL_STRING("0x01", fakes::irSends.back().payloads.front().c_str());
}

void test_command_ids_are_not_stable_but_names_are(void) {
  // this is the reason the configuration must not store numeric ids:
  // registering anything before a command shifts its id
  uint16_t early = 0, late = 0;
  register_command_withName(&early, makeCommandData(IR, {"7", "0x10"}), "order.first");
  register_command_withName(&late, makeCommandData(IR, {"7", "0x11"}), "order.second");
  TEST_ASSERT_TRUE(late > early);
  TEST_ASSERT_EQUAL_UINT16(early, get_commandID_byName("order.first"));
}

void test_all_commands_can_be_iterated_for_export(void) {
  const std::map<uint16_t, commandData> &all = get_all_commands();
  const std::map<uint16_t, std::string> &names = get_all_commandNames();

  TEST_ASSERT_TRUE(all.count(CMD_IR_SAMSUNG_POWER) > 0);
  TEST_ASSERT_EQUAL(IR, all.at(CMD_IR_SAMSUNG_POWER).commandHandler);
  TEST_ASSERT_EQUAL_STRING("CMD_SCENE_TV", names.at(CMD_SCENE_TV).c_str());

  commandData found;
  TEST_ASSERT_TRUE(get_commandData_byID(CMD_MQTT_BULB, found));
  TEST_ASSERT_EQUAL_STRING("bulb1_set", found.commandPayloads.front().c_str());
  TEST_ASSERT_FALSE(get_commandData_byID(60000, found));
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
  RUN_TEST(test_registering_the_same_command_twice_reuses_the_id);
  RUN_TEST(test_a_repeated_registration_does_not_grow_the_command_table);
  RUN_TEST(test_a_registration_with_different_data_still_overrides);
  RUN_TEST(test_register_command_assigns_unique_ids);
  RUN_TEST(test_get_uniqueCommandID_does_not_collide_with_registered_commands);
  RUN_TEST(test_ir_command_is_sent_with_protocol_and_payload);
  RUN_TEST(test_mqtt_command_uses_configured_payload);
  RUN_TEST(test_mqtt_command_additional_payload_wins);
  RUN_TEST(test_scene_and_gui_commands_are_delegated);
  RUN_TEST(test_unknown_command_is_ignored_without_crash);
  RUN_TEST(test_command_can_be_found_by_its_variable_name);
  RUN_TEST(test_unknown_name_returns_command_unknown);
  RUN_TEST(test_explicit_name_is_used_when_given);
  RUN_TEST(test_re_registering_a_name_points_it_at_the_new_command);
  RUN_TEST(test_command_ids_are_not_stable_but_names_are);
  RUN_TEST(test_all_commands_can_be_iterated_for_export);
  RUN_TEST(test_received_ir_message_is_forwarded_to_gui);
  return UNITY_END();
}
