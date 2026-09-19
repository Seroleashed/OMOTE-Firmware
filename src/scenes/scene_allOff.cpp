#include <map>
#include "scenes/scene_allOff.h"
#include "applicationInternal/keys.h"
#include "applicationInternal/scenes/sceneRegistry.h"
#include "applicationInternal/scenes/sequenceEngine.h"
#include "applicationInternal/hardware/hardwarePresenter.h"
// devices
#include "devices/TV/device_samsungTV/device_samsungTV.h"
#include "devices/AVreceiver/device_yamahaAmp/device_yamahaAmp.h"
#include "applicationInternal/commandHandler.h"

uint16_t SCENE_ALLOFF      ; //"Scene_allOff"
uint16_t SCENE_ALLOFF_FORCE; //"Scene_allOff_force"

std::map<char, repeatModes> key_repeatModes_allOff;
std::map<char, uint16_t> key_commands_short_allOff;
std::map<char, uint16_t> key_commands_long_allOff;

void scene_setKeys_allOff() {
  key_repeatModes_allOff = {
  
  
  
  
  
  
  
  
  
  
  };
  
  key_commands_short_allOff = {
  
  
  
  
  
  
  
  
  
  
  };
  
  key_commands_long_allOff = {
  
  
  };

}

void scene_start_sequence_allOff(void) {
  // 3.5 seconds of switching things off. This used to hold the main loop for
  // the whole time - the display frozen, key presses unseen. Now it runs over
  // the loop, step by step.
  sequenceEngine::enqueue({
    {SAMSUNG_POWER_OFF, "", 500},
    {YAMAHA_POWER_OFF,  "", 500},
    // repeat IR to be sure
    {SAMSUNG_POWER_OFF, "", 500},
    {YAMAHA_POWER_OFF,  "", 500},
    // repeat IR to be sure
    {SAMSUNG_POWER_OFF, "", 500},
    {YAMAHA_POWER_OFF,  "", 500},
    // you cannot power off FireTV, but at least you can stop the currently running app
    {KEYBOARD_HOME,     "", 500},
    {KEYBOARD_HOME,     "",   0},
  });
}

void scene_end_sequence_allOff(void) {

}

std::string scene_name_allOff = "Off";

void register_scene_allOff(void) {
  register_command(&SCENE_ALLOFF      , makeCommandData(SCENE, {scene_name_allOff}));
  register_command(&SCENE_ALLOFF_FORCE, makeCommandData(SCENE, {scene_name_allOff, "FORCE"}));

  register_scene(
    scene_name_allOff,
    & scene_setKeys_allOff,
    & scene_start_sequence_allOff,
    & scene_end_sequence_allOff,
    & key_repeatModes_allOff,
    & key_commands_short_allOff,
    & key_commands_long_allOff,
    NULL,
    SCENE_ALLOFF);
}
