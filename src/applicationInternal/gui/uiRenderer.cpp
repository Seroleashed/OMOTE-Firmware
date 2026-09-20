#include "applicationInternal/gui/uiRenderer.h"

#include <string>
#include <vector>

#include "applicationInternal/commandHandler.h"
#include "applicationInternal/omote_log.h"

namespace uiRenderer {

namespace {

/*
  The grid descriptors LVGL wants have to outlive the call - it keeps the
  pointer, it does not copy. One set per rendered screen, kept here and reused
  when the next screen is drawn, because only one screen is ever being built at
  a time.
*/
std::vector<lv_coord_t> columnDescriptor;
std::vector<lv_coord_t> rowDescriptor;

// A command id in a pointer-sized field, the way gui_numpad already does it.
void *asUserData(uint16_t command) { return (void *)(intptr_t)command; }
uint16_t fromUserData(void *userData) { return (uint16_t)(intptr_t)userData; }

void onPressed(lv_event_t *event) {
  lv_obj_t *target = lv_event_get_target(event);
  uint16_t command = fromUserData(lv_obj_get_user_data(target));
  if (command == COMMAND_UNKNOWN) return;
  executeCommand(command);
}

// A slider and an arc send where they were moved to, not a fixed payload.
void onValueChanged(lv_event_t *event) {
  lv_obj_t *target = lv_event_get_target(event);
  uint16_t command = fromUserData(lv_obj_get_user_data(target));
  if (command == COMMAND_UNKNOWN) return;

  int32_t value = lv_obj_check_type(target, &lv_arc_class) ? lv_arc_get_value(target)
                                                           : lv_slider_get_value(target);
  executeCommand(command, std::to_string(value));
}

/*
  Resolves the command and says whether it exists.

  A widget whose command is unknown is drawn disabled rather than dropped. A
  screen that silently loses half its buttons is harder to work out than one
  showing greyed-out ones with the right captions.
*/
uint16_t bind(lv_obj_t *object, const std::string &commandName, bool &known) {
  known = true;
  if (commandName.empty()) {
    known = false;
    lv_obj_set_user_data(object, asUserData(COMMAND_UNKNOWN));
    return COMMAND_UNKNOWN;
  }

  uint16_t command = get_commandID_byName(commandName);
  if (command == COMMAND_UNKNOWN) {
    known = false;
    lv_obj_add_state(object, LV_STATE_DISABLED);
    omote_log_w("uiRenderer: command '%s' is not on this device, widget disabled\r\n",
                commandName.c_str());
  }
  lv_obj_set_user_data(object, asUserData(command));
  return command;
}

void placeInGrid(lv_obj_t *object, const configModel::Widget &widget) {
  lv_obj_set_grid_cell(object, LV_GRID_ALIGN_STRETCH, widget.column, widget.columnSpan,
                       LV_GRID_ALIGN_STRETCH, widget.row, widget.rowSpan);
}

lv_obj_t *renderButton(const configModel::Widget &widget, lv_obj_t *parent, bool &known) {
  lv_obj_t *button = lv_btn_create(parent);
  lv_obj_t *caption = lv_label_create(button);

  // The icon wins when both are given: an icon plus its own name written next
  // to it is what an editor produces by accident, not on purpose.
  lv_label_set_text(caption, widget.icon.empty() ? widget.label.c_str() : widget.icon.c_str());
  lv_obj_center(caption);

  bind(button, widget.command, known);
  lv_obj_add_event_cb(button, onPressed, LV_EVENT_CLICKED, NULL);
  return button;
}

lv_obj_t *renderLabel(const configModel::Widget &widget, lv_obj_t *parent) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, widget.label.c_str());
  return label;
}

lv_obj_t *renderSlider(const configModel::Widget &widget, lv_obj_t *parent, bool &known) {
  lv_obj_t *slider = lv_slider_create(parent);
  lv_slider_set_range(slider, widget.minimum, widget.maximum);
  bind(slider, widget.command, known);
  lv_obj_add_event_cb(slider, onValueChanged, LV_EVENT_VALUE_CHANGED, NULL);
  return slider;
}

lv_obj_t *renderArc(const configModel::Widget &widget, lv_obj_t *parent, bool &known) {
  lv_obj_t *arc = lv_arc_create(parent);
  lv_arc_set_range(arc, widget.minimum, widget.maximum);
  bind(arc, widget.command, known);
  lv_obj_add_event_cb(arc, onValueChanged, LV_EVENT_VALUE_CHANGED, NULL);
  return arc;
}

lv_obj_t *renderList(const configModel::Widget &widget, lv_obj_t *parent, uint16_t &disabled) {
  lv_obj_t *list = lv_list_create(parent);
  for (size_t i = 0; i < widget.items.size(); i++) {
    lv_obj_t *button = lv_list_add_btn(list, NULL, widget.items[i].label.c_str());
    bool known = true;
    bind(button, widget.items[i].command, known);
    if (!known) disabled++;
    lv_obj_add_event_cb(button, onPressed, LV_EVENT_CLICKED, NULL);
  }
  return list;
}

} // namespace

Result render(const configModel::Screen &screen, lv_obj_t *parent) {
  Result result;

  // How many rows the widgets actually reach into. Asking the layout for it
  // beats making the file state it - a row count that disagrees with the
  // widgets is a mistake waiting to happen.
  uint8_t rows = 1;
  for (size_t i = 0; i < screen.widgets.size(); i++) {
    uint8_t reach = (uint8_t)(screen.widgets[i].row + screen.widgets[i].rowSpan);
    if (reach > rows) rows = reach;
  }

  columnDescriptor.assign(screen.columns, LV_GRID_FR(1));
  columnDescriptor.push_back(LV_GRID_TEMPLATE_LAST);
  rowDescriptor.assign(rows, (lv_coord_t)screen.rowHeight);
  rowDescriptor.push_back(LV_GRID_TEMPLATE_LAST);

  lv_obj_set_grid_dsc_array(parent, columnDescriptor.data(), rowDescriptor.data());
  lv_obj_set_layout(parent, LV_LAYOUT_GRID);

  for (size_t i = 0; i < screen.widgets.size(); i++) {
    const configModel::Widget &widget = screen.widgets[i];
    lv_obj_t *object = NULL;
    bool known = true;

    switch (widget.type) {
      case configModel::WidgetType::Button: object = renderButton(widget, parent, known); break;
      case configModel::WidgetType::Label: object = renderLabel(widget, parent); break;
      case configModel::WidgetType::Slider: object = renderSlider(widget, parent, known); break;
      case configModel::WidgetType::Arc: object = renderArc(widget, parent, known); break;
      case configModel::WidgetType::List:
        object = renderList(widget, parent, result.widgetsDisabled);
        break;
      case configModel::WidgetType::StatusBar:
        // Status comes from guiStatusUpdate, which owns its own labels. Here it
        // is a placeholder that reserves the cell.
        object = lv_label_create(parent);
        lv_label_set_text(object, "");
        break;
      case configModel::WidgetType::Spacer:
        object = lv_obj_create(parent);
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
        break;
    }

    if (object == NULL) continue;
    placeInGrid(object, widget);
    result.widgetsDrawn++;
    if (!known && widget.type != configModel::WidgetType::List) result.widgetsDisabled++;
  }

  omote_log_i("uiRenderer: screen '%s', %u widget(s), %u disabled\r\n", screen.name.c_str(),
              (unsigned)result.widgetsDrawn, (unsigned)result.widgetsDisabled);
  return result;
}

} // namespace uiRenderer
