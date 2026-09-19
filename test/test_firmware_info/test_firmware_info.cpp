/*
  Unit tests for applicationInternal/firmwareInfo.cpp

  Small module, but the date conversion is the kind of code that is wrong for
  eleven months of the year and nobody notices, because the only way to see it
  in the firmware is to rebuild on the right day.
*/

#include <unity.h>

#include <string>

#include "applicationInternal/firmwareInfo.h"

void setUp(void) {}
void tearDown(void) {}

void test_compiler_date_becomes_iso_date(void) {
  TEST_ASSERT_EQUAL_STRING("2026-09-19", firmwareInfo::normalizeCompilerDate("Sep 19 2026").c_str());
  TEST_ASSERT_EQUAL_STRING("2024-01-31", firmwareInfo::normalizeCompilerDate("Jan 31 2024").c_str());
  TEST_ASSERT_EQUAL_STRING("2025-12-25", firmwareInfo::normalizeCompilerDate("Dec 25 2025").c_str());
}

void test_single_digit_day_is_padded(void) {
  // __DATE__ pads the day with a space, not with a zero
  TEST_ASSERT_EQUAL_STRING("2026-09-01", firmwareInfo::normalizeCompilerDate("Sep  1 2026").c_str());
}

void test_every_month_is_known(void) {
  static const char *const months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (int i = 0; i < 12; i++) {
    std::string input = std::string(months[i]) + " 15 2026";
    std::string expected = "2026-" + std::string(i + 1 < 10 ? "0" : "") + std::to_string(i + 1) + "-15";
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), firmwareInfo::normalizeCompilerDate(input).c_str());
  }
}

void test_unexpected_input_is_passed_through_unchanged(void) {
  // never throw away information just because it does not parse
  TEST_ASSERT_EQUAL_STRING("", firmwareInfo::normalizeCompilerDate("").c_str());
  TEST_ASSERT_EQUAL_STRING("not a date", firmwareInfo::normalizeCompilerDate("not a date").c_str());
  TEST_ASSERT_EQUAL_STRING("Foo 19 2026", firmwareInfo::normalizeCompilerDate("Foo 19 2026").c_str());
  TEST_ASSERT_EQUAL_STRING("Sep xx 2026", firmwareInfo::normalizeCompilerDate("Sep xx 2026").c_str());
  TEST_ASSERT_EQUAL_STRING("Sep 19 20x6", firmwareInfo::normalizeCompilerDate("Sep 19 20x6").c_str());
}

void test_version_is_never_empty(void) {
  // without -D OMOTE_FIRMWARE_VERSION the fallback has to kick in, otherwise
  // the settings screen would show a blank line
  TEST_ASSERT_TRUE(firmwareInfo::version().size() > 0);
}

void test_version_line_contains_version_and_date(void) {
  std::string line = firmwareInfo::versionLine();
  TEST_ASSERT_TRUE(line.find(firmwareInfo::version()) != std::string::npos);
  TEST_ASSERT_TRUE(line.find(firmwareInfo::buildDate()) != std::string::npos);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_compiler_date_becomes_iso_date);
  RUN_TEST(test_single_digit_day_is_padded);
  RUN_TEST(test_every_month_is_known);
  RUN_TEST(test_unexpected_input_is_passed_through_unchanged);
  RUN_TEST(test_version_is_never_empty);
  RUN_TEST(test_version_line_contains_version_and_date);
  return UNITY_END();
}
