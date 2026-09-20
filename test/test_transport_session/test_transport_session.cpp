/*
  Unit tests for applicationInternal/transport/transportSession.cpp

  The session is what makes it safe to put a protocol on the serial port the log
  already owns. Two things have to hold, and both are the kind that only bite
  once somebody is standing there with a cable:

    * while a transfer runs, the log must not write a single byte into it
    * a session must always end, including when the cable is pulled - otherwise
      the log stays muted until the next restart and the device is silent for a
      reason the user cannot see
*/

#include <unity.h>

#include <string>

#include "applicationInternal/omote_log.h"
#include "applicationInternal/storage/configStorage.h"
#include "applicationInternal/transport/configTransport.h"
#include "applicationInternal/transport/transportSession.h"
#include "fake_filesystem.h"

// a byte stream the test drives from both ends
class FakeByteStream : public transportSession::ByteStream {
public:
  std::string toDevice;   // what the host has sent
  std::string fromDevice; // what the device answered

  size_t read(std::string &into) override {
    if (toDevice.empty()) return 0;
    size_t count = toDevice.size();
    into += toDevice;
    toDevice.clear();
    return count;
  }

  void write(const std::string &bytes) override { fromDevice += bytes; }

  std::string takeAnswer() {
    std::string answer = fromDevice;
    fromDevice.clear();
    return answer;
  }
};

static FakeByteStream stream;
static FakeFileSystem fileSystem;
static unsigned long clockMs = 0;

static bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

// what the host sends, then one turn of the main loop
static std::string send(const std::string &bytes) {
  stream.toDevice += bytes;
  transportSession::loop(clockMs);
  return stream.takeAnswer();
}

static void openSession() { send(std::string(transportSession::MAGIC) + "\n"); }

void setUp(void) {
  stream = FakeByteStream();
  fileSystem.reset();
  clockMs = 1000;
  omote_log_setMuted(0);

  configStorage::setFileSystem(&fileSystem);
  configTransport::Callbacks callbacks;
  configTransport::begin(&fileSystem, callbacks);
  transportSession::begin(&stream);
}

void tearDown(void) { omote_log_setMuted(0); }

// --- getting in and out ------------------------------------------------------

void test_nothing_happens_until_the_magic_line_arrives(void) {
  // outside a session the port belongs to the log, and somebody with a terminal
  // open gets to type whatever they like
  std::string answer = send("hello\nLIST\nwhat is this\n");
  TEST_ASSERT_EQUAL_STRING("", answer.c_str());
  TEST_ASSERT_FALSE(transportSession::isActive());
}

void test_the_magic_line_opens_a_session(void) {
  std::string answer = send(std::string(transportSession::MAGIC) + "\n");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "READY"), answer.c_str());
  TEST_ASSERT_TRUE(transportSession::isActive());
}

void test_bye_closes_it_again(void) {
  openSession();
  std::string answer = send("BYE\n");

  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "CLOSED"), answer.c_str());
  TEST_ASSERT_FALSE(transportSession::isActive());
}

void test_commands_reach_the_protocol_once_the_session_is_open(void) {
  fileSystem.files["/cfg/system.json"] = "{\"a\":1}";
  openSession();

  std::string answer = send("LIST\n");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "/cfg/system.json"), answer.c_str());
}

void test_a_command_in_the_same_read_as_the_magic_line_is_not_lost(void) {
  // a host that writes both in one go, which is the normal thing to do
  fileSystem.files["/cfg/system.json"] = "{\"a\":1}";

  std::string answer = send(std::string(transportSession::MAGIC) + "\nLIST\n");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "READY"), answer.c_str());
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "/cfg/system.json"), answer.c_str());
}

// --- the log ------------------------------------------------------------------

void test_the_log_goes_quiet_for_the_duration(void) {
  // a log line in the middle of a transfer corrupts the file being moved
  TEST_ASSERT_FALSE(omote_log_isMuted());

  openSession();
  TEST_ASSERT_TRUE_MESSAGE(omote_log_isMuted(), "the log has to be silent during a session");

  send("BYE\n");
  TEST_ASSERT_FALSE_MESSAGE(omote_log_isMuted(), "and has to come back afterwards");
}

void test_an_idle_session_closes_itself_and_gives_the_log_back(void) {
  /*
    The cable was pulled. Without this the log stays muted until the next
    restart, and the device is silent for a reason nobody can work out.
  */
  openSession();
  TEST_ASSERT_TRUE(omote_log_isMuted());

  clockMs += transportSession::IDLE_TIMEOUT_MS + 1;
  transportSession::loop(clockMs);

  TEST_ASSERT_FALSE(transportSession::isActive());
  TEST_ASSERT_FALSE(omote_log_isMuted());
}

void test_traffic_keeps_the_session_alive(void) {
  openSession();

  for (int i = 0; i < 5; i++) {
    clockMs += transportSession::IDLE_TIMEOUT_MS - 1000;
    send("LIST\n");
    TEST_ASSERT_TRUE_MESSAGE(transportSession::isActive(), "a busy session must not time out");
  }
}

void test_closing_from_the_firmware_gives_the_log_back(void) {
  // what the simulator does when a client disconnects
  openSession();
  transportSession::close();

  TEST_ASSERT_FALSE(transportSession::isActive());
  TEST_ASSERT_FALSE(omote_log_isMuted());
}

// --- the hard case -------------------------------------------------------------

void test_bye_inside_a_file_does_not_close_the_session(void) {
  /*
    A configuration file is text, and a line of it could easily read BYE. If the
    session layer went looking for that word in the byte stream it would close
    halfway through writing the file - and the file would be lost with no error
    anywhere. So the question is asked of the protocol, which knows it is in the
    middle of a transfer.
  */
  std::string content = "{\n\"note\": \"say\",\nBYE\n}";
  openSession();

  char crc[16];
  snprintf(crc, sizeof(crc), "%08x", (unsigned int)configStorage::crc32(content));
  send("PUT /cfg/devices/x.json " + std::to_string(content.size()) + " " + crc + "\n");

  std::string answer = send(content);

  TEST_ASSERT_TRUE_MESSAGE(transportSession::isActive(), "the session must survive its own payload");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "OK"), answer.c_str());
  TEST_ASSERT_EQUAL_STRING(content.c_str(),
                           configStorage::load("/cfg/devices/x.json").payload.c_str());
}

void test_a_transfer_split_over_several_loops_survives(void) {
  // which is what actually happens: the main loop takes whatever the serial
  // port has, a few hundred bytes at a time
  std::string content(900, 'z');
  openSession();

  char crc[16];
  snprintf(crc, sizeof(crc), "%08x", (unsigned int)configStorage::crc32(content));
  send("PUT /cfg/devices/big.json " + std::to_string(content.size()) + " " + crc + "\n");

  std::string answer;
  for (size_t i = 0; i < content.size(); i += 100) {
    clockMs += 10;
    answer += send(content.substr(i, 100));
  }

  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "OK"), answer.c_str());
  TEST_ASSERT_TRUE(configStorage::load("/cfg/devices/big.json").usable());
}

void test_a_terminal_left_open_does_not_grow_a_buffer(void) {
  // somebody leaning on the keyboard with no newline in sight
  for (int i = 0; i < 100; i++) send(std::string(60, 'k'));
  TEST_ASSERT_FALSE(transportSession::isActive());

  /*
    A host has to finish that half-written line before the magic word means
    anything - otherwise the two run together and the line reads
    "kkkk...OMOTE-CONFIG-V1", which is not the magic word and rightly ignored.
    omotectl sends the leading newline for exactly this reason; noise on the
    wire while the board boots produces the same situation.
  */
  std::string answer = send("\n" + std::string(transportSession::MAGIC) + "\n");
  TEST_ASSERT_TRUE_MESSAGE(contains(answer, "READY"), answer.c_str());
}

void test_noise_before_the_magic_word_on_the_same_line_is_ignored(void) {
  // the flip side: a line that merely ends with the magic word is not it, or a
  // log line quoting the protocol could open a session
  std::string answer = send("xyz" + std::string(transportSession::MAGIC) + "\n");
  TEST_ASSERT_FALSE(transportSession::isActive());
  TEST_ASSERT_EQUAL_STRING("", answer.c_str());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_nothing_happens_until_the_magic_line_arrives);
  RUN_TEST(test_the_magic_line_opens_a_session);
  RUN_TEST(test_bye_closes_it_again);
  RUN_TEST(test_commands_reach_the_protocol_once_the_session_is_open);
  RUN_TEST(test_a_command_in_the_same_read_as_the_magic_line_is_not_lost);
  RUN_TEST(test_the_log_goes_quiet_for_the_duration);
  RUN_TEST(test_an_idle_session_closes_itself_and_gives_the_log_back);
  RUN_TEST(test_traffic_keeps_the_session_alive);
  RUN_TEST(test_closing_from_the_firmware_gives_the_log_back);
  RUN_TEST(test_bye_inside_a_file_does_not_close_the_session);
  RUN_TEST(test_a_transfer_split_over_several_loops_survives);
  RUN_TEST(test_a_terminal_left_open_does_not_grow_a_buffer);
  RUN_TEST(test_noise_before_the_magic_word_on_the_same_line_is_ignored);
  return UNITY_END();
}
