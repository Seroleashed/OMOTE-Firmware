/*
  Unit tests for applicationInternal/bootGuard.cpp

  Safe mode is the feature you only ever need when everything else is already
  broken, which is exactly why it has to be tested here rather than on the
  device. The fake storage below plays the part of NVS and survives a simulated
  restart, so a whole sequence of crashing boots can be replayed in one test.
*/

#include <unity.h>

#include "applicationInternal/bootGuard.h"

// --- storage that survives a simulated restart -------------------------------
class FakeBootCounterStorage : public BootCounterStorage {
public:
  uint8_t failedBoots = 0;
  bool safeModeRequested = false;
  int writes = 0;

  uint8_t readFailedBoots() override { return failedBoots; }
  void writeFailedBoots(uint8_t count) override {
    failedBoots = count;
    writes++;
  }
  bool readSafeModeRequested() override { return safeModeRequested; }
  void writeSafeModeRequested(bool requested) override { safeModeRequested = requested; }
};

static FakeBootCounterStorage storage;

void setUp(void) {
  storage = FakeBootCounterStorage();
}

void tearDown(void) {}

// a boot that reaches the end of setup()
static void bootSuccessfully() {
  bootGuard::begin(&storage);
  bootGuard::markBootSuccessful();
}

// a boot that dies somewhere in setup()
static void bootAndCrash() {
  bootGuard::begin(&storage);
}

// --- the normal case ---------------------------------------------------------

void test_a_normal_boot_is_not_safe_mode(void) {
  bootGuard::begin(&storage);
  TEST_ASSERT_FALSE(bootGuard::isSafeMode());
  TEST_ASSERT_EQUAL_STRING("normal", bootGuard::statusText().c_str());
}

void test_a_successful_boot_clears_the_counter(void) {
  bootGuard::begin(&storage);
  TEST_ASSERT_EQUAL_UINT8(1, storage.failedBoots);
  bootGuard::markBootSuccessful();
  TEST_ASSERT_EQUAL_UINT8(0, storage.failedBoots);
}

void test_many_successful_boots_never_reach_safe_mode(void) {
  for (int i = 0; i < 20; i++) {
    bootSuccessfully();
    TEST_ASSERT_FALSE(bootGuard::isSafeMode());
  }
}

// --- the crash case ----------------------------------------------------------

void test_two_crashes_are_not_enough(void) {
  // a single crash can also be a flaky power supply; that must not throw away
  // a working configuration
  bootAndCrash();
  bootAndCrash();
  bootGuard::begin(&storage);
  TEST_ASSERT_FALSE(bootGuard::isSafeMode());
}

void test_three_crashes_in_a_row_start_safe_mode(void) {
  bootAndCrash();
  bootAndCrash();
  bootAndCrash();

  bootGuard::begin(&storage);
  TEST_ASSERT_TRUE(bootGuard::isSafeMode());
  TEST_ASSERT_EQUAL_INT((int)bootGuard::SafeModeReason::RepeatedCrash, (int)bootGuard::safeModeReason());
  TEST_ASSERT_EQUAL_STRING("safe mode (repeated crash)", bootGuard::statusText().c_str());
}

void test_safe_mode_resets_the_counter_so_it_is_not_a_one_way_street(void) {
  bootAndCrash();
  bootAndCrash();
  bootAndCrash();

  // the rescue boot
  bootGuard::begin(&storage);
  TEST_ASSERT_TRUE(bootGuard::isSafeMode());
  TEST_ASSERT_EQUAL_UINT8(0, storage.failedBoots);

  // and the boot after that tries the stored configuration again
  bootGuard::begin(&storage);
  TEST_ASSERT_FALSE(bootGuard::isSafeMode());
}

void test_a_successful_boot_in_between_starts_counting_over(void) {
  bootAndCrash();
  bootAndCrash();
  bootSuccessfully();
  bootAndCrash();
  bootAndCrash();

  bootGuard::begin(&storage);
  TEST_ASSERT_FALSE(bootGuard::isSafeMode());
}

// --- the requested case ------------------------------------------------------

void test_requested_safe_mode_takes_effect_on_the_next_boot(void) {
  bootGuard::begin(&storage);
  TEST_ASSERT_FALSE(bootGuard::isSafeMode());
  bootGuard::requestSafeModeOnNextBoot();
  // still not safe mode: the request needs a restart
  TEST_ASSERT_FALSE(bootGuard::isSafeMode());
  TEST_ASSERT_TRUE(bootGuard::isSafeModeRequestedForNextBoot());

  bootGuard::begin(&storage);
  TEST_ASSERT_TRUE(bootGuard::isSafeMode());
  TEST_ASSERT_EQUAL_INT((int)bootGuard::SafeModeReason::Requested, (int)bootGuard::safeModeReason());
}

void test_a_request_is_consumed_after_one_boot(void) {
  bootGuard::begin(&storage);
  bootGuard::requestSafeModeOnNextBoot();

  bootGuard::begin(&storage);
  TEST_ASSERT_TRUE(bootGuard::isSafeMode());

  bootGuard::begin(&storage);
  TEST_ASSERT_FALSE(bootGuard::isSafeMode());
  TEST_ASSERT_FALSE(storage.safeModeRequested);
}

void test_a_request_can_be_cancelled_before_the_restart(void) {
  bootGuard::begin(&storage);
  bootGuard::requestSafeModeOnNextBoot();
  bootGuard::cancelSafeModeOnNextBoot();
  TEST_ASSERT_FALSE(bootGuard::isSafeModeRequestedForNextBoot());

  bootGuard::begin(&storage);
  TEST_ASSERT_FALSE(bootGuard::isSafeMode());
}

// --- robustness --------------------------------------------------------------

void test_without_storage_nothing_crashes_and_nothing_is_safe_mode(void) {
  // if the hardware layer ever hands over a null pointer, the firmware still
  // has to start - just without the protection
  bootGuard::begin(NULL);
  TEST_ASSERT_FALSE(bootGuard::isSafeMode());
  bootGuard::markBootSuccessful();
  bootGuard::requestSafeModeOnNextBoot();
  TEST_ASSERT_FALSE(bootGuard::isSafeModeRequestedForNextBoot());
}

void test_a_counter_beyond_the_threshold_also_triggers_safe_mode(void) {
  // e.g. a firmware that used a higher threshold before, or a corrupted value
  storage.failedBoots = 200;
  bootGuard::begin(&storage);
  TEST_ASSERT_TRUE(bootGuard::isSafeMode());
}

void test_the_counter_is_written_once_per_boot(void) {
  // it goes into NVS on every single start, so it must not be written in a loop
  bootGuard::begin(&storage);
  TEST_ASSERT_EQUAL_INT(1, storage.writes);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_normal_boot_is_not_safe_mode);
  RUN_TEST(test_a_successful_boot_clears_the_counter);
  RUN_TEST(test_many_successful_boots_never_reach_safe_mode);
  RUN_TEST(test_two_crashes_are_not_enough);
  RUN_TEST(test_three_crashes_in_a_row_start_safe_mode);
  RUN_TEST(test_safe_mode_resets_the_counter_so_it_is_not_a_one_way_street);
  RUN_TEST(test_a_successful_boot_in_between_starts_counting_over);
  RUN_TEST(test_requested_safe_mode_takes_effect_on_the_next_boot);
  RUN_TEST(test_a_request_is_consumed_after_one_boot);
  RUN_TEST(test_a_request_can_be_cancelled_before_the_restart);
  RUN_TEST(test_without_storage_nothing_crashes_and_nothing_is_safe_mode);
  RUN_TEST(test_a_counter_beyond_the_threshold_also_triggers_safe_mode);
  RUN_TEST(test_the_counter_is_written_once_per_boot);
  return UNITY_END();
}
