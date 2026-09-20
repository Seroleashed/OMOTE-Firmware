/*
  Unit tests for applicationInternal/gui/uiRenderer.cpp

  Against a real LVGL with no display driver behind it: lv_init() plus a flush
  callback that does nothing is enough to create objects and then walk the tree.
  No window, no SDL, no screenshots.

  What that can check: that the right number and kind of objects appear, that a
  button is wired to the right command, that pressing one sends it, and that a
  widget naming a command this device does not have comes out disabled rather
  than missing.

  What it cannot check: whether the result looks right. Pixels need a display,
  and that is what the browser preview of step 21 is for.
*/

#include <unity.h>

#include <lvgl.h>

#include <string>

#include "applicationInternal/commandHandler.h"
#include "applicationInternal/gui/uiRenderer.h"
// declares SerialClass, millis() and delay() for a build without Arduino
#include "applicationInternal/hardware/arduinoLayer.h"
#include "applicationInternal/hardware/IRremoteProtocols.h"
#include "applicationInternal/storage/configUi.h"
#include "devices/misc/device_specialCommands.h"

// --- a display that goes nowhere ---------------------------------------------

static lv_disp_draw_buf_t drawBuffer;
static lv_color_t pixels[SCR_WIDTH * 10];
static lv_disp_drv_t displayDriver;
static bool lvglReady = false;

static void flushNowhere(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
  (void)area;
  (void)colors;
  // LVGL insists on being told the flush finished, even when there is nothing
  // to flush to.
  lv_disp_flush_ready(driver);
}

static void startLvglOnce() {
  if (lvglReady) return;
  lv_init();
  lv_disp_draw_buf_init(&drawBuffer, pixels, NULL, SCR_WIDTH * 10);
  lv_disp_drv_init(&displayDriver);
  displayDriver.draw_buf = &drawBuffer;
  displayDriver.flush_cb = flushNowhere;
  displayDriver.hor_res = SCR_WIDTH;
  displayDriver.ver_res = SCR_HEIGHT;
  lv_disp_drv_register(&displayDriver);
  lvglReady = true;
}

// --- what the firmware would call --------------------------------------------

static uint16_t TV_POWER;
static uint16_t TV_VOLUME;
static int irSendCount = 0;
static std::string lastPayload;

// The renderer calls executeCommand, which lives in commandHandler and needs
// the hardware facade. Only the pieces it actually reaches are provided here.
void sendIRcode(int protocol, std::list<std::string> payloads, std::string additionalPayload) {
  (void)protocol;
  (void)payloads;
  irSendCount++;
  lastPayload = additionalPayload;
}
void setLastActivityTimestamp() {}
void showNewIRmessage(std::string message) { (void)message; }
void handleScene(uint16_t command, commandData data, std::string additionalPayload) {
  (void)command;
  (void)data;
  (void)additionalPayload;
}
void handleGUI(uint16_t command, commandData data, std::string additionalPayload) {
  (void)command;
  (void)data;
  (void)additionalPayload;
}
bool publishMQTTMessage(const char *topic, const char *payload) {
  (void)topic;
  (void)payload;
  return true;
}
bool getIsWifiConnected() { return true; }
void showMQTTmessage(std::string topic, std::string payload) {
  (void)topic;
  (void)payload;
}
void showWiFiConnected(bool connected) { (void)connected; }

// The log writes to Serial, which on a device is the Arduino layer and in the
// simulator a small stand-in. Neither is linked here, so this is the whole of
// it - the log level is NONE in this environment anyway.
SerialClass Serial;
void SerialClass::begin(unsigned long) {}
size_t SerialClass::printf(const char *format, ...) {
  (void)format;
  return 0;
}
size_t SerialClass::println(const char c[]) {
  (void)c;
  return 0;
}
size_t SerialClass::println(int nr) {
  (void)nr;
  return 0;
}
unsigned long millis() { return 0; }
void delay(uint32_t ms) { (void)ms; }

static lv_obj_t *parent = NULL;

static void giveCommandUnknownAnIdOfItsOwn() {
  /*
    Ids are handed out from 0, and COMMAND_UNKNOWN starts as 0 - so without
    this the *first* command registered is indistinguishable from "no such
    command", and every widget comes out disabled. main.cpp calls this first
    for the same reason.
  */
  static bool done = false;
  if (done) return;
  register_specialCommands();
  done = true;
}

void setUp(void) {
  startLvglOnce();
  giveCommandUnknownAnIdOfItsOwn();
  irSendCount = 0;
  lastPayload.clear();

  // a fresh container per test, so one test's widgets cannot be counted by the
  // next one
  if (parent != NULL) lv_obj_del(parent);
  parent = lv_obj_create(lv_scr_act());
  lv_obj_set_size(parent, SCR_WIDTH, SCR_HEIGHT);

  register_command(&TV_POWER, makeCommandData(IR, {std::to_string(IR_PROTOCOL_NEC), "0xAAAA"}));
  register_command(&TV_VOLUME, makeCommandData(IR, {std::to_string(IR_PROTOCOL_NEC), "0xBBBB"}));
}

void tearDown(void) {}

static configModel::Widget button(uint8_t row, uint8_t column, const std::string &label,
                                  const std::string &command) {
  configModel::Widget widget;
  widget.type = configModel::WidgetType::Button;
  widget.row = row;
  widget.column = column;
  widget.label = label;
  widget.command = command;
  return widget;
}

// --- drawing ------------------------------------------------------------------

void test_an_empty_screen_draws_nothing_and_does_not_crash(void) {
  configModel::Screen screen;
  screen.name = "Leer";

  uiRenderer::Result result = uiRenderer::render(screen, parent);

  TEST_ASSERT_EQUAL_UINT16(0, result.widgetsDrawn);
  TEST_ASSERT_EQUAL_UINT32(0, lv_obj_get_child_cnt(parent));
}

void test_every_widget_becomes_an_object(void) {
  configModel::Screen screen;
  screen.name = "Numpad";
  screen.columns = 3;
  for (int i = 0; i < 9; i++) {
    screen.widgets.push_back(
        button((uint8_t)(i / 3), (uint8_t)(i % 3), std::to_string(i + 1), "TV_POWER"));
  }

  uiRenderer::Result result = uiRenderer::render(screen, parent);

  TEST_ASSERT_EQUAL_UINT16(9, result.widgetsDrawn);
  TEST_ASSERT_EQUAL_UINT32(9, lv_obj_get_child_cnt(parent));
}

void test_a_button_carries_its_caption(void) {
  configModel::Screen screen;
  screen.name = "X";
  screen.widgets.push_back(button(0, 0, "Ein", "TV_POWER"));

  uiRenderer::render(screen, parent);

  lv_obj_t *drawn = lv_obj_get_child(parent, 0);
  lv_obj_t *caption = lv_obj_get_child(drawn, 0);
  TEST_ASSERT_NOT_NULL(caption);
  TEST_ASSERT_EQUAL_STRING("Ein", lv_label_get_text(caption));
}

void test_the_grid_gets_as_many_rows_as_the_widgets_reach(void) {
  // worked out from the widgets rather than stated in the file: a row count
  // that disagrees with them is a mistake waiting to happen
  configModel::Screen screen;
  screen.name = "X";
  screen.columns = 2;
  screen.widgets.push_back(button(0, 0, "a", "TV_POWER"));
  screen.widgets.push_back(button(4, 1, "b", "TV_POWER"));

  uiRenderer::render(screen, parent);

  lv_obj_t *lower = lv_obj_get_child(parent, 1);
  TEST_ASSERT_EQUAL_UINT32(4, lv_obj_get_style_grid_cell_row_pos(lower, LV_PART_MAIN));
}

void test_each_widget_type_produces_the_right_object(void) {
  configModel::Screen screen;
  screen.name = "Alles";
  screen.columns = 1;

  configModel::Widget slider;
  slider.type = configModel::WidgetType::Slider;
  slider.row = 0;
  slider.command = "TV_VOLUME";
  slider.minimum = 0;
  slider.maximum = 64;
  screen.widgets.push_back(slider);

  configModel::Widget arc;
  arc.type = configModel::WidgetType::Arc;
  arc.row = 1;
  arc.command = "TV_VOLUME";
  screen.widgets.push_back(arc);

  configModel::Widget label;
  label.type = configModel::WidgetType::Label;
  label.row = 2;
  label.label = "Hallo";
  screen.widgets.push_back(label);

  uiRenderer::render(screen, parent);

  TEST_ASSERT_TRUE(lv_obj_check_type(lv_obj_get_child(parent, 0), &lv_slider_class));
  TEST_ASSERT_TRUE(lv_obj_check_type(lv_obj_get_child(parent, 1), &lv_arc_class));
  TEST_ASSERT_TRUE(lv_obj_check_type(lv_obj_get_child(parent, 2), &lv_label_class));
  TEST_ASSERT_EQUAL_STRING("Hallo", lv_label_get_text(lv_obj_get_child(parent, 2)));
}

void test_a_slider_keeps_its_range(void) {
  configModel::Screen screen;
  screen.name = "X";
  configModel::Widget slider;
  slider.type = configModel::WidgetType::Slider;
  slider.command = "TV_VOLUME";
  slider.minimum = 10;
  slider.maximum = 64;
  screen.widgets.push_back(slider);

  uiRenderer::render(screen, parent);

  lv_obj_t *drawn = lv_obj_get_child(parent, 0);
  TEST_ASSERT_EQUAL_INT32(10, lv_slider_get_min_value(drawn));
  TEST_ASSERT_EQUAL_INT32(64, lv_slider_get_max_value(drawn));
}

void test_a_list_becomes_one_button_per_item(void) {
  configModel::Screen screen;
  screen.name = "X";
  configModel::Widget list;
  list.type = configModel::WidgetType::List;
  for (int i = 0; i < 3; i++) {
    configModel::ListItem item;
    item.label = "Eintrag " + std::to_string(i);
    item.command = "TV_POWER";
    list.items.push_back(item);
  }
  screen.widgets.push_back(list);

  uiRenderer::render(screen, parent);

  lv_obj_t *drawn = lv_obj_get_child(parent, 0);
  TEST_ASSERT_EQUAL_UINT32(3, lv_obj_get_child_cnt(drawn));
}

// --- binding -------------------------------------------------------------------

void test_pressing_a_button_sends_its_command(void) {
  // the whole point of the renderer, and the one thing a screenshot could
  // never show
  configModel::Screen screen;
  screen.name = "X";
  screen.widgets.push_back(button(0, 0, "Ein", "TV_POWER"));

  uiRenderer::render(screen, parent);
  lv_event_send(lv_obj_get_child(parent, 0), LV_EVENT_CLICKED, NULL);

  TEST_ASSERT_EQUAL_INT(1, irSendCount);
}

void test_a_slider_sends_where_it_was_moved_to(void) {
  configModel::Screen screen;
  screen.name = "X";
  configModel::Widget slider;
  slider.type = configModel::WidgetType::Slider;
  slider.command = "TV_VOLUME";
  slider.minimum = 0;
  slider.maximum = 100;
  screen.widgets.push_back(slider);

  uiRenderer::render(screen, parent);
  lv_obj_t *drawn = lv_obj_get_child(parent, 0);
  lv_slider_set_value(drawn, 42, LV_ANIM_OFF);
  lv_event_send(drawn, LV_EVENT_VALUE_CHANGED, NULL);

  TEST_ASSERT_EQUAL_INT(1, irSendCount);
  TEST_ASSERT_EQUAL_STRING("42", lastPayload.c_str());
}

void test_an_unknown_command_leaves_a_disabled_widget_not_a_hole(void) {
  /*
    A file from another remote naming a device this one does not have is the
    normal case, not an error. A screen that silently loses half its buttons is
    harder to work out than one showing greyed-out ones with the right captions.
  */
  configModel::Screen screen;
  screen.name = "X";
  screen.columns = 2;
  screen.widgets.push_back(button(0, 0, "Ein", "TV_POWER"));
  screen.widgets.push_back(button(0, 1, "Fremd", "GIBT_ES_NICHT"));

  uiRenderer::Result result = uiRenderer::render(screen, parent);

  TEST_ASSERT_EQUAL_UINT16(2, result.widgetsDrawn);
  TEST_ASSERT_EQUAL_UINT16(1, result.widgetsDisabled);

  lv_obj_t *foreign = lv_obj_get_child(parent, 1);
  TEST_ASSERT_TRUE(lv_obj_has_state(foreign, LV_STATE_DISABLED));
  // and the caption is still there, so the user can see what is missing
  TEST_ASSERT_EQUAL_STRING("Fremd", lv_label_get_text(lv_obj_get_child(foreign, 0)));
}

void test_pressing_a_disabled_widget_sends_nothing(void) {
  configModel::Screen screen;
  screen.name = "X";
  screen.widgets.push_back(button(0, 0, "Fremd", "GIBT_ES_NICHT"));

  uiRenderer::render(screen, parent);
  lv_event_send(lv_obj_get_child(parent, 0), LV_EVENT_CLICKED, NULL);

  TEST_ASSERT_EQUAL_INT(0, irSendCount);
}

// --- memory --------------------------------------------------------------------

void test_deleting_the_parent_takes_everything_with_it(void) {
  /*
    guiMemoryOptimizer keeps three tabs and deletes the rest. Everything the
    renderer makes hangs off the parent it was given, so there is no
    bookkeeping of our own to get wrong - which is worth proving rather than
    assuming.
  */
  configModel::Screen screen;
  screen.name = "X";
  screen.columns = 3;
  for (int i = 0; i < 9; i++) {
    screen.widgets.push_back(
        button((uint8_t)(i / 3), (uint8_t)(i % 3), std::to_string(i), "TV_POWER"));
  }

  lv_obj_t *container = lv_obj_create(parent);
  uiRenderer::render(screen, container);
  TEST_ASSERT_EQUAL_UINT32(9, lv_obj_get_child_cnt(container));

  lv_obj_del(container);
  TEST_ASSERT_EQUAL_UINT32(0, lv_obj_get_child_cnt(parent));
}

void test_rendering_the_same_screen_twice_does_not_pile_up(void) {
  // switching back and forth between two screens is the normal thing a user
  // does, and it happens through a fresh parent every time
  configModel::Screen screen;
  screen.name = "X";
  screen.widgets.push_back(button(0, 0, "a", "TV_POWER"));

  for (int i = 0; i < 5; i++) {
    lv_obj_t *container = lv_obj_create(parent);
    uiRenderer::render(screen, container);
    TEST_ASSERT_EQUAL_UINT32(1, lv_obj_get_child_cnt(container));
    lv_obj_del(container);
  }
  TEST_ASSERT_EQUAL_UINT32(0, lv_obj_get_child_cnt(parent));
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_an_empty_screen_draws_nothing_and_does_not_crash);
  RUN_TEST(test_every_widget_becomes_an_object);
  RUN_TEST(test_a_button_carries_its_caption);
  RUN_TEST(test_the_grid_gets_as_many_rows_as_the_widgets_reach);
  RUN_TEST(test_each_widget_type_produces_the_right_object);
  RUN_TEST(test_a_slider_keeps_its_range);
  RUN_TEST(test_a_list_becomes_one_button_per_item);
  RUN_TEST(test_pressing_a_button_sends_its_command);
  RUN_TEST(test_a_slider_sends_where_it_was_moved_to);
  RUN_TEST(test_an_unknown_command_leaves_a_disabled_widget_not_a_hole);
  RUN_TEST(test_pressing_a_disabled_widget_sends_nothing);
  RUN_TEST(test_deleting_the_parent_takes_everything_with_it);
  RUN_TEST(test_rendering_the_same_screen_twice_does_not_pile_up);
  return UNITY_END();
}
