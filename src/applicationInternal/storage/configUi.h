#pragma once

#include <stdint.h>

#include <string>
#include <vector>

/*
  /cfg/ui.json - screens described as data.

  Held back from step 6 on purpose: the contents of this file are the widget set
  of the renderer, and inventing it before there was a renderer would have meant
  building it twice.

  ## The widget set is deliberately small

  Seven kinds, no more. Every one of them has to be rendered, previewed in a
  browser (step 21) and explained to somebody in an editor - and a format that
  can express anything ends up being a programming language with none of the
  tools. What is not here on purpose: nesting beyond one grid, per-widget
  colours and fonts, anything conditional. Step 24 covers the last of those with
  macros, where conditions belong.

      {
        "schemaVersion": 1,
        "type": "omote.ui",
        "screens": [{
          "name": "Numpad",
          "grid": { "columns": 3, "rowHeight": 52 },
          "widgets": [
            { "type": "button", "row": 0, "column": 0, "label": "1",
              "command": "SAMSUNG_NUM_1" },
            { "type": "slider", "row": 4, "column": 0, "columnSpan": 3,
              "command": "YAMAHA_VOLUME", "min": 0, "max": 100 }
          ]
        }]
      }

  ## Every widget refers to a command by name

  The same rule as everywhere else since step 5. A screen that names a command
  this device does not have loses that one widget, not the screen - a file moved
  between two remotes is the normal case, not an error.
*/

namespace configModel {

const uint16_t UI_SCHEMA_VERSION = 1;

enum class WidgetType {
  Button,    // label or icon, sends a command
  Label,     // static text
  Slider,    // sends its value as the payload
  Arc,       // same, round
  List,      // rows of label + command
  StatusBar, // battery, wifi, active scene
  Spacer,    // an empty cell, so a layout can leave a gap
};

struct ListItem {
  std::string label;
  std::string command;
  std::string payload;
};

struct Widget {
  WidgetType type = WidgetType::Button;

  // position in the screen's grid
  uint8_t row = 0;
  uint8_t column = 0;
  uint8_t rowSpan = 1;
  uint8_t columnSpan = 1;

  std::string label;
  std::string icon;    // an LVGL symbol name, e.g. "LV_SYMBOL_POWER"
  std::string command; // what pressing it sends
  std::string payload; // fixed payload; a slider sends its value instead

  // slider and arc
  int16_t minimum = 0;
  int16_t maximum = 100;

  std::vector<ListItem> items; // list only
};

struct Screen {
  std::string name;
  uint8_t columns = 3;
  uint16_t rowHeight = 52; // pixels, as in the hand written numpad
  std::vector<Widget> widgets;
};

struct UiConfig {
  std::vector<Screen> screens;
};

// enum <-> string, so the file does not depend on the order of the enum
std::string widgetTypeToString(WidgetType type);
bool widgetTypeFromString(const std::string &text, WidgetType &type);

std::string serializeUi(const UiConfig &config);
/*
  Rejects what cannot be drawn: an unknown widget type, a button with no
  command and no label, a position outside the grid, a screen without a name.
  Everything it rejects, it names.
*/
bool parseUi(const std::string &json, UiConfig &config, std::string &error);

} // namespace configModel
