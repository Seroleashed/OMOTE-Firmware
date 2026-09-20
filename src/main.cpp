// OMOTE firmware for ESP32
// 2023-2025 Maximilian Kern, Klaus Musch

#include <string>
#include <vector>

#include "applicationInternal/omote_log.h"
// init hardware and hardware loop
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/hardware/firmwareImage.h"
#include "applicationInternal/firmwareInfo.h"
#include "applicationInternal/bootGuard.h"
#include "applicationInternal/credentials.h"
#include "applicationInternal/storage/configStorage.h"
#include "applicationInternal/storage/configLoader.h"
#include "applicationInternal/storage/configScenes.h"
#include "applicationInternal/transport/configTransport.h"
#include "applicationInternal/transport/transportSession.h"
// register devices and their commands
//   special
#include "devices/misc/device_specialCommands.h"
#include "applicationInternal/commandHandler.h"
//   keyboards
#if (ENABLE_KEYBOARD_MQTT == 1)
#include "devices/keyboard/device_keyboard_mqtt/device_keyboard_mqtt.h"
#endif // ENABLE_KEYBOARD_MQTT
#if (ENABLE_KEYBOARD_BLE == 1)
#include "devices/keyboard/device_keyboard_ble/device_keyboard_ble.h"
#endif // ENABLE_KEYBOARD_BLE
//   TV
#include "devices/TV/device_samsungTV/device_samsungTV.h"
//#include "devices/TV/device_lgTV/device_lgTV.h"
//#include "devices/TV/device_sonyTV/device_sonyTV.h.h"
//   AV receiver
#include "devices/AVreceiver/device_yamahaAmp/device_yamahaAmp.h"
//#include "devices/AVreceiver/device_boseAmp/device_boseAmp.h"
//#include "devices/AVreceiver/device_denonAvr/device_denonAvr.h"
//#include "devices/AVreceiver/device_lgsoundbar/device_lgsoundbar.h"
//   media player
#include "devices/mediaPlayer/device_appleTV/device_appleTV.h"
//#include "devices/mediaPlayer/device_lgbluray/device_lgbluray.h"
//#include "devices/mediaPlayer/device_samsungbluray/device_samsungbluray.h"
//#include "devices/mediaPlayer/device_shield/device_shield.h"
//   misc
#include "devices/misc/device_smarthome/device_smarthome.h"
//#include "devices/misc/device_airconditioner/device_airconditioner.h"
// register gui and keys
#include "applicationInternal/gui/guiBase.h"
#include "applicationInternal/gui/guiRegistry.h"
#include "guis/gui_sceneSelection.h"
#include "guis/gui_irReceiver.h"
#include "guis/gui_settings.h"
#include "guis/gui_numpad.h"
#include "guis/gui_BLEpairing.h"
#include "devices/AVreceiver/device_yamahaAmp/gui_yamahaAmp.h"
#include "devices/mediaPlayer/device_appleTV/gui_appleTV.h"
#include "devices/misc/device_smarthome/gui_smarthome.h"
//#include "devices/misc/device_airconditioner/gui_airconditioner.h"
#include "applicationInternal/keys.h"
#include "applicationInternal/gui/guiStatusUpdate.h"
// register scenes
#include "scenes/scene__default.h"
#include "scenes/scene_allOff.h"
#include "scenes/scene_TV.h"
#include "scenes/scene_fireTV.h"
#include "scenes/scene_chromecast.h"
#include "scenes/scene_appleTV.h"
#include "applicationInternal/scenes/sceneHandler.h"
#include "applicationInternal/scenes/sequenceEngine.h"

/*
  What the configuration transport is allowed to ask of the firmware.

  APPLY deliberately does not re-register anything on the spot: commands that
  are already registered would be replaced, but a gui or a scene holding an id
  would keep pointing at the old one. Restarting is the honest answer, and it
  takes under a second.
*/
static void applyConfigurationFromTransport() {
  omote_log_i("transport: APPLY - restart the device to load the new configuration\r\n");
}

static void rebootFromTransport() {
  #if defined(ARDUINO)
  ESP.restart();
  #else
  omote_log_i("transport: REBOOT (the simulator stays where it is)\r\n");
  #endif
}

static std::vector<std::string> transportInfoLines() {
  std::vector<std::string> lines;
  lines.push_back("version " + firmwareInfo::version());
  lines.push_back("built " + firmwareInfo::buildDate());
  lines.push_back("hardwareRev " + std::to_string(configModel::thisHardwareRevision()));
  lines.push_back("config " + bootGuard::statusText());
  lines.push_back("devicesLoaded " + std::to_string(configLoader::lastReport().devicesLoaded));
  lines.push_back("filesFailed " + std::to_string(configLoader::lastReport().filesFailed));
  return lines;
}

#if defined(ARDUINO)
// in case of Arduino we have a setup() and a loop()
void setup() {

#elif defined(WIN32) || defined(__linux__) || defined(__APPLE__)
// in case of Windows/Linux, we have only a main() function, no setup() and loop(), so we have to simulate them
// forward declaration of loop()
void loop(unsigned long *pIMUTaskTimer, unsigned long *pUpdateStatusTimer);
// main function as usual in C
int main(int argc, char *argv[]) {
#endif

  // --- Startup ---
  Serial.begin(115200);
  // which image is running? After an OTA this says "pending verify" until the
  // end of setup() confirms it
  {
    FirmwareImageInfo firmwareImage = get_firmwareImageInfo();
    omote_log_i("OMOTE %s, running from '%s' (%s)%s\r\n",
                firmwareInfo::versionLine().c_str(),
                firmwareImage.runningPartition.c_str(),
                firmwareBootStateToString(firmwareImage.state).c_str(),
                firmwareImage.otaCapable ? ", OTA capable" : "");
  }
  // Count this boot attempt. Has to happen before anything reads a stored
  // configuration, because that is what it protects against.
  bootGuard::begin(get_bootCounterStorage());
  // WiFi and MQTT logins live in NVS, never in the configuration files - those
  // get exported and shared. secrets.h stays the compile time default.
  credentials::begin(get_credentialStorage());
  // do some general hardware setup, like powering the TFT, I2C, ...
  init_hardware_general();
  // get wakeup reason
  init_sleep();
  // Restore settings from internal flash memory
  init_preferences();

  // File system for the JSON configuration (littlefs on the ESP32, a local
  // folder in the simulator). Has to be set before the devices are registered:
  // configLoader reads /cfg/devices/ further down.
  configStorage::setFileSystem(get_configFileSystem());
  // blinking led
  init_userled();
  // startup SD card
  #if(OMOTE_HARDWARE_REV >= 5)
  // SD card is currently not used, so save some time on startup and don't init the SD card
  // init_SD_card();
  #endif

  // setup IR sender
  init_infraredSender();

  // register commands for the devices
  register_specialCommands();
  //   TV
  register_device_samsungTV();
  //register_device_lgTV();
  //register_device_sonyTV();
  //   AV receiver
  register_device_yamahaAmp();
  //register_device_boseAmp();
  //register_device_denonAvr();
  //register_device_lgsoundbar();
  //   media player
  register_device_appleTV();
  //register_device_lgbluray();
  //register_device_samsungbluray();
  //register_device_shield();
  //   misc
  register_device_smarthome();
  //register_device_airconditioner();

  #if (ENABLE_KEYBOARD_MQTT == 1)
  register_device_keyboard_mqtt();
  #endif
  #if (ENABLE_KEYBOARD_BLE == 1)
  register_device_keyboard_ble();
  #endif
  register_keyboardCommands();

  #if (ENABLE_JSON_CONFIG == 1)
  /*
    Devices from /cfg/devices/*.json, on top of everything registered above.
    Deliberately last: what is compiled in is the fallback and gets registered
    first, a stored file wins over it by name. A broken file is skipped, the
    rest still loads, and configLoader::lastReport() keeps the reason.

    From here on a new IR device needs no compiler.
  */
  configLoader::loadDevices();
  /*
    And the settings from /cfg/system.json. After init_preferences(), so the
    file is applied on top of what the user set on the device - and after the
    devices, because a broken device file must not stop the display brightness
    from being right.
  */
  configLoader::loadSystem();
  /*
    Scenes and the keypad layout. After the devices on purpose: a scene refers
    to commands by name, so they have to exist before it can be registered.
  */
  configLoader::loadScenes();
  configLoader::loadKeys();
  #endif

  // Register the GUIs. They will be displayed in the order they have been registered.
  register_gui_sceneSelection();
  register_gui_irReceiver();
  register_gui_settings();
  register_gui_appleTV();
  register_gui_numpad();
  #if (ENABLE_KEYBOARD_BLE == 1)
  register_gui_blepairing();
  #endif
  register_gui_smarthome();
  //register_gui_airconditioner();
  register_gui_yamahaAmp();
  // Only show these GUIs in the main gui list. If you don't set this explicitely, by default all registered guis are shown.
  #if (USE_SCENE_SPECIFIC_GUI_LIST != 0)
  main_gui_list =
    {tabName_yamahaAmp, tabName_sceneSelection, tabName_smarthome, tabName_settings, tabName_irReceiver
    #if (ENABLE_KEYBOARD_BLE == 1)
    , tabName_blepairing
    #endif
    };
  #endif

  // register the scenes and their key_commands_*
  register_scene_defaultKeys();
  register_scene_TV();
  register_scene_fireTV();
  register_scene_chromecast();
  register_scene_appleTV();
  register_scene_allOff();
  // Only show these scenes on the sceneSelection gui. If you don't set this explicitely, by default all registered scenes are shown.
  set_scenes_on_sceneSelectionGUI({scene_name_TV, scene_name_fireTV, scene_name_chromecast, scene_name_appleTV});

  // init GUI - will initialize tft, touch and lvgl
  init_gui(); // This has to come before any other i2c devices are initialized, otherwise the i2c bus will not be powered
  setLabelActiveScene();
  gui_loop(); // Run the LVGL UI once before the loop takes over
  
  // Power Pin and battery monitor definition
  init_battery();

  // init BLE keyboard. Has to be after init_gui (because of powered I2C) and after init_battery (because of fuel gauge init)
  #if (ENABLE_KEYBOARD_BLE == 1)
  init_keyboardBLE();
  #endif

  // setup keyboard matrix driver
  init_keys();

  // setup the Inertial Measurement Unit (IMU) for motion detection. Has to be after init_gui(), otherwise I2C will not work
  init_IMU();

  // init WiFi - needs to be after init_gui() because WifiLabel must be available
  #if (ENABLE_WIFI_AND_MQTT == 1)
  init_mqtt();
  #endif

  // A host can now move configuration files on and off the device. Set up last,
  // so a session cannot start while half the firmware is still coming up - and
  // so APPLY has something to re-read.
  {
    configTransport::Callbacks transportCallbacks;
    transportCallbacks.apply = &applyConfigurationFromTransport;
    transportCallbacks.reboot = &rebootFromTransport;
    transportCallbacks.info = &transportInfoLines;
    configTransport::begin(get_configFileSystem(), transportCallbacks);
    transportSession::begin(get_transportByteStream());
    #if (ENABLE_BLE_CONFIG == 1)
    // a second way in, next to the cable. Whichever sends the magic line first
    // owns the session until it closes.
    init_bleTransport_HAL();
    transportSession::addStream(get_bleTransportByteStream());
    #endif
  }

  omote_log_i("Setup finished in %lu ms.\r\n", millis());

  // Everything came up: hardware, storage, gui, keypad and WiFi. That is the
  // self test an update has to pass. Only now the image is confirmed - if we
  // never get here, the bootloader starts the previous one again.
  confirm_firmwareIsWorking();
  // Same idea one level up: this boot reached the end, so the configuration it
  // used cannot be the one that keeps the device from starting.
  bootGuard::markBootSuccessful();

  #if defined(WIN32) || defined(__linux__) || defined(__APPLE__)
  // In Windows/Linux there is no loop function that is automatically being called. So we have to do this on our own infinitely here in main()
  unsigned long IMUTaskTimer = 0;
  unsigned long updateStatusTimer = 0;
  while (1)
    loop(&IMUTaskTimer, &updateStatusTimer);
  #endif

}

// Loop ------------------------------------------------------------------------------------------------------------------------------------
#if defined(ARDUINO)
unsigned long IMUTaskTimer = 0;
unsigned long updateStatusTimer = 0;
unsigned long *pIMUTaskTimer = &IMUTaskTimer;
unsigned long *pUpdateStatusTimer = &updateStatusTimer;
void loop() {
#elif defined(WIN32) || defined(__linux__) || defined(__APPLE__)
void loop(unsigned long *pIMUTaskTimer, unsigned long *pUpdateStatusTimer) {
#endif

  // --- do as often as possible --------------------------------------------------------
  // update backlight and keyboard brightness. Fade in on startup, dim before going to sleep
  update_backlightBrightness();
  #if(OMOTE_HARDWARE_REV >= 5)
    update_keyboardBrightness();
  #endif
  // keypad handling: get key states from hardware and process them
  keypad_loop();
  // a host moving configuration files on or off the device, over the serial
  // port or - in the simulator - a socket on localhost
  transportSession::loop(millis());
  // step through a running scene sequence. Before the engine existed, the
  // scenes held this very loop with delay() while they switched devices on.
  sequenceEngine::loop(millis());
  // process IR receiver, if activated
  if (get_irReceiverEnabled()) {
    infraredReceiver_loop();
  }
  // update LVGL UI
  gui_loop();
  // call mqtt loop to receive mqtt messages, if you are subscribed to some topics
  #if (ENABLE_WIFI_AND_MQTT == 1)
  mqtt_loop();
  #endif

  // --- every 100 ms -------------------------------------------------------------------
  // Refresh IMU data (motion detection) every 100 ms
  // If no action (key, TFT or motion), then go to sleep
  if(millis() - *pIMUTaskTimer >= 100){
    *pIMUTaskTimer = millis();

    check_activity();

  }

  // --- every 1000 ms ------------------------------------------------------------------
  if(millis() - *pUpdateStatusTimer >= 1000) {
    *pUpdateStatusTimer = millis();

    // update user_led, battery, BLE, memoryUsage on GUI
    updateHardwareStatusAndShowOnGUI();
  }

}
