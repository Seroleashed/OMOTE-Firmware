/*
  Snapshot test for the whole registered command set.

  This is the safety net for the conversion to a JSON based configuration.
  Every device that main.cpp registers is registered here as well, and the
  resulting command table - name, handler and payloads - is written as plain
  text and compared against the reference in commands.snapshot.txt.

  Whenever a refactoring changes what a user would actually notice (a payload,
  a handler, a command that silently disappears), this test fails and shows the
  differing line. Steps that must *not* change behaviour (stable names, the
  sequence engine, the JSON loader with no JSON files present) are expected to
  leave the snapshot untouched.

  Command *ids* are deliberately not part of the snapshot: they are handed out
  in registration order and are allowed to move. The name is the stable
  reference, which is exactly what the configuration files rely on.

  ## Updating the reference

  Adding or removing a device changes the snapshot on purpose. Then:

      pio test -e native_test -f test_command_snapshot   # fails, writes .actual
      mv test/test_command_snapshot/commands.actual.txt \
         test/test_command_snapshot/commands.snapshot.txt

  Review the diff before committing - that diff is the point of this test.
*/

#include <unity.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "applicationInternal/commandHandler.h"
#include "applicationInternal/storage/configModel.h"
#include "omote_fakes.h"

// the devices main.cpp registers. Everything that needs LVGL or real hardware
// stays out of this list; those parts are covered by the gui tests.
#include "devices/TV/device_samsungTV/device_samsungTV.h"
#include "devices/AVreceiver/device_yamahaAmp/device_yamahaAmp.h"
#include "devices/mediaPlayer/device_appleTV/device_appleTV.h"
#include "devices/misc/device_smarthome/device_smarthome.h"

static const char *const SNAPSHOT_FILE = "commands.snapshot.txt";
static const char *const ACTUAL_FILE = "commands.actual.txt";

// --- file helpers ------------------------------------------------------------

/*
  The reference file lives next to this source file. __FILE__ is either
  absolute or relative to the project directory, and the test binary is started
  from the project directory, so both forms resolve. If they ever do not, the
  test still prints the complete snapshot to stdout, so nothing is lost.
*/
static std::string testFileDirectory() {
  std::string path = __FILE__;
  size_t slash = path.find_last_of("/\\");
  if (slash == std::string::npos) return std::string(".");
  return path.substr(0, slash);
}

static std::string snapshotPath(const char *fileName) {
  return testFileDirectory() + "/" + fileName;
}

static bool readFile(const std::string &path, std::string &content) {
  std::FILE *file = std::fopen(path.c_str(), "rb");
  if (file == NULL) return false;

  content.clear();
  char buffer[1024];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    content.append(buffer, read);
  }
  std::fclose(file);
  return true;
}

static bool writeFile(const std::string &path, const std::string &content) {
  std::FILE *file = std::fopen(path.c_str(), "wb");
  if (file == NULL) return false;
  size_t written = std::fwrite(content.data(), 1, content.size(), file);
  std::fclose(file);
  return written == content.size();
}

// --- building the snapshot ---------------------------------------------------

// keeps one command on one line, whatever a payload contains
static std::string escapePayload(const std::string &payload) {
  std::string result;
  for (size_t i = 0; i < payload.size(); i++) {
    char character = payload[i];
    if (character == '\n') {
      result += "\\n";
    } else if (character == '\r') {
      result += "\\r";
    } else if (character == '|') {
      result += "\\|";
    } else {
      result += character;
    }
  }
  return result;
}

static std::vector<std::string> snapshotLines() {
  std::vector<std::string> lines;

  const std::map<uint16_t, std::string> &names = get_all_commandNames();
  for (std::map<uint16_t, std::string>::const_iterator it = names.begin(); it != names.end(); ++it) {
    commandData data;
    if (!get_commandData_byID(it->first, data)) continue;

    std::string line = it->second + " | " + configModel::handlerToString(data.commandHandler);
    for (std::list<std::string>::const_iterator payload = data.commandPayloads.begin();
         payload != data.commandPayloads.end(); ++payload) {
      line += " | " + escapePayload(*payload);
    }
    lines.push_back(line);
  }

  // sorted by name, so the file only changes when a command changes - not when
  // the registration order does
  std::sort(lines.begin(), lines.end());
  return lines;
}

static std::string buildSnapshot() {
  std::vector<std::string> lines = snapshotLines();

  std::string snapshot;
  snapshot += "# OMOTE command snapshot\n";
  snapshot += "# Generated by test/test_command_snapshot. Do not edit by hand.\n";
  snapshot += "# Format: NAME | HANDLER | payload | payload ...\n";
  snapshot += "# Sorted by name. Command ids are not part of the snapshot.\n";
  snapshot += "# commands: " + std::to_string(lines.size()) + "\n";
  for (size_t i = 0; i < lines.size(); i++) {
    snapshot += lines[i];
    snapshot += "\n";
  }
  return snapshot;
}

static std::string firstDifference(const std::string &expected, const std::string &actual) {
  std::vector<std::string> expectedLines;
  std::vector<std::string> actualLines;
  size_t start = 0;
  for (size_t i = 0; i <= expected.size(); i++) {
    if (i == expected.size() || expected[i] == '\n') {
      expectedLines.push_back(expected.substr(start, i - start));
      start = i + 1;
    }
  }
  start = 0;
  for (size_t i = 0; i <= actual.size(); i++) {
    if (i == actual.size() || actual[i] == '\n') {
      actualLines.push_back(actual.substr(start, i - start));
      start = i + 1;
    }
  }

  size_t lineCount = std::max(expectedLines.size(), actualLines.size());
  for (size_t i = 0; i < lineCount; i++) {
    std::string expectedLine = i < expectedLines.size() ? expectedLines[i] : std::string("<missing>");
    std::string actualLine = i < actualLines.size() ? actualLines[i] : std::string("<missing>");
    if (expectedLine != actualLine) {
      return "line " + std::to_string(i + 1) + "\n  reference: " + expectedLine + "\n  now:       " + actualLine;
    }
  }
  return "files differ only in trailing whitespace";
}

// --- test setup --------------------------------------------------------------

static void registerAllDevicesOnce() {
  // registering twice would hand out new ids for the same names and double the
  // command table, so this happens exactly once per test binary
  static bool done = false;
  if (done) return;
  done = true;

  // fakes::reset() has already called register_specialCommands(), the same way
  // main.cpp does
  register_device_samsungTV();
  register_device_yamahaAmp();
  register_device_appleTV();
  register_device_smarthome();
  register_keyboardCommands();
}

void setUp(void) {
  fakes::reset();
  registerAllDevicesOnce();
}

void tearDown(void) {}

// --- tests -------------------------------------------------------------------

void test_command_snapshot_matches_the_reference(void) {
  std::string actual = buildSnapshot();

  std::string expected;
  if (!readFile(snapshotPath(SNAPSHOT_FILE), expected)) {
    writeFile(snapshotPath(SNAPSHOT_FILE), actual);
    std::printf("\n--- no reference snapshot found, this is the current one ---\n%s"
                "--- end of snapshot ---\n",
                actual.c_str());
    TEST_FAIL_MESSAGE("no reference snapshot existed. One was written to "
                      "test/test_command_snapshot/commands.snapshot.txt - review it and commit it.");
  }

  if (expected != actual) {
    writeFile(snapshotPath(ACTUAL_FILE), actual);
    std::string message = "the registered commands changed: " + firstDifference(expected, actual) +
                          "\nFull output was written to test/test_command_snapshot/commands.actual.txt";
    TEST_FAIL_MESSAGE(message.c_str());
  }
}

void test_the_snapshot_is_not_empty(void) {
  // guards against the snapshot silently becoming a file with only comments in
  // it, which would make the test pass for the wrong reason
  TEST_ASSERT_GREATER_THAN_size_t(50, snapshotLines().size());
}

void test_every_registered_command_resolves_back_to_its_name(void) {
  const std::map<uint16_t, std::string> &names = get_all_commandNames();
  for (std::map<uint16_t, std::string>::const_iterator it = names.begin(); it != names.end(); ++it) {
    TEST_ASSERT_EQUAL_UINT16(it->first, get_commandID_byName(it->second));
    TEST_ASSERT_EQUAL_STRING(it->second.c_str(), get_commandName_byID(it->first).c_str());
  }
}

void test_no_command_name_is_used_twice(void) {
  std::vector<std::string> names;
  const std::map<uint16_t, std::string> &registered = get_all_commandNames();
  for (std::map<uint16_t, std::string>::const_iterator it = registered.begin(); it != registered.end(); ++it) {
    names.push_back(it->second);
  }
  std::sort(names.begin(), names.end());
  TEST_ASSERT_EQUAL_size_t(names.size(), std::unique(names.begin(), names.end()) - names.begin());
}

void test_every_command_has_a_handler_the_export_understands(void) {
  // a command whose handler has no name cannot be written to a device pack, so
  // it would silently vanish on export
  const std::map<uint16_t, commandData> &commands = get_all_commands();
  for (std::map<uint16_t, commandData>::const_iterator it = commands.begin(); it != commands.end(); ++it) {
    std::string handler = configModel::handlerToString(it->second.commandHandler);
    if (handler.empty()) {
      std::string message = "command '" + get_commandName_byID(it->first) + "' has a handler without a name";
      TEST_FAIL_MESSAGE(message.c_str());
    }
  }
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_command_snapshot_matches_the_reference);
  RUN_TEST(test_the_snapshot_is_not_empty);
  RUN_TEST(test_every_registered_command_resolves_back_to_its_name);
  RUN_TEST(test_no_command_name_is_used_twice);
  RUN_TEST(test_every_command_has_a_handler_the_export_understands);
  return UNITY_END();
}
