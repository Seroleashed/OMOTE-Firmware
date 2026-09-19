/*
  Unit tests for applicationInternal/storage/configModel.cpp

  Two questions are answered here:
  1. does a device survive the way out to JSON and back unchanged?
  2. does a broken or foreign file produce a clear message instead of a
     half imported device?

  The second one matters more than it looks: from phase 2 on, these files come
  from a USB stick, a BLE transfer or from somebody else's OMOTE.
*/

#include <unity.h>

#include <string>

#include "applicationInternal/commandHandler.h"
#include "applicationInternal/hardware/IRremoteProtocols.h"
#include "applicationInternal/storage/configModel.h"
#include "applicationInternal/storage/configStorage.h"
#include "fake_filesystem.h"
#include "omote_fakes.h"

static FakeFileSystem fakeFs;

static configModel::DevicePack makeSamsungPack() {
  configModel::DevicePack pack;
  pack.id = "samsungTV";
  pack.name = "Samsung TV";
  pack.manufacturer = "Samsung";
  pack.model = "UE55";

  configModel::CommandDef power;
  power.name = "SAMSUNG_POWER";
  power.handler = IR;
  power.payloads = {std::to_string(IR_PROTOCOL_SAMSUNG), "0xE0E040BF"};
  pack.commands.push_back(power);

  configModel::CommandDef bulb;
  bulb.name = "LIVINGROOM_LIGHT";
  bulb.handler = MQTT;
  bulb.payloads = {"livingroom/light/set", "TOGGLE"};
  pack.commands.push_back(bulb);

  return pack;
}

void setUp(void) {
  fakes::reset();
  fakeFs.reset();
  configStorage::setFileSystem(&fakeFs);
}

void tearDown(void) {}

// --- round trip --------------------------------------------------------------

void test_device_pack_survives_serialize_and_parse(void) {
  configModel::DevicePack original = makeSamsungPack();
  std::string json = configModel::serializeDevicePack(original);

  configModel::DevicePack parsed;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseDevicePack(json, parsed, error), error.c_str());

  TEST_ASSERT_EQUAL_STRING("samsungTV", parsed.id.c_str());
  TEST_ASSERT_EQUAL_STRING("Samsung TV", parsed.name.c_str());
  TEST_ASSERT_EQUAL_STRING("UE55", parsed.model.c_str());
  TEST_ASSERT_EQUAL_size_t(2, parsed.commands.size());

  TEST_ASSERT_EQUAL_STRING("SAMSUNG_POWER", parsed.commands[0].name.c_str());
  TEST_ASSERT_EQUAL(IR, parsed.commands[0].handler);
  TEST_ASSERT_EQUAL_size_t(2, parsed.commands[0].payloads.size());
  TEST_ASSERT_EQUAL_STRING("0xE0E040BF", parsed.commands[0].payloads.back().c_str());
  TEST_ASSERT_EQUAL(MQTT, parsed.commands[1].handler);
}

void test_json_contains_names_and_no_numeric_ids(void) {
  std::string json = configModel::serializeDevicePack(makeSamsungPack());

  TEST_ASSERT_TRUE(json.find("\"SAMSUNG_POWER\"") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"handler\": \"IR\"") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"schemaVersion\": 1") != std::string::npos);
  // the volatile command id must never end up in a file
  TEST_ASSERT_TRUE(json.find("\"commandId\"") == std::string::npos);
}

void test_payload_with_special_characters_survives(void) {
  configModel::DevicePack pack;
  pack.id = "weird";
  configModel::CommandDef command;
  command.name = "WEIRD_ONE";
  command.handler = MQTT;
  command.payloads = {"haus/wohnzimmer/lampe", "{\"state\":\"ON\",\"note\":\"a\\\"b\"}"};
  pack.commands.push_back(command);

  configModel::DevicePack parsed;
  std::string error;
  TEST_ASSERT_TRUE(configModel::parseDevicePack(configModel::serializeDevicePack(pack), parsed, error));
  TEST_ASSERT_EQUAL_STRING(command.payloads.back().c_str(), parsed.commands[0].payloads.back().c_str());
}

void test_device_without_commands_is_valid(void) {
  configModel::DevicePack pack;
  pack.id = "empty";
  configModel::DevicePack parsed;
  std::string error;
  TEST_ASSERT_TRUE(configModel::parseDevicePack(configModel::serializeDevicePack(pack), parsed, error));
  TEST_ASSERT_EQUAL_size_t(0, parsed.commands.size());
  // a missing display name falls back to the id
  TEST_ASSERT_EQUAL_STRING("empty", parsed.name.c_str());
}

// --- validation --------------------------------------------------------------

static void assertRejected(const std::string &json, const char *expectedTextInError) {
  configModel::DevicePack parsed;
  std::string error;
  TEST_ASSERT_FALSE(configModel::parseDevicePack(json, parsed, error));
  TEST_ASSERT_FALSE(error.empty());
  if (expectedTextInError != nullptr) {
    TEST_ASSERT_TRUE_MESSAGE(error.find(expectedTextInError) != std::string::npos, error.c_str());
  }
}

void test_garbage_is_rejected(void) { assertRejected("this is not json", "JSON"); }

void test_missing_schema_version_is_rejected(void) {
  assertRejected("{\"type\":\"omote.devicePack\",\"device\":{\"id\":\"a\"},\"commands\":[]}",
                 "schemaVersion");
}

void test_newer_schema_version_is_rejected_with_a_readable_message(void) {
  assertRejected("{\"schemaVersion\":99,\"type\":\"omote.devicePack\",\"device\":{\"id\":\"a\"},"
                 "\"commands\":[]}",
                 "newer version");
}

void test_foreign_file_type_is_rejected(void) {
  assertRejected("{\"schemaVersion\":1,\"type\":\"something.else\",\"device\":{\"id\":\"a\"},"
                 "\"commands\":[]}",
                 "device pack");
}

void test_device_without_id_is_rejected(void) {
  assertRejected("{\"schemaVersion\":1,\"type\":\"omote.devicePack\",\"device\":{},\"commands\":[]}",
                 "device.id");
}

void test_unknown_handler_is_rejected_and_names_the_command(void) {
  assertRejected("{\"schemaVersion\":1,\"type\":\"omote.devicePack\",\"device\":{\"id\":\"a\"},"
                 "\"commands\":[{\"name\":\"X\",\"handler\":\"ZIGBEE\",\"payloads\":[\"1\"]}]}",
                 "X");
}

void test_command_without_payloads_is_rejected(void) {
  assertRejected("{\"schemaVersion\":1,\"type\":\"omote.devicePack\",\"device\":{\"id\":\"a\"},"
                 "\"commands\":[{\"name\":\"X\",\"handler\":\"IR\"}]}",
                 "payloads");
}

void test_unknown_extra_fields_are_ignored(void) {
  // a file written by a newer web UI must still load
  configModel::DevicePack parsed;
  std::string error;
  TEST_ASSERT_TRUE(configModel::parseDevicePack(
      "{\"schemaVersion\":1,\"type\":\"omote.devicePack\",\"createdBy\":\"web ui 2.0\","
      "\"device\":{\"id\":\"a\",\"icon\":\"tv\"},"
      "\"commands\":[{\"name\":\"X\",\"handler\":\"IR\",\"payloads\":[\"7\",\"0x1\"],"
      "\"color\":\"red\"}]}",
      parsed, error));
  TEST_ASSERT_EQUAL_size_t(1, parsed.commands.size());
}

// --- export of the compiled-in configuration ---------------------------------

void test_registered_commands_can_be_exported(void) {
  uint16_t power = 0, volUp = 0, otherDevice = 0;
  register_command_withName(&power, makeCommandData(IR, {"7", "0xE0E040BF"}), "samsungTV.power");
  register_command_withName(&volUp, makeCommandData(IR, {"7", "0xE0E0E01F"}), "samsungTV.volUp");
  register_command_withName(&otherDevice, makeCommandData(MQTT, {"lamp/set", "ON"}), "hue.lamp");

  configModel::DevicePack pack =
      configModel::devicePackFromRegisteredCommands("samsungTV", "Samsung TV", "samsungTV.");

  TEST_ASSERT_EQUAL_size_t(2, pack.commands.size());
  TEST_ASSERT_EQUAL_STRING("samsungTV.power", pack.commands[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("0xE0E040BF", pack.commands[0].payloads.back().c_str());
}

void test_export_without_prefix_takes_every_named_command(void) {
  uint16_t one = 0;
  register_command_withName(&one, makeCommandData(IR, {"7", "0x1"}), "only.one");
  configModel::DevicePack pack =
      configModel::devicePackFromRegisteredCommands("all", "everything", "");
  TEST_ASSERT_TRUE(pack.commands.size() >= 1);
}

// --- the whole chain ---------------------------------------------------------

void test_export_save_reboot_load_import(void) {
  // what happens on the device: export -> store on littlefs -> power loss ->
  // read back -> import
  uint16_t power = 0;
  register_command_withName(&power, makeCommandData(IR, {"7", "0xE0E040BF"}), "roundtrip.power");
  configModel::DevicePack exported =
      configModel::devicePackFromRegisteredCommands("roundtrip", "Round Trip", "roundtrip.");

  TEST_ASSERT_TRUE(configStorage::save("/cfg/devices/roundtrip.json",
                                       configModel::serializeDevicePack(exported),
                                       configModel::SCHEMA_VERSION));

  configStorage::LoadedConfig loaded = configStorage::load("/cfg/devices/roundtrip.json");
  TEST_ASSERT_TRUE(loaded.usable());
  TEST_ASSERT_EQUAL_UINT16(configModel::SCHEMA_VERSION, loaded.schemaVersion);

  configModel::DevicePack imported;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseDevicePack(loaded.payload, imported, error),
                           error.c_str());
  TEST_ASSERT_EQUAL_STRING("roundtrip.power", imported.commands[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("0xE0E040BF", imported.commands[0].payloads.back().c_str());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_device_pack_survives_serialize_and_parse);
  RUN_TEST(test_json_contains_names_and_no_numeric_ids);
  RUN_TEST(test_payload_with_special_characters_survives);
  RUN_TEST(test_device_without_commands_is_valid);
  RUN_TEST(test_garbage_is_rejected);
  RUN_TEST(test_missing_schema_version_is_rejected);
  RUN_TEST(test_newer_schema_version_is_rejected_with_a_readable_message);
  RUN_TEST(test_foreign_file_type_is_rejected);
  RUN_TEST(test_device_without_id_is_rejected);
  RUN_TEST(test_unknown_handler_is_rejected_and_names_the_command);
  RUN_TEST(test_command_without_payloads_is_rejected);
  RUN_TEST(test_unknown_extra_fields_are_ignored);
  RUN_TEST(test_registered_commands_can_be_exported);
  RUN_TEST(test_export_without_prefix_takes_every_named_command);
  RUN_TEST(test_export_save_reboot_load_import);
  return UNITY_END();
}
