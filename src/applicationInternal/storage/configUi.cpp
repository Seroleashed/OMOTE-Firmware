#include "applicationInternal/storage/configUi.h"

#include <ArduinoJson.h>

#include "applicationInternal/omote_log.h"
#include "applicationInternal/storage/configFile.h"

namespace configModel {

// --- widget types ------------------------------------------------------------

std::string widgetTypeToString(WidgetType type) {
  switch (type) {
    case WidgetType::Button: return "button";
    case WidgetType::Label: return "label";
    case WidgetType::Slider: return "slider";
    case WidgetType::Arc: return "arc";
    case WidgetType::List: return "list";
    case WidgetType::StatusBar: return "statusBar";
    case WidgetType::Spacer: return "spacer";
  }
  return "";
}

bool widgetTypeFromString(const std::string &text, WidgetType &type) {
  if (text == "button") { type = WidgetType::Button; return true; }
  if (text == "label") { type = WidgetType::Label; return true; }
  if (text == "slider") { type = WidgetType::Slider; return true; }
  if (text == "arc") { type = WidgetType::Arc; return true; }
  if (text == "list") { type = WidgetType::List; return true; }
  if (text == "statusBar") { type = WidgetType::StatusBar; return true; }
  if (text == "spacer") { type = WidgetType::Spacer; return true; }
  return false;
}

// --- serialize ---------------------------------------------------------------

std::string serializeUi(const UiConfig &config) {
  JsonDocument doc;
  configFile::writeEnvelope(doc, configFile::TYPE_UI, UI_SCHEMA_VERSION);

  JsonArray screens = doc["screens"].to<JsonArray>();
  for (size_t s = 0; s < config.screens.size(); s++) {
    const Screen &screen = config.screens[s];
    JsonObject entry = screens.add<JsonObject>();
    entry["name"] = screen.name;

    JsonObject grid = entry["grid"].to<JsonObject>();
    grid["columns"] = screen.columns;
    grid["rowHeight"] = screen.rowHeight;

    JsonArray widgets = entry["widgets"].to<JsonArray>();
    for (size_t w = 0; w < screen.widgets.size(); w++) {
      const Widget &widget = screen.widgets[w];
      JsonObject item = widgets.add<JsonObject>();
      item["type"] = widgetTypeToString(widget.type);
      item["row"] = widget.row;
      item["column"] = widget.column;
      // spans of 1 are the default and would be noise on every single widget
      if (widget.rowSpan != 1) item["rowSpan"] = widget.rowSpan;
      if (widget.columnSpan != 1) item["columnSpan"] = widget.columnSpan;

      if (!widget.label.empty()) item["label"] = widget.label;
      if (!widget.icon.empty()) item["icon"] = widget.icon;
      if (!widget.command.empty()) item["command"] = widget.command;
      if (!widget.payload.empty()) item["payload"] = widget.payload;

      if (widget.type == WidgetType::Slider || widget.type == WidgetType::Arc) {
        item["min"] = widget.minimum;
        item["max"] = widget.maximum;
      }

      if (!widget.items.empty()) {
        JsonArray items = item["items"].to<JsonArray>();
        for (size_t i = 0; i < widget.items.size(); i++) {
          JsonObject listItem = items.add<JsonObject>();
          listItem["label"] = widget.items[i].label;
          listItem["command"] = widget.items[i].command;
          if (!widget.items[i].payload.empty()) listItem["payload"] = widget.items[i].payload;
        }
      }
    }
  }

  std::string result;
  serializeJsonPretty(doc, result);
  return result;
}

// --- parse -------------------------------------------------------------------

static bool parseWidget(JsonObjectConst source, const Screen &screen, Widget &widget,
                        std::string &error) {
  std::string typeText = source["type"].is<const char *>() ? source["type"].as<std::string>() : "";
  if (!widgetTypeFromString(typeText, widget.type)) {
    error = "screen '" + screen.name + "': unknown widget type '" + typeText + "'";
    return false;
  }

  if (source["row"].is<uint8_t>()) widget.row = source["row"].as<uint8_t>();
  if (source["column"].is<uint8_t>()) widget.column = source["column"].as<uint8_t>();
  if (source["rowSpan"].is<uint8_t>()) widget.rowSpan = source["rowSpan"].as<uint8_t>();
  if (source["columnSpan"].is<uint8_t>()) widget.columnSpan = source["columnSpan"].as<uint8_t>();

  if (widget.rowSpan == 0 || widget.columnSpan == 0) {
    error = "screen '" + screen.name + "': a span of 0 draws nothing";
    return false;
  }
  /*
    A widget reaching past the right edge is a layout mistake that renders as
    something subtly wrong rather than obviously broken - LVGL puts it
    somewhere, and the author spends a while wondering why. Cheaper to refuse.
  */
  if (widget.column + widget.columnSpan > screen.columns) {
    error = "screen '" + screen.name + "': a widget at column " + std::to_string(widget.column) +
            " spanning " + std::to_string(widget.columnSpan) + " does not fit in " +
            std::to_string(screen.columns) + " columns";
    return false;
  }

  if (source["label"].is<const char *>()) widget.label = source["label"].as<std::string>();
  if (source["icon"].is<const char *>()) widget.icon = source["icon"].as<std::string>();
  if (source["command"].is<const char *>()) widget.command = source["command"].as<std::string>();
  if (source["payload"].is<const char *>()) widget.payload = source["payload"].as<std::string>();
  if (source["min"].is<int16_t>()) widget.minimum = source["min"].as<int16_t>();
  if (source["max"].is<int16_t>()) widget.maximum = source["max"].as<int16_t>();

  JsonArrayConst items = source["items"];
  if (!items.isNull()) {
    for (JsonObjectConst entry : items) {
      ListItem listItem;
      listItem.label = entry["label"].is<const char *>() ? entry["label"].as<std::string>() : "";
      listItem.command = entry["command"].is<const char *>() ? entry["command"].as<std::string>() : "";
      listItem.payload = entry["payload"].is<const char *>() ? entry["payload"].as<std::string>() : "";
      if (listItem.label.empty() && listItem.command.empty()) continue; // nothing to draw
      widget.items.push_back(listItem);
    }
  }

  // --- what each kind needs to be drawable at all ---------------------------
  switch (widget.type) {
    case WidgetType::Button:
      // A button with neither a caption nor an icon is an invisible thing the
      // user can press. Worth refusing rather than rendering.
      if (widget.label.empty() && widget.icon.empty()) {
        error = "screen '" + screen.name + "': a button needs a label or an icon";
        return false;
      }
      break;
    case WidgetType::Label:
      if (widget.label.empty()) {
        error = "screen '" + screen.name + "': a label needs text";
        return false;
      }
      break;
    case WidgetType::Slider:
    case WidgetType::Arc:
      if (widget.minimum >= widget.maximum) {
        error = "screen '" + screen.name + "': min must be below max";
        return false;
      }
      if (widget.command.empty()) {
        error = "screen '" + screen.name + "': a " + widgetTypeToString(widget.type) +
                " needs a command to send its value to";
        return false;
      }
      break;
    case WidgetType::List:
      if (widget.items.empty()) {
        error = "screen '" + screen.name + "': a list needs items";
        return false;
      }
      break;
    case WidgetType::StatusBar:
    case WidgetType::Spacer:
      break; // neither needs anything
  }

  return true;
}

bool parseUi(const std::string &json, UiConfig &config, std::string &error) {
  config = UiConfig();

  JsonDocument doc;
  uint16_t version = 0;
  if (!configFile::parseAndCheckEnvelope(json, configFile::TYPE_UI, UI_SCHEMA_VERSION, doc, version,
                                         error)) {
    return false;
  }

  JsonArrayConst screens = doc["screens"];
  if (screens.isNull()) {
    error = "screens array is missing";
    return false;
  }

  for (JsonObjectConst entry : screens) {
    Screen screen;

    if (!entry["name"].is<const char *>() || std::string(entry["name"].as<const char *>()).empty()) {
      error = "a screen without a name";
      return false;
    }
    screen.name = entry["name"].as<std::string>();

    // Two screens of the same name would make the gui registry depend on
    // insertion order - a bug that only shows up on the device.
    for (size_t i = 0; i < config.screens.size(); i++) {
      if (config.screens[i].name == screen.name) {
        error = "screen '" + screen.name + "' appears twice";
        return false;
      }
    }

    JsonVariantConst grid = entry["grid"];
    if (grid["columns"].is<uint8_t>()) screen.columns = grid["columns"].as<uint8_t>();
    if (grid["rowHeight"].is<uint16_t>()) screen.rowHeight = grid["rowHeight"].as<uint16_t>();
    if (screen.columns == 0) {
      error = "screen '" + screen.name + "': a grid needs at least one column";
      return false;
    }

    JsonArrayConst widgets = entry["widgets"];
    if (!widgets.isNull()) {
      for (JsonObjectConst source : widgets) {
        Widget widget;
        if (!parseWidget(source, screen, widget, error)) return false;
        screen.widgets.push_back(widget);
      }
    }

    config.screens.push_back(screen);
  }

  omote_log_i("configUi: read %u screen(s)\r\n", (unsigned)config.screens.size());
  return true;
}

} // namespace configModel
