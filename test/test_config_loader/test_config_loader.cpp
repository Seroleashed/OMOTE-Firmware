/*
  Unit tests for applicationInternal/storage/configLoader.cpp

  This is the step where a bad file can cost somebody their remote, so most of
  these tests are about what happens when something is wrong: a typo in one
  file, a damaged file, a file from a newer firmware. The rule they all pin
  down is the same - everything that still works keeps working, and the reason
  for what did not is kept rather than logged and forgotten.
*/

#include <unity.h>

#include <string>

#include "applicationInternal/bootGuard.h"
#include "applicationInternal/commandHandler.h"
#include "applicationInternal/hardware/IRremoteProtocols.h"
#include "applicationInternal/storage/configFile.h"
#include "applicationInternal/storage/configLoader.h"
#include "applicationInternal/storage/configModel.h"
#include "applicationInternal/storage/configStorage.h"
#include "fake_filesystem.h"
#include "omote_fakes.h"

static FakeFileSystem fileSystem;

// bootGuard needs a storage to report "not safe mode"
class NoSafeModeStorage : public BootCounterStorage {
public:
  uint8_t readFailedBoots() override { return 0; }
  void writeFailedBoots(uint8_t) override {}
  bool readSafeModeRequested() override { return false; }
  void writeSafeModeRequested(bool) override {}
};
static NoSafeModeStorage bootStorage;

static std::string devicePackJson(const std::string &deviceId, const std::string &commandName,
                                  const std::string &code = "0xE0E040BF") {
  return "{\"schemaVersion\":1,\"type\":\"omote.devicePack\","
         "\"device\":{\"id\":\"" +
         deviceId +
         "\"},"
         "\"commands\":[{\"name\":\"" +
         commandName + "\",\"handler\":\"IR\",\"payloads\":[\"7\",\"" + code + "\"]}]}";
}

void setUp(void) {
  fakes::reset();
  fileSystem.reset();
  configStorage::setFileSystem(&fileSystem);
  bootGuard::begin(&bootStorage);
}

void tearDown(void) {}

// --- nothing stored ----------------------------------------------------------

void test_without_any_files_nothing_is_loaded_and_nothing_breaks(void) {
  // the normal case on a device that has never been configured
  configLoader::Report report = configLoader::loadDevices();

  TEST_ASSERT_EQUAL_UINT16(0, report.devicesLoaded);
  TEST_ASSERT_EQUAL_UINT16(0, report.filesFailed);
  TEST_ASSERT_FALSE(report.hasErrors());
}

void test_files_outside_the_devices_directory_are_ignored(void) {
  fileSystem.files["/cfg/system.json"] = devicePackJson("wrongplace", "WRONGPLACE_POWER");

  configLoader::Report report = configLoader::loadDevices();
  TEST_ASSERT_EQUAL_UINT16(0, report.devicesLoaded);
}

// --- loading -----------------------------------------------------------------

void test_a_device_from_json_registers_its_commands(void) {
  fileSystem.files["/cfg/devices/testtv.json"] = devicePackJson("testtv", "TESTTV_POWER");

  configLoader::Report report = configLoader::loadDevices();

  TEST_ASSERT_EQUAL_UINT16(1, report.devicesLoaded);
  TEST_ASSERT_EQUAL_UINT16(1, report.commandsRegistered);

  uint16_t command = get_commandID_byName("TESTTV_POWER");
  TEST_ASSERT_NOT_EQUAL(COMMAND_UNKNOWN, command);

  // and it actually works - this is the whole point of the step
  executeCommand(command);
  TEST_ASSERT_EQUAL_size_t(1, fakes::irSends.size());
  TEST_ASSERT_EQUAL_INT(IR_PROTOCOL_SAMSUNG, fakes::irSends[0].protocol);
}

void test_several_devices_are_all_loaded(void) {
  fileSystem.files["/cfg/devices/a.json"] = devicePackJson("a", "A_POWER");
  fileSystem.files["/cfg/devices/b.json"] = devicePackJson("b", "B_POWER");
  fileSystem.files["/cfg/devices/c.json"] = devicePackJson("c", "C_POWER");

  configLoader::Report report = configLoader::loadDevices();

  TEST_ASSERT_EQUAL_UINT16(3, report.devicesLoaded);
  TEST_ASSERT_NOT_EQUAL(COMMAND_UNKNOWN, get_commandID_byName("A_POWER"));
  TEST_ASSERT_NOT_EQUAL(COMMAND_UNKNOWN, get_commandID_byName("C_POWER"));
}

void test_a_file_written_through_configStorage_is_loaded(void) {
  // the round trip that the web UI will use: save through configStorage with
  // its envelope, read it back through the loader
  TEST_ASSERT_TRUE(configStorage::save("/cfg/devices/saved.json",
                                       devicePackJson("saved", "SAVED_POWER"), 1));

  configLoader::Report report = configLoader::loadDevices();
  TEST_ASSERT_EQUAL_UINT16(1, report.devicesLoaded);
  TEST_ASSERT_NOT_EQUAL(COMMAND_UNKNOWN, get_commandID_byName("SAVED_POWER"));
}

void test_plain_json_without_an_envelope_is_loaded_too(void) {
  // a file copied onto the device by hand. The envelope is added when we write,
  // not demanded when we read.
  fileSystem.files["/cfg/devices/handmade.json"] = devicePackJson("handmade", "HANDMADE_POWER");

  configLoader::Report report = configLoader::loadDevices();
  TEST_ASSERT_EQUAL_UINT16(1, report.devicesLoaded);
  TEST_ASSERT_EQUAL_UINT16(0, report.filesFailed);
}

// --- overriding --------------------------------------------------------------

void test_json_wins_over_a_command_registered_in_cpp(void) {
  // "edit the Samsung TV in the browser" depends on exactly this
  static uint16_t compiledIn;
  register_command_withName(&compiledIn, makeCommandData(IR, {"7", "0xAAAAAAAA"}), "SHARED_POWER");

  fileSystem.files["/cfg/devices/shared.json"] = devicePackJson("shared", "SHARED_POWER", "0xBBBBBBBB");
  configLoader::Report report = configLoader::loadDevices();

  TEST_ASSERT_EQUAL_UINT16(1, report.files[0].overrides);

  // the name now points at the definition from the file
  uint16_t fromJson = get_commandID_byName("SHARED_POWER");
  TEST_ASSERT_NOT_EQUAL(compiledIn, fromJson);
  executeCommand(fromJson);
  TEST_ASSERT_EQUAL_size_t(1, fakes::irSends.size());
  TEST_ASSERT_EQUAL_STRING("0xBBBBBBBB", fakes::irSends[0].payloads.front().c_str());
}

void test_the_old_command_id_keeps_working_after_being_overridden(void) {
  // whoever still holds the id - a key map, a gui button - must not end up
  // pressing a dead button
  static uint16_t compiledIn;
  register_command_withName(&compiledIn, makeCommandData(IR, {"7", "0xAAAAAAAA"}), "SHARED2_POWER");

  fileSystem.files["/cfg/devices/shared2.json"] = devicePackJson("shared2", "SHARED2_POWER", "0xBBBBBBBB");
  configLoader::loadDevices();

  executeCommand(compiledIn);
  TEST_ASSERT_EQUAL_size_t(1, fakes::irSends.size());
  TEST_ASSERT_EQUAL_STRING("0xAAAAAAAA", fakes::irSends[0].payloads.front().c_str());
}

// --- broken files ------------------------------------------------------------

void test_a_broken_file_does_not_stop_the_others(void) {
  fileSystem.files["/cfg/devices/a_good.json"] = devicePackJson("good1", "GOOD1_POWER");
  fileSystem.files["/cfg/devices/b_broken.json"] = "{ this is not json";
  fileSystem.files["/cfg/devices/c_good.json"] = devicePackJson("good2", "GOOD2_POWER");

  configLoader::Report report = configLoader::loadDevices();

  TEST_ASSERT_EQUAL_UINT16(2, report.devicesLoaded);
  TEST_ASSERT_EQUAL_UINT16(1, report.filesFailed);
  TEST_ASSERT_NOT_EQUAL(COMMAND_UNKNOWN, get_commandID_byName("GOOD1_POWER"));
  TEST_ASSERT_NOT_EQUAL(COMMAND_UNKNOWN, get_commandID_byName("GOOD2_POWER"));
}

void test_the_reason_for_a_skipped_file_is_kept(void) {
  // the web UI has to be able to tell the user which file to fix and why
  fileSystem.files["/cfg/devices/broken.json"] = "{ this is not json";

  configLoader::Report report = configLoader::loadDevices();

  TEST_ASSERT_EQUAL_size_t(1, report.files.size());
  TEST_ASSERT_FALSE(report.files[0].loaded);
  TEST_ASSERT_EQUAL_STRING("/cfg/devices/broken.json", report.files[0].path.c_str());
  TEST_ASSERT_TRUE(report.files[0].error.size() > 0);
  TEST_ASSERT_TRUE(report.hasErrors());
}

void test_a_file_from_a_newer_firmware_is_named_in_the_error(void) {
  fileSystem.files["/cfg/devices/future.json"] =
      "{\"schemaVersion\":99,\"type\":\"omote.devicePack\",\"device\":{\"id\":\"x\"},\"commands\":[]}";

  configLoader::Report report = configLoader::loadDevices();
  TEST_ASSERT_EQUAL_UINT16(1, report.filesFailed);
  TEST_ASSERT_TRUE_MESSAGE(report.files[0].error.find("newer") != std::string::npos,
                           report.files[0].error.c_str());
}

void test_a_damaged_file_is_reported_as_damaged_not_as_bad_json(void) {
  // a file with an envelope whose checksum does not match is a different
  // problem from a typo, and sends the user looking in a different place
  TEST_ASSERT_TRUE(configStorage::save("/cfg/devices/bitrot.json",
                                       devicePackJson("bitrot", "BITROT_POWER"), 1));
  fileSystem.corrupt("/cfg/devices/bitrot.json");
  fileSystem.remove("/cfg/devices/bitrot.json.bak");

  configLoader::Report report = configLoader::loadDevices();

  TEST_ASSERT_EQUAL_UINT16(1, report.filesFailed);
  TEST_ASSERT_TRUE_MESSAGE(report.files[0].error.find("damaged") != std::string::npos,
                           report.files[0].error.c_str());
}

void test_a_damaged_file_falls_back_to_its_backup(void) {
  // configStorage keeps the previous version; the loader benefits from it
  // without having to know how
  TEST_ASSERT_TRUE(configStorage::save("/cfg/devices/rot.json",
                                       devicePackJson("rot", "ROT_POWER", "0xAAAAAAAA"), 1));
  TEST_ASSERT_TRUE(configStorage::save("/cfg/devices/rot.json",
                                       devicePackJson("rot", "ROT_POWER", "0xBBBBBBBB"), 1));
  fileSystem.corrupt("/cfg/devices/rot.json");

  configLoader::Report report = configLoader::loadDevices();

  TEST_ASSERT_EQUAL_UINT16(1, report.devicesLoaded);
  executeCommand(get_commandID_byName("ROT_POWER"));
  TEST_ASSERT_EQUAL_STRING("0xAAAAAAAA", fakes::irSends[0].payloads.front().c_str());
}

void test_storage_bookkeeping_files_are_not_loaded_as_devices(void) {
  // loading a .bak would quietly resurrect the previous version of a device
  // alongside the current one
  configStorage::save("/cfg/devices/dev.json", devicePackJson("dev", "DEV_POWER"), 1);
  configStorage::save("/cfg/devices/dev.json", devicePackJson("dev", "DEV_POWER"), 1);
  TEST_ASSERT_TRUE(fileSystem.exists("/cfg/devices/dev.json.bak"));

  configLoader::Report report = configLoader::loadDevices();
  TEST_ASSERT_EQUAL_UINT16(1, report.devicesLoaded);
}

// --- safe mode ---------------------------------------------------------------

class SafeModeStorage : public BootCounterStorage {
public:
  uint8_t readFailedBoots() override { return 0; }
  void writeFailedBoots(uint8_t) override {}
  bool readSafeModeRequested() override { return true; }
  void writeSafeModeRequested(bool) override {}
};

void test_safe_mode_skips_the_stored_configuration_entirely(void) {
  // the way back when a file manages to crash the firmware during startup
  static SafeModeStorage safeModeStorage;
  bootGuard::begin(&safeModeStorage);

  fileSystem.files["/cfg/devices/tv.json"] = devicePackJson("tv", "SAFEMODE_TV_POWER");

  configLoader::Report report = configLoader::loadDevices();

  TEST_ASSERT_TRUE(report.skippedBecauseOfSafeMode);
  TEST_ASSERT_EQUAL_UINT16(0, report.devicesLoaded);
  TEST_ASSERT_EQUAL(COMMAND_UNKNOWN, get_commandID_byName("SAFEMODE_TV_POWER"));

  bootGuard::begin(&bootStorage);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_without_any_files_nothing_is_loaded_and_nothing_breaks);
  RUN_TEST(test_files_outside_the_devices_directory_are_ignored);
  RUN_TEST(test_a_device_from_json_registers_its_commands);
  RUN_TEST(test_several_devices_are_all_loaded);
  RUN_TEST(test_a_file_written_through_configStorage_is_loaded);
  RUN_TEST(test_plain_json_without_an_envelope_is_loaded_too);
  RUN_TEST(test_json_wins_over_a_command_registered_in_cpp);
  RUN_TEST(test_the_old_command_id_keeps_working_after_being_overridden);
  RUN_TEST(test_a_broken_file_does_not_stop_the_others);
  RUN_TEST(test_the_reason_for_a_skipped_file_is_kept);
  RUN_TEST(test_a_file_from_a_newer_firmware_is_named_in_the_error);
  RUN_TEST(test_a_damaged_file_is_reported_as_damaged_not_as_bad_json);
  RUN_TEST(test_a_damaged_file_falls_back_to_its_backup);
  RUN_TEST(test_storage_bookkeeping_files_are_not_loaded_as_devices);
  RUN_TEST(test_safe_mode_skips_the_stored_configuration_entirely);
  return UNITY_END();
}
