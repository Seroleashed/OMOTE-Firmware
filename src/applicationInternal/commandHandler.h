#pragma once

#include <string>
#include <list>
#include <map>

#include "devices/keyboard/device_keyboard_mqtt/device_keyboard_mqtt.h"
#include "devices/keyboard/device_keyboard_ble/device_keyboard_ble.h"

extern uint16_t COMMAND_UNKNOWN;

/*
  Depending on which keyboard is enabled (BLE or MQTT), we define KEYBOARD_UP, KEYBOARD_DOWN and so on.
  These defines are used in keys.cpp, gui*.cpp and commandHandler.cpp
  Example:
  If BLE  is enabled, then KEYBOARD_UP will be the same as KEYBOARD_BLE_UP
  If MQTT is enabled, then KEYBOARD_UP will be the same as KEYBOARD_MQTT_UP
  If none of them is enabled, then KEYBOARD_UP will be the same as KEYBOARD_UP_DUMMY
  Doing so you can switch between the keyboards without changing the UI code (keys.cpp, gui*.cpp and commandHandler.cpp)
  If you need something different than this behaviour, then you can change the code in 'register_keyboardCommands()'
  or you can of course change keys.cpp, gui*.cpp and commandHandler.cpp so that they directly use KEYBOARD_BLE_UP or KEYBOARD_MQTT_UP etc.
*/

extern uint16_t KEYBOARD_DUMMY_UP;
extern uint16_t KEYBOARD_DUMMY_DOWN;
extern uint16_t KEYBOARD_DUMMY_RIGHT;
extern uint16_t KEYBOARD_DUMMY_LEFT;
extern uint16_t KEYBOARD_DUMMY_SELECT;
extern uint16_t KEYBOARD_DUMMY_SENDSTRING;
extern uint16_t KEYBOARD_DUMMY_BACK;
extern uint16_t KEYBOARD_DUMMY_HOME;
extern uint16_t KEYBOARD_DUMMY_MENU;
extern uint16_t KEYBOARD_DUMMY_SCAN_PREVIOUS_TRACK;
extern uint16_t KEYBOARD_DUMMY_REWIND_LONG;
extern uint16_t KEYBOARD_DUMMY_REWIND;
extern uint16_t KEYBOARD_DUMMY_PLAYPAUSE;
extern uint16_t KEYBOARD_DUMMY_FASTFORWARD;
extern uint16_t KEYBOARD_DUMMY_FASTFORWARD_LONG;
extern uint16_t KEYBOARD_DUMMY_SCAN_NEXT_TRACK;
extern uint16_t KEYBOARD_DUMMY_MUTE;
extern uint16_t KEYBOARD_DUMMY_VOLUME_INCREMENT;
extern uint16_t KEYBOARD_DUMMY_VOLUME_DECREMENT;

#if (ENABLE_KEYBOARD_BLE == 1)
  #define KEYBOARD_PREFIX KEYBOARD_BLE_
#elif (ENABLE_KEYBOARD_MQTT == 1)
  #define KEYBOARD_PREFIX KEYBOARD_MQTT_
#else
  // Of course keyboard commands will not work if neither BLE nor MQTT keyboard is enabled, but at least code will compile.
  // But you have to change keys.cpp, gui_numpad.cpp and commandHandler.cpp where keyboard commands are used so that a command can be executed successfully.
  // Search for "executeCommand(Key" to find them.
  #define KEYBOARD_PREFIX KEYBOARD_DUMMY_
#endif

extern uint16_t KEYBOARD_UP;
extern uint16_t KEYBOARD_DOWN;
extern uint16_t KEYBOARD_RIGHT;
extern uint16_t KEYBOARD_LEFT;
extern uint16_t KEYBOARD_SELECT;
extern uint16_t KEYBOARD_SENDSTRING;
extern uint16_t KEYBOARD_BACK;
extern uint16_t KEYBOARD_HOME;
extern uint16_t KEYBOARD_MENU;
extern uint16_t KEYBOARD_SCAN_PREVIOUS_TRACK;
extern uint16_t KEYBOARD_REWIND_LONG;
extern uint16_t KEYBOARD_REWIND;
extern uint16_t KEYBOARD_PLAYPAUSE;
extern uint16_t KEYBOARD_FASTFORWARD;
extern uint16_t KEYBOARD_FASTFORWARD_LONG;
extern uint16_t KEYBOARD_SCAN_NEXT_TRACK;
extern uint16_t KEYBOARD_MUTE;
extern uint16_t KEYBOARD_VOLUME_INCREMENT;
extern uint16_t KEYBOARD_VOLUME_DECREMENT;

// Not needed anymore, but maybe again in future
// /*
//  * Concatenate preprocessor tokens A and B without expanding macro definitions
//  * (however, if invoked from a macro, macro arguments are expanded).
//  */
// #define PPCAT_NX(A, B) A ## B
// /*
//  * Concatenate preprocessor tokens A and B after macro-expanding them.
//  */
// #define PPCAT(A, B) PPCAT_NX(A, B)
// 
// Test
// https://stackoverflow.com/questions/5256313/c-c-macro-string-concatenation
// #define STR(x) #x
// #define XSTR(x) STR(x)
// #pragma message "1 The value is: " XSTR(KEYBOARD_BLE_UP)
// #pragma message "2 The value is: " XSTR(KEYBOARD_MQTT_UP)
// #pragma message "3 The value is: " XSTR(KEYBOARD_UP)

enum commandHandlers {
  SPECIAL,
  SCENE,
  GUI,
  IR,
  #if (ENABLE_WIFI_AND_MQTT == 1)
  MQTT,
  #endif
  #if (ENABLE_KEYBOARD_BLE == 1)
  BLE_KEYBOARD,
  #endif
};

struct commandData {
  commandHandlers commandHandler;
  std::list<std::string> commandPayloads;
};

/*
  Registering a command.

  The numeric command id is handed out in registration order, so it changes as
  soon as a device is added, removed or moved. A stored configuration must
  therefore never refer to that number - it refers to the command *name*.

  register_command() is a macro that turns the variable name of the command
  into that stable name, so none of the existing call sites has to change:

      register_command(&SAMSUNG_POWER, makeCommandData(IR, {...}));
      -> name "SAMSUNG_POWER"

  Use register_command_withName() where the name has to differ from the
  variable, for example for commands created at runtime from a JSON file.
*/
void register_command_withName(uint16_t *command, commandData aCommandData, std::string commandName);
#define register_command(commandPtr, aCommandData) \
  register_command_withName(commandPtr, aCommandData, #commandPtr)

// only get a unique ID. used by KEYBOARD_DUMMY and COMMAND_UNKNOWN
void get_uniqueCommandID(uint16_t *command);

// --- lookup by name, used by the JSON configuration and the web UI ----------
// returns COMMAND_UNKNOWN if the name is not registered
uint16_t get_commandID_byName(const std::string &commandName);
// returns an empty string if the id is not registered
std::string get_commandName_byID(uint16_t command);
bool get_commandData_byID(uint16_t command, commandData &aCommandData);
// every registered command, ordered by id. Used to export the configuration.
const std::map<uint16_t, commandData> &get_all_commands();
const std::map<uint16_t, std::string> &get_all_commandNames();

void register_keyboardCommands();
commandData makeCommandData(commandHandlers a, std::list<std::string> b);
void executeCommand(uint16_t command, std::string additionalPayload = "");

void receiveNewIRmessage_cb(std::string message);
#if (ENABLE_KEYBOARD_BLE == 1)
// used as callback from hardware
void receiveBLEmessage_cb(std::string message);
#endif
#if (ENABLE_WIFI_AND_MQTT == 1)
// used as callbacks from hardware
void receiveWiFiConnected_cb(bool connected);
void receiveMQTTmessage_cb(std::string topic, std::string payload);
#endif
