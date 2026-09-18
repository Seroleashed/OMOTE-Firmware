#pragma once
/*
  Test doubles for everything the modules under test call but that would
  otherwise need real hardware, LVGL or a running scene handler.

  Everything the firmware calls through "hardwarePresenter.h" or through the
  gui_* headers is implemented here in a recording fake, so a test can assert
  on what the firmware *tried* to do:

      fakes::reset();
      fakes::setMillis(1000);
      executeCommand(MY_IR_COMMAND);
      TEST_ASSERT_EQUAL(1, fakes::irSends.size());

  The clock is fully controlled by the test (fakes::setMillis /
  fakes::advanceMillis), so key repeat and hold behaviour is deterministic
  and does not need any sleeping.
*/

#include <string>
#include <vector>
#include <list>

#include "applicationInternal/hardware/hardwarePresenter.h"

namespace fakes {

struct IRSend {
  int protocol;
  std::list<std::string> payloads;
  std::string additionalPayload;
};

struct MQTTPublish {
  std::string topic;
  std::string payload;
};

struct SceneCall {
  uint16_t command;
  std::list<std::string> payloads;
  std::string additionalPayload;
};

// --- recorded calls ---------------------------------------------------------
extern std::vector<IRSend> irSends;
extern std::vector<MQTTPublish> mqttPublishes;
extern std::vector<SceneCall> sceneCalls;
extern std::vector<SceneCall> guiCalls;
extern std::vector<std::string> shownIRMessages;
extern int activityTimestampsSet;

// --- controllable clock -----------------------------------------------------
// reset() advances the clock instead of resetting it, so it never jumps
// backwards between tests. Use setMillis() only if a test needs an absolute
// point in time.
void setMillis(unsigned long ms);
void advanceMillis(unsigned long ms);

// --- scene / gui state seen by keys.cpp and sceneRegistry.cpp ---------------
void setActiveSceneName(std::string sceneName);
void setActiveGUIname(std::string guiName);
void setActiveGUIlist(int guiList);

// --- keypad injection -------------------------------------------------------
// Row/col of the 5x5 matrix. keyChar is what the scene key maps use.
void pressKey(uint8_t row, uint8_t col, char keyChar);
void releaseKey(uint8_t row, uint8_t col);
void clearKeys();

// Registers COMMAND_UNKNOWN (and the example SPECIAL command) exactly like
// main.cpp does. Without this, COMMAND_UNKNOWN stays 0 and collides with the
// first command a test registers.
void initCommandIDs();

// keys.cpp keeps static state between calls (keyState, keyStateProcessed,
// lastTimeSent). Unity runs all tests of a file in one process, so that state
// leaks from one test into the next. settleKeypad() drives keypad_loop() until
// every key is back to IDLE.
void settleKeypad();

// --- reset everything between tests -----------------------------------------
void reset();

} // namespace fakes
