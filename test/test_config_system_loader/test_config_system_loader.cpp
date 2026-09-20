/*
  Unit tests for configLoader::loadSystem().

  Until this existed, three of the four configuration file types were a format
  with a parser and no effect: pushing a system.json changed nothing on the
  device. These tests are about the two questions that decides:

    * which settings does a file own, and which does it leave alone
    * what happens to the rest when one of them is wrong
*/

#include <unity.h>

#include <string>

#include "applicationInternal/bootGuard.h"
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/storage/configFile.h"
#include "applicationInternal/storage/configLoader.h"
#include "applicationInternal/storage/configStorage.h"
#include "fake_filesystem.h"
#include "omote_fakes.h"

static FakeFileSystem fileSystem;

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
  fileSystem.reset();
  configStorage::setFileSystem(&fileSystem);
  bootGuard::begin(&bootStorage);

  // what the device is running with before any file is read
  set_backlightBrightness(200);
  set_sleepTimeout(20000);
  set_motionThreshold(50);
  set_wakeupByIMUEnabled(true);
}

void tearDown(void) {}

// --- nothing stored ----------------------------------------------------------

void test_without_a_file_the_device_keeps_its_settings(void) {
  configLoader::SystemResult result = configLoader::loadSystem();

  TEST_ASSERT_FALSE(result.fileFound);
  TEST_ASSERT_FALSE(result.applied);
  TEST_ASSERT_EQUAL_UINT8(200, get_backlightBrightness());
  TEST_ASSERT_EQUAL_UINT32(20000, get_sleepTimeout());
}

// --- applying ----------------------------------------------------------------

void test_a_file_is_applied_to_the_device(void) {
  fileSystem.files[configFile::PATH_SYSTEM] =
      "{\"schemaVersion\":1,\"type\":\"omote.system\",\"deviceName\":\"wohnzimmer\","
      "\"display\":{\"backlightBrightness\":120},"
      "\"sleep\":{\"timeoutMs\":45000,\"motionThreshold\":80,\"wakeupByIMU\":false}}";

  configLoader::SystemResult result = configLoader::loadSystem();

  TEST_ASSERT_TRUE(result.fileFound);
  TEST_ASSERT_TRUE_MESSAGE(result.applied, result.error.c_str());
  TEST_ASSERT_EQUAL_UINT8(120, get_backlightBrightness());
  TEST_ASSERT_EQUAL_UINT32(45000, get_sleepTimeout());
  TEST_ASSERT_EQUAL_UINT8(80, get_motionThreshold());
  TEST_ASSERT_FALSE(get_wakeupByIMUEnabled());
  TEST_ASSERT_EQUAL_STRING("wohnzimmer", configLoader::systemConfig().deviceName.c_str());
}

void test_a_setting_the_file_does_not_mention_is_left_alone(void) {
  // a system.json holding only a brightness has to do exactly that one thing.
  // Resetting the sleep timeout as a side effect would be maddening.
  fileSystem.files[configFile::PATH_SYSTEM] =
      "{\"schemaVersion\":1,\"type\":\"omote.system\",\"display\":{\"backlightBrightness\":90}}";

  configLoader::loadSystem();

  TEST_ASSERT_EQUAL_UINT8(90, get_backlightBrightness());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(20000, get_sleepTimeout(),
                                   "the sleep timeout was not in the file");
  TEST_ASSERT_EQUAL_UINT8(50, get_motionThreshold());
  TEST_ASSERT_TRUE(get_wakeupByIMUEnabled());
}

void test_a_file_written_through_the_storage_is_applied_too(void) {
  // the round trip the transport uses: pushed with an envelope, read back here
  TEST_ASSERT_TRUE(configStorage::save(
      configFile::PATH_SYSTEM,
      "{\"schemaVersion\":1,\"type\":\"omote.system\",\"display\":{\"backlightBrightness\":77}}", 1));

  configLoader::SystemResult result = configLoader::loadSystem();
  TEST_ASSERT_TRUE_MESSAGE(result.applied, result.error.c_str());
  TEST_ASSERT_EQUAL_UINT8(77, get_backlightBrightness());
}

// --- when it is wrong --------------------------------------------------------

void test_a_broken_file_changes_nothing_at_all(void) {
  /*
    Half applying a file would be the worst outcome: the user sees some of their
    settings take effect and concludes the file is fine, while the rest silently
    is not.
  */
  fileSystem.files[configFile::PATH_SYSTEM] = "{ this is not json";

  configLoader::SystemResult result = configLoader::loadSystem();

  TEST_ASSERT_TRUE(result.fileFound);
  TEST_ASSERT_FALSE(result.applied);
  TEST_ASSERT_TRUE(result.error.size() > 0);
  TEST_ASSERT_EQUAL_UINT8(200, get_backlightBrightness());
  TEST_ASSERT_EQUAL_UINT32(20000, get_sleepTimeout());
}

void test_the_wrong_kind_of_file_is_refused_by_name(void) {
  // somebody pushed a device pack to /cfg/system.json
  fileSystem.files[configFile::PATH_SYSTEM] =
      "{\"schemaVersion\":1,\"type\":\"omote.devicePack\",\"device\":{\"id\":\"tv\"},\"commands\":[]}";

  configLoader::SystemResult result = configLoader::loadSystem();

  TEST_ASSERT_FALSE(result.applied);
  TEST_ASSERT_TRUE_MESSAGE(result.error.find("omote.system") != std::string::npos,
                           result.error.c_str());
  TEST_ASSERT_EQUAL_UINT8(200, get_backlightBrightness());
}

void test_a_damaged_file_falls_back_to_its_backup(void) {
  configStorage::save(configFile::PATH_SYSTEM,
                      "{\"schemaVersion\":1,\"type\":\"omote.system\","
                      "\"display\":{\"backlightBrightness\":111}}",
                      1);
  configStorage::save(configFile::PATH_SYSTEM,
                      "{\"schemaVersion\":1,\"type\":\"omote.system\","
                      "\"display\":{\"backlightBrightness\":222}}",
                      1);
  fileSystem.corrupt(configFile::PATH_SYSTEM);

  configLoader::SystemResult result = configLoader::loadSystem();

  TEST_ASSERT_TRUE_MESSAGE(result.applied, result.error.c_str());
  TEST_ASSERT_EQUAL_UINT8(111, get_backlightBrightness());
}

// --- safe mode ---------------------------------------------------------------

class SafeModeStorage : public BootCounterStorage {
public:
  uint8_t readFailedBoots() override { return 0; }
  void writeFailedBoots(uint8_t) override {}
  bool readSafeModeRequested() override { return true; }
  void writeSafeModeRequested(bool) override {}
};

void test_safe_mode_does_not_read_the_file(void) {
  // a brightness of 0 in a file would otherwise leave the user with a black
  // screen and no way back
  static SafeModeStorage safeModeStorage;
  bootGuard::begin(&safeModeStorage);

  fileSystem.files[configFile::PATH_SYSTEM] =
      "{\"schemaVersion\":1,\"type\":\"omote.system\",\"display\":{\"backlightBrightness\":0}}";

  configLoader::SystemResult result = configLoader::loadSystem();

  TEST_ASSERT_FALSE(result.applied);
  TEST_ASSERT_EQUAL_UINT8(200, get_backlightBrightness());

  bootGuard::begin(&bootStorage);
}

// --- what the rest of the firmware asks for ----------------------------------

void test_the_config_is_available_before_anything_was_loaded(void) {
  // the network side asks for the broker during startup, and must not get an
  // empty struct just because loadSystem() has not run yet
  TEST_ASSERT_TRUE(configLoader::systemConfig().mqttPort > 0);
  TEST_ASSERT_TRUE(configLoader::systemConfig().deviceName.size() > 0);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_the_config_is_available_before_anything_was_loaded);
  RUN_TEST(test_without_a_file_the_device_keeps_its_settings);
  RUN_TEST(test_a_file_is_applied_to_the_device);
  RUN_TEST(test_a_setting_the_file_does_not_mention_is_left_alone);
  RUN_TEST(test_a_file_written_through_the_storage_is_applied_too);
  RUN_TEST(test_a_broken_file_changes_nothing_at_all);
  RUN_TEST(test_the_wrong_kind_of_file_is_refused_by_name);
  RUN_TEST(test_a_damaged_file_falls_back_to_its_backup);
  RUN_TEST(test_safe_mode_does_not_read_the_file);
  return UNITY_END();
}
