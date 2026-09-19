/*
  Unit tests for applicationInternal/scenes/sequenceEngine.cpp

  The engine exists so a scene stops holding the main loop hostage. These tests
  drive it with a clock the test owns, so a 3.5 second power-off sequence is
  checked in microseconds and the timing is exact rather than approximate.

  What they pin down above all is the order of things when scenes are switched:
  the end sequence of the old scene has to finish before the start sequence of
  the new one begins, because that is what delay() used to guarantee and
  scene_allOff depends on it.
*/

#include <unity.h>

#include <vector>

#include "applicationInternal/commandHandler.h"
#include "applicationInternal/hardware/IRremoteProtocols.h"
#include "applicationInternal/scenes/sequenceEngine.h"
#include "omote_fakes.h"

static uint16_t CMD_A;
static uint16_t CMD_B;
static uint16_t CMD_C;

static unsigned long clockMs = 0;

// drives the engine the way the main loop does: often, and with the current time
static void runLoopUntil(unsigned long targetMs) {
  while (clockMs < targetMs) {
    clockMs += 10;
    sequenceEngine::loop(clockMs);
  }
}

static void tick() { sequenceEngine::loop(clockMs); }

void setUp(void) {
  fakes::reset();
  sequenceEngine::abort();
  clockMs = 1000;

  register_command(&CMD_A, makeCommandData(IR, {std::to_string(IR_PROTOCOL_SAMSUNG), "0xAAAA"}));
  register_command(&CMD_B, makeCommandData(IR, {std::to_string(IR_PROTOCOL_SAMSUNG), "0xBBBB"}));
  register_command(&CMD_C, makeCommandData(IR, {std::to_string(IR_PROTOCOL_SAMSUNG), "0xCCCC"}));
}

void tearDown(void) { sequenceEngine::abort(); }

// --- basics ------------------------------------------------------------------

void test_an_empty_queue_does_nothing(void) {
  tick();
  TEST_ASSERT_FALSE(sequenceEngine::isRunning());
  TEST_ASSERT_EQUAL_size_t(0, fakes::irSends.size());
}

void test_a_single_step_runs_on_the_next_loop(void) {
  sequenceEngine::enqueue({{CMD_A, "", 0}});
  TEST_ASSERT_TRUE(sequenceEngine::isRunning());
  TEST_ASSERT_EQUAL_size_t(0, fakes::irSends.size()); // not yet - enqueue does not execute

  tick();
  TEST_ASSERT_EQUAL_size_t(1, fakes::irSends.size());
  TEST_ASSERT_FALSE(sequenceEngine::isRunning());
}

void test_steps_wait_for_their_delay(void) {
  sequenceEngine::enqueue({{CMD_A, "", 500}, {CMD_B, "", 0}});

  tick();
  TEST_ASSERT_EQUAL_size_t(1, fakes::irSends.size());

  // 490 ms later the second step must still be waiting
  runLoopUntil(clockMs + 490);
  TEST_ASSERT_EQUAL_size_t(1, fakes::irSends.size());

  runLoopUntil(clockMs + 20);
  TEST_ASSERT_EQUAL_size_t(2, fakes::irSends.size());
  TEST_ASSERT_EQUAL_STRING("0xBBBB", fakes::irSends[1].payloads.front().c_str());
}

void test_the_whole_sequence_runs_in_order(void) {
  sequenceEngine::enqueue({{CMD_A, "", 500}, {CMD_B, "", 1500}, {CMD_C, "", 0}});

  runLoopUntil(clockMs + 3000);

  TEST_ASSERT_EQUAL_size_t(3, fakes::irSends.size());
  TEST_ASSERT_EQUAL_STRING("0xAAAA", fakes::irSends[0].payloads.front().c_str());
  TEST_ASSERT_EQUAL_STRING("0xBBBB", fakes::irSends[1].payloads.front().c_str());
  TEST_ASSERT_EQUAL_STRING("0xCCCC", fakes::irSends[2].payloads.front().c_str());
  TEST_ASSERT_FALSE(sequenceEngine::isRunning());
}

void test_a_payload_is_passed_on(void) {
  static uint16_t CMD_MQTT;
  register_command(&CMD_MQTT, makeCommandData(MQTT, {"topic", "default"}));
  sequenceEngine::enqueue({{CMD_MQTT, "fromSequence", 0}});

  tick();
  TEST_ASSERT_EQUAL_size_t(1, fakes::mqttPublishes.size());
  TEST_ASSERT_EQUAL_STRING("fromSequence", fakes::mqttPublishes[0].payload.c_str());
}

void test_the_loop_is_never_blocked(void) {
  // the whole point: a 3.5 second sequence must not cost a single loop
  // iteration more than any other
  sequenceEngine::enqueue({{CMD_A, "", 3500}, {CMD_B, "", 0}});

  int loopsWhileWaiting = 0;
  unsigned long until = clockMs + 3400;
  while (clockMs < until) {
    clockMs += 10;
    sequenceEngine::loop(clockMs);
    loopsWhileWaiting++;
  }

  TEST_ASSERT_TRUE_MESSAGE(loopsWhileWaiting > 300, "the loop should have run hundreds of times");
  TEST_ASSERT_EQUAL_size_t(1, fakes::irSends.size());
}

// --- queueing, which is what scene switching relies on -----------------------

void test_enqueue_appends_and_does_not_replace(void) {
  /*
    Switching scenes calls the end sequence of the old scene and then the start
    sequence of the new one, back to back. With delay() the end sequence
    finished first; the engine has to keep that. scene_allOff sends seven
    power-off commands over 3.5 seconds - dropping them would leave the devices
    on.
  */
  sequenceEngine::enqueue({{CMD_A, "", 500}, {CMD_B, "", 500}});
  sequenceEngine::enqueue({{CMD_C, "", 0}});

  TEST_ASSERT_EQUAL_size_t(3, sequenceEngine::pendingSteps());
  runLoopUntil(clockMs + 2000);

  TEST_ASSERT_EQUAL_size_t(3, fakes::irSends.size());
  TEST_ASSERT_EQUAL_STRING("0xAAAA", fakes::irSends[0].payloads.front().c_str());
  TEST_ASSERT_EQUAL_STRING("0xBBBB", fakes::irSends[1].payloads.front().c_str());
  TEST_ASSERT_EQUAL_STRING("0xCCCC", fakes::irSends[2].payloads.front().c_str());
}

void test_the_delay_between_two_queued_sequences_is_kept(void) {
  // the last step of the end sequence still has its pause before the first step
  // of the start sequence
  sequenceEngine::enqueue({{CMD_A, "", 500}});
  sequenceEngine::enqueue({{CMD_B, "", 0}});

  tick();
  TEST_ASSERT_EQUAL_size_t(1, fakes::irSends.size());
  runLoopUntil(clockMs + 400);
  TEST_ASSERT_EQUAL_size_t(1, fakes::irSends.size());
  runLoopUntil(clockMs + 200);
  TEST_ASSERT_EQUAL_size_t(2, fakes::irSends.size());
}

void test_abort_drops_everything_pending(void) {
  // a new scene switch: what is still queued belongs to a scene the user has
  // moved on from
  sequenceEngine::enqueue({{CMD_A, "", 500}, {CMD_B, "", 500}, {CMD_C, "", 0}});
  tick();
  TEST_ASSERT_EQUAL_size_t(1, fakes::irSends.size());

  sequenceEngine::abort();
  TEST_ASSERT_FALSE(sequenceEngine::isRunning());

  runLoopUntil(clockMs + 5000);
  TEST_ASSERT_EQUAL_size_t(1, fakes::irSends.size());
}

void test_after_an_abort_a_new_sequence_starts_immediately(void) {
  // and does not inherit the pause the aborted one was in the middle of
  sequenceEngine::enqueue({{CMD_A, "", 5000}, {CMD_B, "", 0}});
  tick();
  sequenceEngine::abort();

  sequenceEngine::enqueue({{CMD_C, "", 0}});
  tick();
  TEST_ASSERT_EQUAL_size_t(2, fakes::irSends.size());
  TEST_ASSERT_EQUAL_STRING("0xCCCC", fakes::irSends[1].payloads.front().c_str());
}

// --- from the configuration format -------------------------------------------

void test_a_sequence_from_config_resolves_commands_by_name(void) {
  std::vector<configModel::SequenceStep> steps;
  configModel::SequenceStep first;
  first.commandName = "CMD_A";
  first.delayAfterMs = 500;
  steps.push_back(first);
  configModel::SequenceStep second;
  second.commandName = "CMD_B";
  steps.push_back(second);

  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(sequenceEngine::enqueueFromConfig(steps, error), error.c_str());

  runLoopUntil(clockMs + 1000);
  TEST_ASSERT_EQUAL_size_t(2, fakes::irSends.size());
}

void test_an_unknown_command_is_skipped_and_named(void) {
  // a scene that switches the TV on and then selects an input it does not know
  // should still switch the TV on
  std::vector<configModel::SequenceStep> steps;
  configModel::SequenceStep good;
  good.commandName = "CMD_A";
  steps.push_back(good);
  configModel::SequenceStep bad;
  bad.commandName = "DOES_NOT_EXIST";
  steps.push_back(bad);

  std::string error;
  TEST_ASSERT_FALSE(sequenceEngine::enqueueFromConfig(steps, error));
  TEST_ASSERT_TRUE_MESSAGE(error.find("DOES_NOT_EXIST") != std::string::npos, error.c_str());

  runLoopUntil(clockMs + 100);
  TEST_ASSERT_EQUAL_size_t(1, fakes::irSends.size());
}

// --- robustness --------------------------------------------------------------

void test_the_engine_survives_the_millis_wraparound(void) {
  /*
    millis() wraps after 49 days. Comparing timestamps directly would stall a
    sequence for the next 49 days, and the device that happens to is the one
    that has been on a shelf for two months.
  */
  clockMs = 0xFFFFFF00; // 256 ms before the wrap
  sequenceEngine::enqueue({{CMD_A, "", 500}, {CMD_B, "", 0}});

  sequenceEngine::loop(clockMs);
  TEST_ASSERT_EQUAL_size_t(1, fakes::irSends.size());

  // step past the wrap in small increments, the way the real loop does
  for (int i = 0; i < 100; i++) {
    clockMs += 10;
    sequenceEngine::loop(clockMs);
  }
  TEST_ASSERT_EQUAL_size_t(2, fakes::irSends.size());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_an_empty_queue_does_nothing);
  RUN_TEST(test_a_single_step_runs_on_the_next_loop);
  RUN_TEST(test_steps_wait_for_their_delay);
  RUN_TEST(test_the_whole_sequence_runs_in_order);
  RUN_TEST(test_a_payload_is_passed_on);
  RUN_TEST(test_the_loop_is_never_blocked);
  RUN_TEST(test_enqueue_appends_and_does_not_replace);
  RUN_TEST(test_the_delay_between_two_queued_sequences_is_kept);
  RUN_TEST(test_abort_drops_everything_pending);
  RUN_TEST(test_after_an_abort_a_new_sequence_starts_immediately);
  RUN_TEST(test_a_sequence_from_config_resolves_commands_by_name);
  RUN_TEST(test_an_unknown_command_is_skipped_and_named);
  RUN_TEST(test_the_engine_survives_the_millis_wraparound);
  return UNITY_END();
}
