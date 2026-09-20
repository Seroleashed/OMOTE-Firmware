#include <map>
#include <string>
#include <list>
#include <map>
#include <lvgl.h>
#include "guiRegistry.h"
#include "applicationInternal/gui/guiBase.h"
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/scenes/sceneRegistry.h"
#include "applicationInternal/omote_log.h"
#include "scenes/scene__default.h"

// ------------------------------------------------------------------------------------
// this is a map of the registered_guis that can be accessed by name
std::map<std::string, gui_definition> registered_guis_byName_map;

// ------------------------------------------------------------------------------------


void register_gui(
  std::string a_name,
  create_tab_content a_create_tab_content,
  notify_tab_before_delete a_notify_tab_before_delete,
  gui_setKeys a_gui_setKeys,
  key_repeatModes a_key_repeatModes,
  key_commands_short a_key_commands_short,
  key_commands_long a_key_commands_long,
  create_tab_content_named a_create_tab_content_named
  ) {

  /*
    A name registered twice used to be refused outright, because for two screens
    written in C++ it can only be a mistake. Since step 16 it is also how a
    screen from ui.json replaces the one compiled into the firmware - the same
    rule devices and scenes already follow. It is still worth a warning: for the
    C++ case it remains a mistake.
  */
  bool replacing = registered_guis_byName_map.count(a_name) > 0;
  if (replacing) {
    omote_log_w("gui: '%s' is registered again, the newer definition replaces the older one\r\n",
                a_name.c_str());
  }

  gui_definition new_gui_definition = gui_definition{
    a_name,
    a_create_tab_content,
    a_notify_tab_before_delete,
    a_gui_setKeys,
    a_key_repeatModes,
    a_key_commands_short,
    a_key_commands_long,
    a_create_tab_content_named
  };

  // put the gui_definition in a map that can be accessed by name
  registered_guis_byName_map[a_name] = new_gui_definition;

  // By default, put all registered guis in the sequence of guis to be shown of the default scene
  // Can be overwritten by scenes to have their own gui_list.
  // A replacement keeps the position it already had - appending again would
  // show the same screen twice when paging through.
  if (!replacing) {
    main_gui_list.insert(main_gui_list.end(), {std::string(a_name)});
  }

  setKeysForAllRegisteredGUIsAndScenes();

}

void setKeysForAllRegisteredGUIsAndScenes() {
  // Whenever a new gui or scene is registered, a new gui or scene command could have been defined in the gui or scene.
  // But this new command could have already been used before in the key definition of another gui or scene. The command at this time was 0, which is undefined.
  // So we have to set the keys again for all guis and scenes that have been registered before.
  // 1. set again the defaultKeys
  register_scene_defaultKeys();
  // 2. loop over all registered scenes and call setKeys()
  for (std::map<std::string, scene_definition>::iterator it = registered_scenes.begin(); it != registered_scenes.end(); ++it) {
    // A scene may be registered without a setKeys function - a scene that only
    // runs a start sequence needs no key map at all. The gui loop below has
    // always checked for that; this one crashed instead.
    if (it->second.this_scene_setKeys != NULL) {
      it->second.this_scene_setKeys();
    }
  }
  // 3. loop over all registered guis and call setKeys()
  for (std::map<std::string, gui_definition>::iterator it = registered_guis_byName_map.begin(); it != registered_guis_byName_map.end(); ++it) {
    if (it->second.this_gui_setKeys != NULL) {
      it->second.this_gui_setKeys();
    }
  }

}
