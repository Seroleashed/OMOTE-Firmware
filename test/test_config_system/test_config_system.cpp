/*
  Unit tests for /cfg/system.json and the envelope every configuration file
  shares (configFile.cpp).

  The rule these tests pin down: a field the file does not mention keeps the
  value the caller passed in. That is what makes a partial file useful - the web
  UI can save just the brightness - and it is what keeps a truncated or hand
  edited file from resetting the whole device to defaults.
*/

#include <unity.h>

#include <string>

#include "applicationInternal/storage/configFile.h"
#include "applicationInternal/storage/configModel.h"

void setUp(void) {}
void tearDown(void) {}

// a config that differs from the defaults in every single field, so a test can
// tell "kept" from "overwritten by chance"
static configModel::SystemConfig distinctiveConfig() {
  configModel::SystemConfig config;
  config.deviceName = "wohnzimmer";
  config.backlightBrightness = 111;
  config.keyboardBrightness = 22;
  config.sleepTimeoutMs = 45000;
  config.wakeupByIMU = false;
  config.motionThreshold = 77;
  config.mqttEnabled = false;
  config.mqttBroker = "10.0.0.5";
  config.mqttPort = 8883;
  config.mqttClientName = "remote1";
  return config;
}

// --- round trip --------------------------------------------------------------

void test_system_config_survives_serialize_and_parse(void) {
  configModel::SystemConfig written = distinctiveConfig();
  std::string json = configModel::serializeSystemConfig(written);

  configModel::SystemConfig read;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseSystemConfig(json, read, error), error.c_str());

  TEST_ASSERT_EQUAL_STRING(written.deviceName.c_str(), read.deviceName.c_str());
  TEST_ASSERT_EQUAL_UINT8(written.backlightBrightness, read.backlightBrightness);
  TEST_ASSERT_EQUAL_UINT8(written.keyboardBrightness, read.keyboardBrightness);
  TEST_ASSERT_EQUAL_UINT32(written.sleepTimeoutMs, read.sleepTimeoutMs);
  TEST_ASSERT_EQUAL(written.wakeupByIMU, read.wakeupByIMU);
  TEST_ASSERT_EQUAL_UINT8(written.motionThreshold, read.motionThreshold);
  TEST_ASSERT_EQUAL(written.mqttEnabled, read.mqttEnabled);
  TEST_ASSERT_EQUAL_STRING(written.mqttBroker.c_str(), read.mqttBroker.c_str());
  TEST_ASSERT_EQUAL_UINT16(written.mqttPort, read.mqttPort);
  TEST_ASSERT_EQUAL_STRING(written.mqttClientName.c_str(), read.mqttClientName.c_str());
}

void test_defaults_come_from_the_firmware(void) {
  configModel::SystemConfig config = configModel::defaultSystemConfig();
  // whatever secrets.h says, these must never come back empty - they are what
  // the device falls back to when there is no file at all
  TEST_ASSERT_TRUE(config.deviceName.size() > 0);
  TEST_ASSERT_TRUE(config.mqttClientName.size() > 0);
  TEST_ASSERT_TRUE(config.mqttPort > 0);
}

// --- partial files -----------------------------------------------------------

void test_a_missing_field_keeps_the_value_it_had(void) {
  std::string json =
      "{\"schemaVersion\":1,\"type\":\"omote.system\",\"display\":{\"backlightBrightness\":42}}";

  configModel::SystemConfig config = distinctiveConfig();
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseSystemConfig(json, config, error), error.c_str());

  // the one field the file mentions
  TEST_ASSERT_EQUAL_UINT8(42, config.backlightBrightness);
  // everything else untouched
  TEST_ASSERT_EQUAL_STRING("wohnzimmer", config.deviceName.c_str());
  TEST_ASSERT_EQUAL_UINT8(22, config.keyboardBrightness);
  TEST_ASSERT_EQUAL_UINT32(45000, config.sleepTimeoutMs);
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", config.mqttBroker.c_str());
  TEST_ASSERT_EQUAL_UINT16(8883, config.mqttPort);
}

void test_an_empty_config_object_changes_nothing(void) {
  std::string json = "{\"schemaVersion\":1,\"type\":\"omote.system\"}";

  configModel::SystemConfig config = distinctiveConfig();
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseSystemConfig(json, config, error), error.c_str());

  TEST_ASSERT_EQUAL_STRING("wohnzimmer", config.deviceName.c_str());
  TEST_ASSERT_EQUAL_UINT8(111, config.backlightBrightness);
  TEST_ASSERT_EQUAL_UINT32(45000, config.sleepTimeoutMs);
}

void test_a_field_with_the_wrong_type_is_ignored_not_zeroed(void) {
  // hand edited file, brightness written as a string. Resetting the display to
  // 0 would leave the user staring at a black screen.
  std::string json = "{\"schemaVersion\":1,\"type\":\"omote.system\","
                     "\"display\":{\"backlightBrightness\":\"hell\"}}";

  configModel::SystemConfig config = distinctiveConfig();
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseSystemConfig(json, config, error), error.c_str());
  TEST_ASSERT_EQUAL_UINT8(111, config.backlightBrightness);
}

void test_unknown_fields_are_ignored(void) {
  // written by a newer web UI that knows a setting this firmware does not
  std::string json = "{\"schemaVersion\":1,\"type\":\"omote.system\",\"deviceName\":\"kueche\","
                     "\"somethingNew\":{\"a\":1},\"display\":{\"nightMode\":true}}";

  configModel::SystemConfig config = distinctiveConfig();
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseSystemConfig(json, config, error), error.c_str());
  TEST_ASSERT_EQUAL_STRING("kueche", config.deviceName.c_str());
}

// --- rejection ---------------------------------------------------------------

void test_garbage_is_rejected(void) {
  configModel::SystemConfig config;
  std::string error;
  TEST_ASSERT_FALSE(configModel::parseSystemConfig("not json at all", config, error));
  TEST_ASSERT_TRUE(error.size() > 0);
}

void test_a_device_pack_is_not_accepted_as_a_system_file(void) {
  // picking the wrong file in the web UI is exactly the mistake "type" is for
  std::string json = "{\"schemaVersion\":1,\"type\":\"omote.devicePack\",\"device\":{\"id\":\"tv\"}}";

  configModel::SystemConfig config;
  std::string error;
  TEST_ASSERT_FALSE(configModel::parseSystemConfig(json, config, error));
  TEST_ASSERT_TRUE_MESSAGE(error.find("omote.devicePack") != std::string::npos, error.c_str());
  TEST_ASSERT_TRUE_MESSAGE(error.find("omote.system") != std::string::npos, error.c_str());
}

void test_a_newer_schema_version_is_rejected_readably(void) {
  std::string json = "{\"schemaVersion\":99,\"type\":\"omote.system\"}";

  configModel::SystemConfig config;
  std::string error;
  TEST_ASSERT_FALSE(configModel::parseSystemConfig(json, config, error));
  TEST_ASSERT_TRUE_MESSAGE(error.find("99") != std::string::npos, error.c_str());
  TEST_ASSERT_TRUE_MESSAGE(error.find("newer") != std::string::npos, error.c_str());
}

void test_an_empty_device_name_is_rejected(void) {
  std::string json = "{\"schemaVersion\":1,\"type\":\"omote.system\",\"deviceName\":\"\"}";

  configModel::SystemConfig config;
  std::string error;
  TEST_ASSERT_FALSE(configModel::parseSystemConfig(json, config, error));
}

void test_no_credentials_end_up_in_the_file(void) {
  // the whole point of keeping them in NVS: an exported system.json can be
  // shared or posted in a forum thread
  configModel::SystemConfig config = distinctiveConfig();
  std::string json = configModel::serializeSystemConfig(config);

  TEST_ASSERT_TRUE(json.find("password") == std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"user\"") == std::string::npos);
  TEST_ASSERT_TRUE(json.find("ssid") == std::string::npos);
  TEST_ASSERT_TRUE(json.find("SSID") == std::string::npos);
}

// --- the shared envelope -----------------------------------------------------

void test_envelope_reports_a_missing_schema_version(void) {
  JsonDocument doc;
  uint16_t version = 0;
  std::string error;
  TEST_ASSERT_FALSE(configFile::parseAndCheckEnvelope("{\"type\":\"omote.system\"}",
                                                      configFile::TYPE_SYSTEM, 1, doc, version, error));
  TEST_ASSERT_TRUE_MESSAGE(error.find("schemaVersion") != std::string::npos, error.c_str());
}

void test_envelope_rejects_version_zero(void) {
  // an uninitialised field, not a real version
  JsonDocument doc;
  uint16_t version = 0;
  std::string error;
  TEST_ASSERT_FALSE(configFile::parseAndCheckEnvelope(
      "{\"schemaVersion\":0,\"type\":\"omote.system\"}", configFile::TYPE_SYSTEM, 1, doc, version, error));
}

void test_envelope_reports_a_missing_type_by_name(void) {
  JsonDocument doc;
  uint16_t version = 0;
  std::string error;
  TEST_ASSERT_FALSE(configFile::parseAndCheckEnvelope("{\"schemaVersion\":1}", configFile::TYPE_SYSTEM, 1,
                                                      doc, version, error));
  TEST_ASSERT_TRUE_MESSAGE(error.find("omote.system") != std::string::npos, error.c_str());
}

void test_envelope_hands_an_older_version_to_the_caller(void) {
  // this is what makes a migration possible: the parser learns which format it
  // is looking at instead of guessing from the fields
  JsonDocument doc;
  uint16_t version = 0;
  std::string error;
  TEST_ASSERT_TRUE(configFile::parseAndCheckEnvelope("{\"schemaVersion\":1,\"type\":\"omote.system\"}",
                                                     configFile::TYPE_SYSTEM, 3, doc, version, error));
  TEST_ASSERT_EQUAL_UINT16(1, version);
}

void test_envelope_rejects_a_json_array(void) {
  JsonDocument doc;
  uint16_t version = 0;
  std::string error;
  TEST_ASSERT_FALSE(
      configFile::parseAndCheckEnvelope("[1,2,3]", configFile::TYPE_SYSTEM, 1, doc, version, error));
}

void test_device_paths_are_built_from_the_id(void) {
  TEST_ASSERT_EQUAL_STRING("/cfg/devices/samsungTV.json", configFile::pathForDevice("samsungTV").c_str());
  TEST_ASSERT_EQUAL_STRING("/cfg/system.json", configFile::PATH_SYSTEM);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_system_config_survives_serialize_and_parse);
  RUN_TEST(test_defaults_come_from_the_firmware);
  RUN_TEST(test_a_missing_field_keeps_the_value_it_had);
  RUN_TEST(test_an_empty_config_object_changes_nothing);
  RUN_TEST(test_a_field_with_the_wrong_type_is_ignored_not_zeroed);
  RUN_TEST(test_unknown_fields_are_ignored);
  RUN_TEST(test_garbage_is_rejected);
  RUN_TEST(test_a_device_pack_is_not_accepted_as_a_system_file);
  RUN_TEST(test_a_newer_schema_version_is_rejected_readably);
  RUN_TEST(test_an_empty_device_name_is_rejected);
  RUN_TEST(test_no_credentials_end_up_in_the_file);
  RUN_TEST(test_envelope_reports_a_missing_schema_version);
  RUN_TEST(test_envelope_rejects_version_zero);
  RUN_TEST(test_envelope_reports_a_missing_type_by_name);
  RUN_TEST(test_envelope_hands_an_older_version_to_the_caller);
  RUN_TEST(test_envelope_rejects_a_json_array);
  RUN_TEST(test_device_paths_are_built_from_the_id);
  return UNITY_END();
}
