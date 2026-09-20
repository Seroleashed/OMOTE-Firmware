/*
  Unit tests for applicationInternal/transport/configTransport.cpp

  The protocol is fed bytes and hands back bytes, so all of this runs without a
  serial port, a BLE stack or a single millisecond of waiting. That is the whole
  reason the transport was built that way.

  Most of these tests are about a conversation going wrong: a file that arrives
  damaged, a link that drops mid-transfer, a path trying to climb out of /cfg.
  On a happy link the protocol is four lines; everything else is what makes it
  worth having.
*/

#include <unity.h>

#include <string>
#include <vector>

#include "applicationInternal/storage/configStorage.h"
#include "applicationInternal/transport/configTransport.h"
#include "fake_filesystem.h"

static FakeFileSystem fileSystem;
static int applyCalls = 0;
static int rebootCalls = 0;

static void onApply() { applyCalls++; }
static void onReboot() { rebootCalls++; }
static std::vector<std::string> onInfo() {
  std::vector<std::string> lines;
  lines.push_back("version 0.9.0-test");
  lines.push_back("hardware 5");
  return lines;
}

// sends a command and returns everything the device answered
static std::string ask(const std::string &line) {
  configTransport::feed(line + "\n");
  return configTransport::takeOutput();
}

static bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

void setUp(void) {
  fileSystem.reset();
  configStorage::setFileSystem(&fileSystem);
  applyCalls = 0;
  rebootCalls = 0;

  configTransport::Callbacks callbacks;
  callbacks.apply = &onApply;
  callbacks.reboot = &onReboot;
  callbacks.info = &onInfo;
  configTransport::begin(&fileSystem, callbacks);
}

void tearDown(void) {}

// --- LIST --------------------------------------------------------------------

void test_list_of_an_empty_device_is_empty_not_an_error(void) {
  std::string answer = ask("LIST");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "OK 0"), answer.c_str());
  TEST_ASSERT_TRUE(contains(answer, "END"));
}

void test_list_reports_every_file_with_its_size(void) {
  fileSystem.files["/cfg/system.json"] = "{\"a\":1}";
  fileSystem.files["/cfg/devices/tv.json"] = "{\"b\":22}";

  std::string answer = ask("LIST");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "OK 2"), answer.c_str());
  TEST_ASSERT_TRUE(contains(answer, "/cfg/system.json 7"));
  TEST_ASSERT_TRUE(contains(answer, "/cfg/devices/tv.json 8"));
}

void test_list_reports_the_size_get_would_hand_over(void) {
  /*
    A file written through configStorage carries an envelope on disk, but GET
    hands over the payload. If LIST reported the size on disk, a host comparing
    what it pushed with what the device lists would find a mismatch for every
    single file - which is exactly what happened the first time this was tried
    against the simulator.
  */
  std::string content = "{\"schemaVersion\":1,\"type\":\"omote.system\"}";
  TEST_ASSERT_TRUE(configStorage::save("/cfg/system.json", content, 1));

  std::string raw;
  fileSystem.read("/cfg/system.json", raw);
  TEST_ASSERT_TRUE_MESSAGE(raw.size() > content.size(), "the envelope should make the file bigger");

  std::string answer = ask("LIST");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "/cfg/system.json " + std::to_string(content.size())),
                           answer.c_str());
}

void test_list_hides_the_storage_bookkeeping_files(void) {
  // handing out a .bak invites somebody to pull one and wonder why it is not
  // the file they saved
  fileSystem.files["/cfg/system.json"] = "{}";
  fileSystem.files["/cfg/system.json.bak"] = "{}";
  fileSystem.files["/cfg/system.json.tmp"] = "{}";

  std::string answer = ask("LIST");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "OK 1"), answer.c_str());
  TEST_ASSERT_FALSE(contains(answer, ".bak"));
  TEST_ASSERT_FALSE(contains(answer, ".tmp"));
}

// --- GET ---------------------------------------------------------------------

void test_get_returns_length_crc_and_the_payload(void) {
  std::string content = "{\"schemaVersion\":1}";
  fileSystem.files["/cfg/system.json"] = content;

  std::string answer = ask("GET /cfg/system.json");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "OK 19 "), answer.c_str());
  TEST_ASSERT_TRUE(contains(answer, content));
  TEST_ASSERT_TRUE(contains(answer, "END"));
}

void test_get_hands_over_the_payload_without_the_envelope(void) {
  /*
    What crosses the wire is what an editor would show. The magic, length and
    crc of the storage envelope are a device-side detail and must not leak into
    a file somebody is about to open in a text editor.
  */
  std::string content = "{\"schemaVersion\":1,\"type\":\"omote.system\"}";
  TEST_ASSERT_TRUE(configStorage::save("/cfg/system.json", content, 1));

  std::string answer = ask("GET /cfg/system.json");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, content), answer.c_str());
  TEST_ASSERT_FALSE_MESSAGE(contains(answer, "OMOTECFG"), answer.c_str());
}

void test_get_of_a_missing_file_says_so(void) {
  std::string answer = ask("GET /cfg/nope.json");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "ERR"), answer.c_str());
  TEST_ASSERT_TRUE(contains(answer, "/cfg/nope.json"));
}

void test_get_of_a_damaged_file_says_damaged(void) {
  configStorage::save("/cfg/system.json", "{\"a\":1}", 1);
  fileSystem.corrupt("/cfg/system.json");
  fileSystem.remove("/cfg/system.json.bak");

  std::string answer = ask("GET /cfg/system.json");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "damaged"), answer.c_str());
}

// --- PUT ---------------------------------------------------------------------

static std::string putCommand(const std::string &path, const std::string &content) {
  char crc[16];
  snprintf(crc, sizeof(crc), "%08x", (unsigned int)configStorage::crc32(content));
  return "PUT " + path + " " + std::to_string(content.size()) + " " + crc;
}

void test_a_file_arrives_and_is_stored(void) {
  std::string content = "{\"schemaVersion\":1,\"type\":\"omote.devicePack\"}";

  std::string answer = ask(putCommand("/cfg/devices/tv.json", content));
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "READY"), answer.c_str());
  TEST_ASSERT_TRUE(configTransport::isReceiving());

  configTransport::feed(content);
  answer = configTransport::takeOutput();
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "OK"), answer.c_str());
  TEST_ASSERT_FALSE(configTransport::isReceiving());

  configStorage::LoadedConfig stored = configStorage::load("/cfg/devices/tv.json");
  TEST_ASSERT_TRUE(stored.usable());
  TEST_ASSERT_EQUAL_STRING(content.c_str(), stored.payload.c_str());
}

void test_a_file_arriving_in_pieces_is_acknowledged_along_the_way(void) {
  // BLE hands over about twenty bytes at a time; without a word back the sender
  // cannot tell whether the far end is keeping up
  std::string content(700, 'x');
  ask(putCommand("/cfg/devices/big.json", content));

  std::string answer;
  for (size_t i = 0; i < content.size(); i += 20) {
    configTransport::feed(content.substr(i, 20));
    answer += configTransport::takeOutput();
  }

  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "ACK 256"), answer.c_str());
  TEST_ASSERT_TRUE(contains(answer, "ACK 512"));
  TEST_ASSERT_TRUE(contains(answer, "OK"));
  TEST_ASSERT_TRUE(configStorage::load("/cfg/devices/big.json").usable());
}

void test_a_damaged_file_does_not_replace_a_good_one(void) {
  // the point of sending a crc along at all
  std::string good = "{\"good\":true}";
  configStorage::save("/cfg/devices/tv.json", good, 1);

  std::string broken = "{\"good\":false}";
  // announce the crc of one file, send another
  ask(putCommand("/cfg/devices/tv.json", good).substr(0, 20) + " " +
      std::to_string(broken.size()) + " ffffffff");
  configTransport::feed(broken);
  std::string answer = configTransport::takeOutput();

  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "crc mismatch"), answer.c_str());
  TEST_ASSERT_EQUAL_STRING(good.c_str(), configStorage::load("/cfg/devices/tv.json").payload.c_str());
}

void test_payload_bytes_are_never_read_as_commands(void) {
  // a JSON file is full of newlines, and one of its lines could easily read
  // like a command
  std::string content = "{\n\"a\":1,\nREBOOT\n}";
  ask(putCommand("/cfg/devices/x.json", content));
  configTransport::feed(content);
  configTransport::takeOutput();

  TEST_ASSERT_EQUAL_INT(0, rebootCalls);
  TEST_ASSERT_EQUAL_STRING(content.c_str(), configStorage::load("/cfg/devices/x.json").payload.c_str());
}

void test_an_interrupted_transfer_leaves_the_old_file_alone(void) {
  // the link dropped halfway through
  std::string good = "{\"good\":true}";
  configStorage::save("/cfg/devices/tv.json", good, 1);

  std::string content(500, 'y');
  ask(putCommand("/cfg/devices/tv.json", content));
  configTransport::feed(content.substr(0, 200));
  TEST_ASSERT_TRUE(configTransport::isReceiving());

  configTransport::reset();
  TEST_ASSERT_FALSE(configTransport::isReceiving());
  TEST_ASSERT_EQUAL_STRING(good.c_str(), configStorage::load("/cfg/devices/tv.json").payload.c_str());
}

void test_a_file_that_is_too_large_is_refused_before_a_byte_is_stored(void) {
  std::string answer =
      ask("PUT /cfg/devices/huge.json " + std::to_string(configTransport::MAX_FILE_SIZE + 1) +
          " deadbeef");

  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "too large"), answer.c_str());
  TEST_ASSERT_FALSE(configTransport::isReceiving());
}

void test_an_empty_file_is_allowed(void) {
  std::string answer = ask("PUT /cfg/devices/empty.json 0 00000000");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "OK"), answer.c_str());
  TEST_ASSERT_FALSE(configTransport::isReceiving());
}

void test_a_length_that_is_not_a_number_is_refused(void) {
  std::string answer = ask("PUT /cfg/devices/tv.json zwölf deadbeef");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "ERR"), answer.c_str());
  TEST_ASSERT_FALSE(configTransport::isReceiving());
}

// --- paths -------------------------------------------------------------------

static void assertPathRefused(const std::string &command) {
  std::string answer = ask(command);
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "ERR"), (command + " -> " + answer).c_str());
}

void test_a_path_cannot_climb_out_of_cfg(void) {
  assertPathRefused("GET /cfg/../secrets.h");
  assertPathRefused("GET /cfg/devices/../../etc/passwd");
  assertPathRefused("PUT /cfg/../boot.bin 10 deadbeef");
  assertPathRefused("DEL /cfg/../../nvs");
}

void test_a_path_outside_cfg_is_refused(void) {
  assertPathRefused("GET /etc/passwd");
  assertPathRefused("GET system.json");
  assertPathRefused("PUT /firmware.bin 10 deadbeef");
}

void test_a_backslash_is_refused(void) {
  assertPathRefused("GET /cfg/..\\secrets.h");
}

void test_a_directory_is_not_a_file(void) {
  assertPathRefused("GET /cfg/devices/");
}

// --- DEL ---------------------------------------------------------------------

void test_delete_removes_the_file_and_its_backup(void) {
  configStorage::save("/cfg/devices/tv.json", "{\"a\":1}", 1);
  configStorage::save("/cfg/devices/tv.json", "{\"a\":2}", 1);
  TEST_ASSERT_TRUE(fileSystem.exists("/cfg/devices/tv.json.bak"));

  std::string answer = ask("DEL /cfg/devices/tv.json");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "OK"), answer.c_str());
  TEST_ASSERT_FALSE(fileSystem.exists("/cfg/devices/tv.json"));
  TEST_ASSERT_FALSE_MESSAGE(fileSystem.exists("/cfg/devices/tv.json.bak"),
                            "a delete that leaves the backup is not a delete");
}

void test_deleting_something_that_is_not_there_says_so(void) {
  std::string answer = ask("DEL /cfg/devices/nope.json");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "ERR"), answer.c_str());
}

// --- the rest ----------------------------------------------------------------

void test_info_answers_with_what_the_firmware_provides(void) {
  std::string answer = ask("INFO");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "version 0.9.0-test"), answer.c_str());
  TEST_ASSERT_TRUE(contains(answer, "hardware 5"));
  TEST_ASSERT_TRUE(contains(answer, "END"));
}

void test_apply_and_reboot_reach_the_firmware(void) {
  ask("APPLY");
  TEST_ASSERT_EQUAL_INT(1, applyCalls);

  std::string answer = ask("REBOOT");
  TEST_ASSERT_EQUAL_INT(1, rebootCalls);
  // the answer has to go out before the restart, or nobody ever hears it
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "OK"), answer.c_str());
}

void test_an_unknown_command_is_named_in_the_answer(void) {
  std::string answer = ask("FLY");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "unknown command: FLY"), answer.c_str());
}

void test_crlf_from_a_terminal_is_tolerated(void) {
  configTransport::feed("LIST\r\n");
  std::string answer = configTransport::takeOutput();
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "OK 0"), answer.c_str());
}

void test_a_command_split_across_two_reads_still_works(void) {
  // a serial port hands over whatever happened to arrive
  configTransport::feed("LI");
  configTransport::feed("ST\n");
  std::string answer = configTransport::takeOutput();
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "OK 0"), answer.c_str());
}

void test_an_endless_line_is_dropped_rather_than_buffered(void) {
  // a stream that lost sync must not grow a buffer until the device runs out
  // of memory
  configTransport::feed(std::string(configTransport::MAX_LINE_LENGTH + 100, 'x'));
  std::string answer = configTransport::takeOutput();
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "line too long"), answer.c_str());
}

void test_several_commands_in_one_read_are_all_handled(void) {
  configTransport::feed("INFO\nLIST\n");
  std::string answer = configTransport::takeOutput();
  TEST_ASSERT_TRUE(contains(answer, "version 0.9.0-test"));
  TEST_ASSERT_TRUE(contains(answer, "OK 0"));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_list_of_an_empty_device_is_empty_not_an_error);
  RUN_TEST(test_list_reports_every_file_with_its_size);
  RUN_TEST(test_list_reports_the_size_get_would_hand_over);
  RUN_TEST(test_list_hides_the_storage_bookkeeping_files);
  RUN_TEST(test_get_returns_length_crc_and_the_payload);
  RUN_TEST(test_get_hands_over_the_payload_without_the_envelope);
  RUN_TEST(test_get_of_a_missing_file_says_so);
  RUN_TEST(test_get_of_a_damaged_file_says_damaged);
  RUN_TEST(test_a_file_arrives_and_is_stored);
  RUN_TEST(test_a_file_arriving_in_pieces_is_acknowledged_along_the_way);
  RUN_TEST(test_a_damaged_file_does_not_replace_a_good_one);
  RUN_TEST(test_payload_bytes_are_never_read_as_commands);
  RUN_TEST(test_an_interrupted_transfer_leaves_the_old_file_alone);
  RUN_TEST(test_a_file_that_is_too_large_is_refused_before_a_byte_is_stored);
  RUN_TEST(test_an_empty_file_is_allowed);
  RUN_TEST(test_a_length_that_is_not_a_number_is_refused);
  RUN_TEST(test_a_path_cannot_climb_out_of_cfg);
  RUN_TEST(test_a_path_outside_cfg_is_refused);
  RUN_TEST(test_a_backslash_is_refused);
  RUN_TEST(test_a_directory_is_not_a_file);
  RUN_TEST(test_delete_removes_the_file_and_its_backup);
  RUN_TEST(test_deleting_something_that_is_not_there_says_so);
  RUN_TEST(test_info_answers_with_what_the_firmware_provides);
  RUN_TEST(test_apply_and_reboot_reach_the_firmware);
  RUN_TEST(test_an_unknown_command_is_named_in_the_answer);
  RUN_TEST(test_crlf_from_a_terminal_is_tolerated);
  RUN_TEST(test_a_command_split_across_two_reads_still_works);
  RUN_TEST(test_an_endless_line_is_dropped_rather_than_buffered);
  RUN_TEST(test_several_commands_in_one_read_are_all_handled);
  return UNITY_END();
}
