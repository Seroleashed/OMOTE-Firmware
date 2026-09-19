#include "applicationInternal/scenes/sequenceEngine.h"

#include <deque>

#include "applicationInternal/commandHandler.h"
#include "applicationInternal/omote_log.h"

namespace sequenceEngine {

namespace {

std::deque<Step> queue;
// when the next step may run. 0 means "as soon as loop() comes round"
unsigned long nextStepAt = 0;
bool waiting = false;

} // namespace

void enqueue(const std::vector<Step> &steps) {
  for (size_t i = 0; i < steps.size(); i++) {
    queue.push_back(steps[i]);
  }
}

bool enqueueFromConfig(const std::vector<configModel::SequenceStep> &steps, std::string &error) {
  error.clear();
  bool complete = true;

  for (size_t i = 0; i < steps.size(); i++) {
    uint16_t command = get_commandID_byName(steps[i].commandName);
    if (command == COMMAND_UNKNOWN) {
      // Skip the one step, keep the rest. A scene that switches the TV on and
      // then selects an input it does not know should still switch the TV on.
      if (!error.empty()) error += ", ";
      error += steps[i].commandName;
      complete = false;
      omote_log_w("sequenceEngine: unknown command '%s', step skipped\r\n",
                  steps[i].commandName.c_str());
      continue;
    }

    Step step;
    step.command = command;
    step.payload = steps[i].payload;
    step.delayAfterMs = steps[i].delayAfterMs;
    queue.push_back(step);
  }

  if (!complete) error = "unknown command(s): " + error;
  return complete;
}

void abort() {
  if (!queue.empty()) {
    omote_log_d("sequenceEngine: %u pending step(s) dropped\r\n", (unsigned)queue.size());
  }
  queue.clear();
  waiting = false;
  nextStepAt = 0;
}

void loop(unsigned long currentMillis) {
  if (queue.empty()) {
    waiting = false;
    return;
  }

  if (waiting) {
    // Subtract first, then look at the sign. Comparing the timestamps directly
    // would stall the engine for 49 days when millis() wraps; this does not,
    // and it works whether long is 32 bit (ESP32) or 64 bit (simulator).
    if ((long)(currentMillis - nextStepAt) < 0) return;
  }

  Step step = queue.front();
  queue.pop_front();

  executeCommand(step.command, step.payload);

  if (step.delayAfterMs > 0 && !queue.empty()) {
    nextStepAt = currentMillis + step.delayAfterMs;
    waiting = true;
  } else {
    waiting = false;
  }
}

bool isRunning() { return !queue.empty(); }

size_t pendingSteps() { return queue.size(); }

std::vector<Step> takePending() {
  std::vector<Step> steps(queue.begin(), queue.end());
  queue.clear();
  waiting = false;
  nextStepAt = 0;
  return steps;
}

} // namespace sequenceEngine
