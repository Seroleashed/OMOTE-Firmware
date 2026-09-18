/*
  Unit tests for applicationInternal/storage/configStorage.cpp

  The point of these tests is the question the whole web configuration depends
  on: what happens when the battery dies exactly while the configuration is
  being written? Every scenario below is reproduced deterministically with the
  fake file system - no hardware, no waiting.
*/

#include <unity.h>

#include <string>

#include "applicationInternal/storage/configStorage.h"
#include "fake_filesystem.h"
#include "omote_fakes.h"

static FakeFileSystem fakeFs;
static const std::string NAME = "/cfg/system.json";
static const std::string PAYLOAD_V1 = "{\"deviceName\":\"OMOTE\",\"sleepTimeout\":30000}";
static const std::string PAYLOAD_V2 = "{\"deviceName\":\"Wohnzimmer\",\"sleepTimeout\":60000}";

void setUp(void) {
  fakes::reset();
  fakeFs.reset();
  configStorage::setFileSystem(&fakeFs);
}

void tearDown(void) {}

// --- happy path --------------------------------------------------------------

void test_save_and_load_roundtrip(void) {
  TEST_ASSERT_TRUE(configStorage::save(NAME, PAYLOAD_V1, 1));

  configStorage::LoadedConfig loaded = configStorage::load(NAME);
  TEST_ASSERT_TRUE(loaded.usable());
  TEST_ASSERT_EQUAL(configStorage::LoadResult::Ok, loaded.result);
  TEST_ASSERT_EQUAL_UINT16(1, loaded.schemaVersion);
  TEST_ASSERT_EQUAL_STRING(PAYLOAD_V1.c_str(), loaded.payload.c_str());
}

void test_payload_with_newlines_and_braces_survives(void) {
  const std::string multiline = "{\n  \"a\": 1,\n  \"b\": \"line\\nbreak\"\n}\n";
  TEST_ASSERT_TRUE(configStorage::save(NAME, multiline, 7));

  configStorage::LoadedConfig loaded = configStorage::load(NAME);
  TEST_ASSERT_EQUAL_STRING(multiline.c_str(), loaded.payload.c_str());
  TEST_ASSERT_EQUAL_UINT16(7, loaded.schemaVersion);
}

void test_nothing_stored_yet(void) {
  configStorage::LoadedConfig loaded = configStorage::load(NAME);
  TEST_ASSERT_EQUAL(configStorage::LoadResult::NotFound, loaded.result);
  TEST_ASSERT_FALSE(loaded.usable());
  TEST_ASSERT_FALSE(configStorage::hasStoredConfig(NAME));
}

void test_no_temp_file_is_left_behind(void) {
  configStorage::save(NAME, PAYLOAD_V1, 1);
  TEST_ASSERT_FALSE(fakeFs.exists(NAME + ".tmp"));
}

// --- power loss --------------------------------------------------------------

void test_power_loss_while_writing_keeps_previous_config(void) {
  TEST_ASSERT_TRUE(configStorage::save(NAME, PAYLOAD_V1, 1));

  // the battery dies after 20 bytes of the new file
  fakeFs.writeFailsAfterBytes = 20;
  TEST_ASSERT_FALSE(configStorage::save(NAME, PAYLOAD_V2, 2));

  configStorage::LoadedConfig loaded = configStorage::load(NAME);
  TEST_ASSERT_EQUAL(configStorage::LoadResult::Ok, loaded.result);
  TEST_ASSERT_EQUAL_STRING(PAYLOAD_V1.c_str(), loaded.payload.c_str());
  TEST_ASSERT_EQUAL_UINT16(1, loaded.schemaVersion);
}

void test_power_loss_between_backup_and_activation_loads_backup(void) {
  TEST_ASSERT_TRUE(configStorage::save(NAME, PAYLOAD_V1, 1));

  // the temp file is written and verified, the old file is already moved to
  // .bak, and then the device resets before the final rename
  fakeFs.failRenameTo = NAME;
  TEST_ASSERT_FALSE(configStorage::save(NAME, PAYLOAD_V2, 2));
  TEST_ASSERT_FALSE(fakeFs.exists(NAME));

  configStorage::LoadedConfig loaded = configStorage::load(NAME);
  TEST_ASSERT_EQUAL(configStorage::LoadResult::OkFromBackup, loaded.result);
  TEST_ASSERT_TRUE(loaded.usable());
  TEST_ASSERT_EQUAL_STRING(PAYLOAD_V1.c_str(), loaded.payload.c_str());
}

// --- corruption --------------------------------------------------------------

void test_single_bit_flip_is_detected_and_backup_is_used(void) {
  TEST_ASSERT_TRUE(configStorage::save(NAME, PAYLOAD_V1, 1));
  TEST_ASSERT_TRUE(configStorage::save(NAME, PAYLOAD_V2, 2));

  fakeFs.corrupt(NAME);

  configStorage::LoadedConfig loaded = configStorage::load(NAME);
  TEST_ASSERT_EQUAL(configStorage::LoadResult::OkFromBackup, loaded.result);
  TEST_ASSERT_EQUAL_STRING(PAYLOAD_V1.c_str(), loaded.payload.c_str());
}

void test_both_copies_broken_reports_corrupt(void) {
  configStorage::save(NAME, PAYLOAD_V1, 1);
  configStorage::save(NAME, PAYLOAD_V2, 2);

  fakeFs.corrupt(NAME);
  fakeFs.corrupt(NAME + ".bak");

  configStorage::LoadedConfig loaded = configStorage::load(NAME);
  TEST_ASSERT_EQUAL(configStorage::LoadResult::Corrupt, loaded.result);
  TEST_ASSERT_FALSE(loaded.usable());
  // the caller now has to fall back to the configuration compiled in
  TEST_ASSERT_EQUAL_size_t(0, loaded.payload.size());
}

void test_garbage_file_is_not_accepted(void) {
  fakeFs.files[NAME] = "just some text without a header";
  configStorage::LoadedConfig loaded = configStorage::load(NAME);
  TEST_ASSERT_EQUAL(configStorage::LoadResult::Corrupt, loaded.result);
}

void test_truncated_payload_is_detected_by_length(void) {
  configStorage::save(NAME, PAYLOAD_V1, 1);
  std::string &stored = fakeFs.files[NAME];
  stored = stored.substr(0, stored.size() - 5);

  configStorage::LoadedConfig loaded = configStorage::load(NAME);
  TEST_ASSERT_EQUAL(configStorage::LoadResult::Corrupt, loaded.result);
}

// --- backup rotation ---------------------------------------------------------

void test_backup_is_only_kept_from_a_valid_file(void) {
  configStorage::save(NAME, PAYLOAD_V1, 1);
  // the stored file goes bad before the next save
  fakeFs.corrupt(NAME);
  configStorage::save(NAME, PAYLOAD_V2, 2);

  // a broken file must not become the backup
  TEST_ASSERT_FALSE(fakeFs.exists(NAME + ".bak"));
  configStorage::LoadedConfig loaded = configStorage::load(NAME);
  TEST_ASSERT_EQUAL(configStorage::LoadResult::Ok, loaded.result);
  TEST_ASSERT_EQUAL_STRING(PAYLOAD_V2.c_str(), loaded.payload.c_str());
}

void test_remove_clears_file_and_backup(void) {
  configStorage::save(NAME, PAYLOAD_V1, 1);
  configStorage::save(NAME, PAYLOAD_V2, 2);
  TEST_ASSERT_TRUE(configStorage::hasStoredConfig(NAME));

  TEST_ASSERT_TRUE(configStorage::remove(NAME));
  TEST_ASSERT_FALSE(configStorage::hasStoredConfig(NAME));
  TEST_ASSERT_EQUAL(configStorage::LoadResult::NotFound, configStorage::load(NAME).result);
}

// --- crc ---------------------------------------------------------------------

void test_crc32_matches_known_value(void) {
  // "123456789" has the well known IEEE crc32 of 0xCBF43926
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, configStorage::crc32("123456789"));
  TEST_ASSERT_EQUAL_HEX32(0x00000000, configStorage::crc32(""));
}

void test_crc32_differs_for_similar_payloads(void) {
  TEST_ASSERT_NOT_EQUAL(configStorage::crc32("{\"a\":1}"), configStorage::crc32("{\"a\":2}"));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_save_and_load_roundtrip);
  RUN_TEST(test_payload_with_newlines_and_braces_survives);
  RUN_TEST(test_nothing_stored_yet);
  RUN_TEST(test_no_temp_file_is_left_behind);
  RUN_TEST(test_power_loss_while_writing_keeps_previous_config);
  RUN_TEST(test_power_loss_between_backup_and_activation_loads_backup);
  RUN_TEST(test_single_bit_flip_is_detected_and_backup_is_used);
  RUN_TEST(test_both_copies_broken_reports_corrupt);
  RUN_TEST(test_garbage_file_is_not_accepted);
  RUN_TEST(test_truncated_payload_is_detected_by_length);
  RUN_TEST(test_backup_is_only_kept_from_a_valid_file);
  RUN_TEST(test_remove_clears_file_and_backup);
  RUN_TEST(test_crc32_matches_known_value);
  RUN_TEST(test_crc32_differs_for_similar_payloads);
  return UNITY_END();
}
