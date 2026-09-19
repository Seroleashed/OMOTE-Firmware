#include "omote_fakes.h"

#include <cstdarg>
#include <cstdio>
#include <map>

#include "applicationInternal/commandHandler.h"
#include "applicationInternal/scenes/sceneHandler.h"
#include "applicationInternal/scenes/sceneRegistry.h"
#include "applicationInternal/gui/guiMemoryOptimizer.h"
#include "applicationInternal/gui/guiBase.h"
#include "guis/gui_irReceiver.h"
#include "scenes/scene__default.h"
#include "devices/misc/device_specialCommands.h"
#include "applicationInternal/keys.h"

namespace fakes {

std::vector<IRSend> irSends;
std::vector<MQTTPublish> mqttPublishes;
std::vector<SceneCall> sceneCalls;
std::vector<SceneCall> guiCalls;
std::vector<std::string> shownIRMessages;
int activityTimestampsSet = 0;

// The layout the faked get_keypadMatrix() reports. Off by default, so a test
// that does not care about the matrix sees the same "no matrix" answer the
// simulator gives.
bool keypadMatrixAvailable = false;
char keypadMatrix[keypadROWS][keypadCOLS] = {{0}};

void setKeypadMatrix(const char (*matrix)[keypadCOLS]) {
  keypadMatrixAvailable = true;
  for (uint8_t row = 0; row < keypadROWS; row++) {
    for (uint8_t col = 0; col < keypadCOLS; col++) {
      keypadMatrix[row][col] = matrix[row][col];
    }
  }
}

void clearKeypadMatrix() { keypadMatrixAvailable = false; }

static unsigned long fakeMillis = 10000;
static std::string activeSceneName = "";

void setMillis(unsigned long ms) { fakeMillis = ms; }
void advanceMillis(unsigned long ms) { fakeMillis += ms; }
unsigned long currentMillis() { return fakeMillis; }

void setActiveSceneName(std::string sceneName) { activeSceneName = sceneName; }
std::string getActiveSceneName() { return activeSceneName; }

static std::string activeGUInameForTest = "";
static int activeGUIlistForTest = 0;
void setActiveGUIname(std::string guiName) { activeGUInameForTest = guiName; }
std::string getActiveGUIname() { return activeGUInameForTest; }
void setActiveGUIlist(int guiList) { activeGUIlistForTest = guiList; }
void pollFakeKeys();
int getActiveGUIlist() { return activeGUIlistForTest; }

// The fake keypad mirrors the real hardware: getKeys() is polled every
// keypad_loop() and re-writes the raw matrix. Simply writing rawKeys once from
// the test would not be faithful - keys.cpp resets released keys to IDLE_RAW
// and expects the driver to re-report a key that is still held.
struct FakeKey {
  char keyChar = NO_KEY;
  bool held = false;
  bool releasePending = false;
  unsigned long pressedAt = 0;
};
static FakeKey fakeKeys[keypadROWS][keypadCOLS];

void pressKey(uint8_t row, uint8_t col, char keyChar) {
  fakeKeys[row][col].keyChar = keyChar;
  fakeKeys[row][col].held = true;
  fakeKeys[row][col].releasePending = false;
  fakeKeys[row][col].pressedAt = fakeMillis;
}

void releaseKey(uint8_t row, uint8_t col) {
  fakeKeys[row][col].held = false;
  fakeKeys[row][col].releasePending = true;
}

void clearKeys() {
  for (uint8_t row = 0; row < keypadROWS; row++) {
    for (uint8_t col = 0; col < keypadCOLS; col++) {
      fakeKeys[row][col] = FakeKey{};
      rawKeys[row][col] = {0, NO_KEY, IDLE_RAW};
    }
  }
}

// called by the faked getKeys() below
void pollFakeKeys() {
  for (uint8_t row = 0; row < keypadROWS; row++) {
    for (uint8_t col = 0; col < keypadCOLS; col++) {
      FakeKey &key = fakeKeys[row][col];
      if (key.held) {
        rawKeys[row][col].keyChar = key.keyChar;
        rawKeys[row][col].rawKeyState = PRESSED_RAW;
        rawKeys[row][col].timestampReceived = key.pressedAt;
      } else if (key.releasePending) {
        rawKeys[row][col].keyChar = key.keyChar;
        rawKeys[row][col].rawKeyState = RELEASED_RAW;
        rawKeys[row][col].timestampReceived = fakeMillis;
        key.releasePending = false;
      }
    }
  }
}

void initCommandIDs() {
  static bool done = false;
  if (!done) {
    // same call main.cpp makes; gives COMMAND_UNKNOWN its own id so it can
    // never collide with a command a test registers
    register_specialCommands();
    done = true;
  }
}

void settleKeypad() {
  clearKeys();
  advanceMillis(1000);
  // two rounds: the first consumes any RELEASED state, the second lets every
  // key settle at IDLE
  keypad_loop();
  keypad_loop();
}

void reset() {
  irSends.clear();
  mqttPublishes.clear();
  sceneCalls.clear();
  guiCalls.clear();
  shownIRMessages.clear();
  activityTimestampsSet = 0;
  // The clock never jumps backwards between tests: keys.cpp keeps static
  // timestamps (lastTimeSent), and a clock that goes back would silently
  // rate-limit the first key press of the next test.
  advanceMillis(60000);
  initCommandIDs();
  settleKeypad();
  activeSceneName = "";
  activeGUInameForTest = "";
  activeGUIlistForTest = 0;
  clearKeys();
}

} // namespace fakes

// ============================================================================
// Arduino layer (normally provided by hardware/windows_linux/..., but we want
// a clock the test controls instead of the wall clock)
// ============================================================================
unsigned long millis() { return fakes::currentMillis(); }
void delay(uint32_t ms) { fakes::advanceMillis(ms); }

SerialClass Serial;
void SerialClass::begin(unsigned long) {}
size_t SerialClass::printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  int ret = vprintf(format, args);
  va_end(args);
  return ret;
}
size_t SerialClass::println(const char c[]) { return printf("%s\r\n", c); }
size_t SerialClass::println(int nr) { return printf("%d\r\n", nr); }

// ============================================================================
// hardwarePresenter.h - hardware facade
// ============================================================================
void init_hardware_general(void) {}

void init_preferences(void) {}
void save_preferences(void) {}
std::string get_activeScene() { return fakes::getActiveSceneName(); }
void set_activeScene(std::string anActiveScene) { fakes::setActiveSceneName(anActiveScene); }
static std::string activeGUIname = "";
std::string get_activeGUIname() { return activeGUIname; }
void set_activeGUIname(std::string anActiveGUIname) { activeGUIname = anActiveGUIname; }
static int activeGUIlist = 0;
int get_activeGUIlist() { return activeGUIlist; }
void set_activeGUIlist(int anActiveGUIlist) { activeGUIlist = anActiveGUIlist; }
static int lastActiveGUIlistIndex = 0;
int get_lastActiveGUIlistIndex() { return lastActiveGUIlistIndex; }
void set_lastActiveGUIlistIndex(int aGUIlistIndex) { lastActiveGUIlistIndex = aGUIlistIndex; }

void init_userled(void) {}
void update_userled() {}

void init_battery(void) {}
void get_battery_status(int *battery_voltage, int *battery_percentage, bool *battery_ischarging) {
  *battery_voltage = 4100;
  *battery_percentage = 87;
  *battery_ischarging = false;
}

void init_sleep() {}
void init_IMU() {}
void check_activity() {}
void setLastActivityTimestamp() { fakes::activityTimestampsSet++; }
static uint32_t sleepTimeout = 30000;
uint32_t get_sleepTimeout() { return sleepTimeout; }
void set_sleepTimeout(uint32_t aSleepTimeout) { sleepTimeout = aSleepTimeout; }
static bool wakeupByIMUEnabled = true;
bool get_wakeupByIMUEnabled() { return wakeupByIMUEnabled; }
void set_wakeupByIMUEnabled(bool aWakeupByIMUEnabled) { wakeupByIMUEnabled = aWakeupByIMUEnabled; }
static uint8_t motionThreshold = 50;
uint8_t get_motionThreshold() { return motionThreshold; }
void set_motionThreshold(uint8_t aMotionThreshold) { motionThreshold = aMotionThreshold; }

// keypad: the firmware reads this array, the test writes it
rawKey rawKeys[keypadROWS][keypadCOLS];
void init_keys(void) {}
void getKeys(rawKey (*keys)[keypadCOLS], unsigned long currentMillis) {
  (void)keys;
  (void)currentMillis;
  fakes::pollFakeKeys();
}
bool get_keypadMatrix(char (*matrix)[keypadCOLS]) {
  if (!fakes::keypadMatrixAvailable) return false;
  for (uint8_t row = 0; row < keypadROWS; row++) {
    for (uint8_t col = 0; col < keypadCOLS; col++) {
      matrix[row][col] = fakes::keypadMatrix[row][col];
    }
  }
  return true;
}
#if (OMOTE_HARDWARE_REV >= 5)
void update_keyboardBrightness(void) {}
static uint8_t keyboardBrightness = 255;
uint8_t get_keyboardBrightness() { return keyboardBrightness; }
void set_keyboardBrightness(uint8_t aKeyboardBrightness) { keyboardBrightness = aKeyboardBrightness; }
void init_SD_card(void) {}
#endif

void init_infraredSender(void) {}
void sendIRcode(int protocol, std::list<std::string> commandPayloads, std::string additionalPayload) {
  fakes::irSends.push_back({protocol, commandPayloads, additionalPayload});
}

void start_infraredReceiver(void) {}
void shutdown_infraredReceiver(void) {}
void infraredReceiver_loop(void) {}
static bool irReceiverEnabled = false;
bool get_irReceiverEnabled() { return irReceiverEnabled; }
void set_irReceiverEnabled(bool aIrReceiverEnabled) { irReceiverEnabled = aIrReceiverEnabled; }

void update_backlightBrightness(void) {}
static uint8_t backlightBrightness = 255;
uint8_t get_backlightBrightness() { return backlightBrightness; }
void set_backlightBrightness(uint8_t aBacklightBrightness) { backlightBrightness = aBacklightBrightness; }

void init_lvgl_hardware() {}

#if (ENABLE_WIFI_AND_MQTT == 1)
void init_mqtt(void) {}
static bool wifiConnected = true;
bool getIsWifiConnected() { return wifiConnected; }
void mqtt_loop() {}
bool publishMQTTMessage(const char *topic, const char *payload) {
  fakes::mqttPublishes.push_back({std::string(topic), std::string(payload)});
  return true;
}
void wifi_shutdown() {}
#endif

void get_heapUsage(unsigned long *heapSize, unsigned long *freeHeap, unsigned long *maxAllocHeap,
                   unsigned long *minFreeHeap) {
  *heapSize = 320000;
  *freeHeap = 200000;
  *maxAllocHeap = 100000;
  *minFreeHeap = 180000;
}

// ============================================================================
// sceneHandler.h - recorded instead of executed
// ============================================================================
void setLabelActiveScene() {}
void handleScene(uint16_t command, commandData aCommandData, std::string additionalPayload) {
  fakes::sceneCalls.push_back({command, aCommandData.commandPayloads, additionalPayload});
}
void handleGUI(uint16_t command, commandData aCommandData, std::string additionalPayload) {
  fakes::guiCalls.push_back({command, aCommandData.commandPayloads, additionalPayload});
}

// ============================================================================
// gui layer
// ============================================================================
std::string gui_memoryOptimizer_getActiveSceneName() { return fakes::getActiveSceneName(); }
void gui_memoryOptimizer_setActiveSceneName(std::string aSceneName) { fakes::setActiveSceneName(aSceneName); }
std::string gui_memoryOptimizer_getActiveGUIname() { return fakes::getActiveGUIname(); }
void gui_memoryOptimizer_setActiveGUIname(std::string aGUIname) { fakes::setActiveGUIname(aGUIname); }
GUIlists gui_memoryOptimizer_getActiveGUIlist() { return (GUIlists)fakes::getActiveGUIlist(); }
void gui_memoryOptimizer_setActiveGUIlist(GUIlists aGUIlist) { fakes::setActiveGUIlist((int)aGUIlist); }

void showNewIRmessage(std::string message) { fakes::shownIRMessages.push_back(message); }
#if (ENABLE_WIFI_AND_MQTT == 1)
void showMQTTmessage(std::string topic, std::string payload) {
  fakes::shownIRMessages.push_back(topic + "=" + payload);
}
void showWiFiConnected(bool connected) { (void)connected; }
#endif

// ============================================================================
// scene__default.cpp is not compiled in env:native_test (it drags in all
// example devices and scenes). The data it owns lives here instead, so tests
// can define their own default key maps.
// ============================================================================
void register_scene_defaultKeys(void) {}

std::map<char, repeatModes> key_repeatModes_default;
std::map<char, uint16_t> key_commands_short_default;
std::map<char, uint16_t> key_commands_long_default;
t_gui_list main_gui_list;
