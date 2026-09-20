/*
  Unit tests for /cfg/ui.json (configUi.cpp).

  This is the format the screen editor of step 21 will write and the renderer of
  the next branch will draw. Both of those are expensive to get wrong, so the
  rules live here where they are cheap to check.

  The recurring theme: refuse what would draw *something* but not the right
  thing. A widget reaching past the edge of the grid, a button with nothing on
  it, a slider whose range is backwards - LVGL will happily render all three,
  and the author will spend an evening wondering what they did.
*/

#include <unity.h>

#include <string>

#include "applicationInternal/storage/configUi.h"

void setUp(void) {}
void tearDown(void) {}

static configModel::UiConfig numpadLike() {
  configModel::Screen screen;
  screen.name = "Numpad";
  screen.columns = 3;
  screen.rowHeight = 52;

  for (int i = 0; i < 9; i++) {
    configModel::Widget button;
    button.type = configModel::WidgetType::Button;
    button.row = (uint8_t)(i / 3);
    button.column = (uint8_t)(i % 3);
    button.label = std::to_string(i + 1);
    button.command = "SAMSUNG_NUM_" + std::to_string(i + 1);
    screen.widgets.push_back(button);
  }

  configModel::Widget volume;
  volume.type = configModel::WidgetType::Slider;
  volume.row = 3;
  volume.column = 0;
  volume.columnSpan = 3;
  volume.command = "YAMAHA_VOLUME";
  volume.minimum = 0;
  volume.maximum = 64;
  screen.widgets.push_back(volume);

  configModel::UiConfig config;
  config.screens.push_back(screen);
  return config;
}

// --- round trip --------------------------------------------------------------

void test_a_screen_survives_serialize_and_parse(void) {
  configModel::UiConfig written = numpadLike();
  std::string json = configModel::serializeUi(written);

  configModel::UiConfig read;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseUi(json, read, error), error.c_str());

  TEST_ASSERT_EQUAL_size_t(1, read.screens.size());
  TEST_ASSERT_EQUAL_STRING("Numpad", read.screens[0].name.c_str());
  TEST_ASSERT_EQUAL_UINT8(3, read.screens[0].columns);
  TEST_ASSERT_EQUAL_UINT16(52, read.screens[0].rowHeight);
  TEST_ASSERT_EQUAL_size_t(10, read.screens[0].widgets.size());

  const configModel::Widget &slider = read.screens[0].widgets[9];
  TEST_ASSERT_EQUAL_INT((int)configModel::WidgetType::Slider, (int)slider.type);
  TEST_ASSERT_EQUAL_UINT8(3, slider.columnSpan);
  TEST_ASSERT_EQUAL_INT16(64, slider.maximum);
  TEST_ASSERT_EQUAL_STRING("YAMAHA_VOLUME", slider.command.c_str());
}

void test_widgets_reference_commands_by_name(void) {
  std::string json = configModel::serializeUi(numpadLike());
  TEST_ASSERT_TRUE(json.find("SAMSUNG_NUM_1") != std::string::npos);
  // the numeric command id must never reach a file
  TEST_ASSERT_TRUE(json.find("commandId") == std::string::npos);
}

void test_a_span_of_one_is_not_written(void) {
  // it is the default, and on every widget of a 20 button screen it is noise
  std::string json = configModel::serializeUi(numpadLike());
  size_t firstButton = json.find("SAMSUNG_NUM_1");
  TEST_ASSERT_TRUE(firstButton != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"rowSpan\"") == std::string::npos ||
                   json.find("\"rowSpan\"") > firstButton);
}

void test_every_widget_type_round_trips(void) {
  configModel::Screen screen;
  screen.name = "Alles";
  screen.columns = 2;

  const configModel::WidgetType types[] = {
      configModel::WidgetType::Button, configModel::WidgetType::Label,
      configModel::WidgetType::Slider, configModel::WidgetType::Arc,
      configModel::WidgetType::List,   configModel::WidgetType::StatusBar,
      configModel::WidgetType::Spacer};

  for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
    configModel::Widget widget;
    widget.type = types[i];
    widget.row = (uint8_t)i;
    widget.label = "x";
    widget.command = "TV_POWER";
    configModel::ListItem item;
    item.label = "eins";
    item.command = "TV_POWER";
    widget.items.push_back(item);
    screen.widgets.push_back(widget);
  }

  configModel::UiConfig written;
  written.screens.push_back(screen);

  configModel::UiConfig read;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(configModel::parseUi(configModel::serializeUi(written), read, error),
                           error.c_str());
  TEST_ASSERT_EQUAL_size_t(7, read.screens[0].widgets.size());
  for (size_t i = 0; i < 7; i++) {
    TEST_ASSERT_EQUAL_INT((int)types[i], (int)read.screens[0].widgets[i].type);
  }
}

void test_a_screen_without_widgets_is_valid(void) {
  // the editor of step 21 starts every new screen exactly like this
  configModel::UiConfig read;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(
      configModel::parseUi("{\"schemaVersion\":1,\"type\":\"omote.ui\","
                           "\"screens\":[{\"name\":\"Leer\"}]}",
                           read, error),
      error.c_str());
  TEST_ASSERT_EQUAL_size_t(0, read.screens[0].widgets.size());
}

// --- what is refused ---------------------------------------------------------

static void assertRejected(const std::string &json, const char *expectedTextInError) {
  configModel::UiConfig config;
  std::string error;
  TEST_ASSERT_FALSE_MESSAGE(configModel::parseUi(json, config, error), json.c_str());
  TEST_ASSERT_FALSE(error.empty());
  if (expectedTextInError != NULL) {
    TEST_ASSERT_TRUE_MESSAGE(error.find(expectedTextInError) != std::string::npos, error.c_str());
  }
}

void test_a_widget_reaching_past_the_grid_is_refused(void) {
  /*
    LVGL renders this as something subtly wrong rather than obviously broken,
    and the author spends a while wondering why the layout is off. Cheaper to
    refuse, with the numbers in the message.
  */
  assertRejected("{\"schemaVersion\":1,\"type\":\"omote.ui\",\"screens\":[{\"name\":\"X\","
                 "\"grid\":{\"columns\":3},"
                 "\"widgets\":[{\"type\":\"button\",\"column\":2,\"columnSpan\":2,"
                 "\"label\":\"a\",\"command\":\"C\"}]}]}",
                 "does not fit");
}

void test_a_button_with_nothing_on_it_is_refused(void) {
  // an invisible thing the user can press
  assertRejected("{\"schemaVersion\":1,\"type\":\"omote.ui\",\"screens\":[{\"name\":\"X\","
                 "\"widgets\":[{\"type\":\"button\",\"command\":\"C\"}]}]}",
                 "label or an icon");
}

void test_a_slider_with_a_backwards_range_is_refused(void) {
  assertRejected("{\"schemaVersion\":1,\"type\":\"omote.ui\",\"screens\":[{\"name\":\"X\","
                 "\"widgets\":[{\"type\":\"slider\",\"command\":\"C\",\"min\":100,\"max\":0}]}]}",
                 "min must be below max");
}

void test_a_slider_without_a_command_is_refused(void) {
  // it would move and do nothing, which looks like a broken device
  assertRejected("{\"schemaVersion\":1,\"type\":\"omote.ui\",\"screens\":[{\"name\":\"X\","
                 "\"widgets\":[{\"type\":\"slider\",\"min\":0,\"max\":10}]}]}",
                 "needs a command");
}

void test_an_empty_list_is_refused(void) {
  assertRejected("{\"schemaVersion\":1,\"type\":\"omote.ui\",\"screens\":[{\"name\":\"X\","
                 "\"widgets\":[{\"type\":\"list\"}]}]}",
                 "needs items");
}

void test_an_unknown_widget_type_is_named(void) {
  assertRejected("{\"schemaVersion\":1,\"type\":\"omote.ui\",\"screens\":[{\"name\":\"X\","
                 "\"widgets\":[{\"type\":\"gauge\"}]}]}",
                 "gauge");
}

void test_a_span_of_zero_is_refused(void) {
  assertRejected("{\"schemaVersion\":1,\"type\":\"omote.ui\",\"screens\":[{\"name\":\"X\","
                 "\"widgets\":[{\"type\":\"spacer\",\"rowSpan\":0}]}]}",
                 "span of 0");
}

void test_a_grid_without_columns_is_refused(void) {
  assertRejected("{\"schemaVersion\":1,\"type\":\"omote.ui\",\"screens\":[{\"name\":\"X\","
                 "\"grid\":{\"columns\":0}}]}",
                 "at least one column");
}

void test_a_screen_without_a_name_is_refused(void) {
  assertRejected("{\"schemaVersion\":1,\"type\":\"omote.ui\",\"screens\":[{}]}", "without a name");
}

void test_two_screens_of_the_same_name_are_refused(void) {
  assertRejected("{\"schemaVersion\":1,\"type\":\"omote.ui\","
                 "\"screens\":[{\"name\":\"A\"},{\"name\":\"A\"}]}",
                 "twice");
}

void test_a_scenes_file_is_not_a_ui_file(void) {
  assertRejected("{\"schemaVersion\":1,\"type\":\"omote.scenes\",\"scenes\":[]}", "omote.ui");
}

void test_unknown_fields_are_ignored(void) {
  // written by a newer editor that knows a property this firmware does not
  configModel::UiConfig config;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(
      configModel::parseUi("{\"schemaVersion\":1,\"type\":\"omote.ui\",\"theme\":\"dark\","
                           "\"screens\":[{\"name\":\"X\",\"background\":\"blue\","
                           "\"widgets\":[{\"type\":\"label\",\"label\":\"hi\",\"font\":\"big\"}]}]}",
                           config, error),
      error.c_str());
  TEST_ASSERT_EQUAL_size_t(1, config.screens[0].widgets.size());
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_a_screen_survives_serialize_and_parse);
  RUN_TEST(test_widgets_reference_commands_by_name);
  RUN_TEST(test_a_span_of_one_is_not_written);
  RUN_TEST(test_every_widget_type_round_trips);
  RUN_TEST(test_a_screen_without_widgets_is_valid);
  RUN_TEST(test_a_widget_reaching_past_the_grid_is_refused);
  RUN_TEST(test_a_button_with_nothing_on_it_is_refused);
  RUN_TEST(test_a_slider_with_a_backwards_range_is_refused);
  RUN_TEST(test_a_slider_without_a_command_is_refused);
  RUN_TEST(test_an_empty_list_is_refused);
  RUN_TEST(test_an_unknown_widget_type_is_named);
  RUN_TEST(test_a_span_of_zero_is_refused);
  RUN_TEST(test_a_grid_without_columns_is_refused);
  RUN_TEST(test_a_screen_without_a_name_is_refused);
  RUN_TEST(test_two_screens_of_the_same_name_are_refused);
  RUN_TEST(test_a_scenes_file_is_not_a_ui_file);
  RUN_TEST(test_unknown_fields_are_ignored);
  return UNITY_END();
}
