/*
  Unit tests for applicationInternal/credentials.cpp

  Two things are being pinned down here. The first is the fallback chain -
  stored beats secrets.h beats nothing - because getting it wrong either ignores
  what the user typed or ignores what they flashed.

  The second is that a password never leaves by accident. The export tests in
  test_config_export check that no credential reaches a file; these check the
  other side, that the only way to get one out is a function whose name says so.
*/

#include <unity.h>

#include <map>
#include <string>

#include "applicationInternal/credentials.h"
#include "secrets.h"

// in-memory stand-in for NVS
class FakeCredentialStorage : public CredentialStorage {
public:
  std::map<std::string, std::string> values;
  bool writesFail = false;

  bool read(const std::string &key, std::string &value) override {
    if (values.count(key) == 0) return false;
    value = values[key];
    return true;
  }
  bool write(const std::string &key, const std::string &value) override {
    if (writesFail) return false;
    values[key] = value;
    return true;
  }
  bool remove(const std::string &key) override { return values.erase(key) > 0; }
  bool exists(const std::string &key) override { return values.count(key) > 0; }
};

static FakeCredentialStorage storage;

void setUp(void) {
  storage = FakeCredentialStorage();
  credentials::begin(&storage);
}

void tearDown(void) {}

// --- the fallback chain ------------------------------------------------------

void test_without_anything_stored_secrets_h_decides(void) {
  /*
    secrets.h in the repository holds the placeholders it ships with, so the
    source here is None. On a checkout where somebody filled them in it would be
    CompiledIn - both are correct, and asserting on the placeholder case is what
    a fresh clone actually has.
  */
  if (std::string(WIFI_SSID) == "YourWifiSSID") {
    TEST_ASSERT_EQUAL_INT((int)credentials::Source::None, (int)credentials::wifiSource());
    TEST_ASSERT_FALSE(credentials::hasWifi());
  } else {
    TEST_ASSERT_EQUAL_INT((int)credentials::Source::CompiledIn, (int)credentials::wifiSource());
    TEST_ASSERT_EQUAL_STRING(WIFI_SSID, credentials::wifiSsid().c_str());
  }
}

void test_a_stored_network_wins_over_secrets_h(void) {
  TEST_ASSERT_TRUE(credentials::setWifi("Wohnzimmer", "geheim123"));

  TEST_ASSERT_EQUAL_INT((int)credentials::Source::Stored, (int)credentials::wifiSource());
  TEST_ASSERT_EQUAL_STRING("Wohnzimmer", credentials::wifiSsid().c_str());
  TEST_ASSERT_TRUE(credentials::hasWifi());
  TEST_ASSERT_TRUE(credentials::hasWifiPassword());
}

void test_clearing_falls_back_to_what_was_there_before(void) {
  credentials::setWifi("Wohnzimmer", "geheim123");
  TEST_ASSERT_TRUE(credentials::clearWifi());

  TEST_ASSERT_NOT_EQUAL((int)credentials::Source::Stored, (int)credentials::wifiSource());
  TEST_ASSERT_NOT_EQUAL(0, credentials::wifiSsid() != "Wohnzimmer");
}

void test_an_empty_network_name_is_refused(void) {
  // storing one would leave the device unable to connect and unable to say why
  TEST_ASSERT_FALSE(credentials::setWifi("", "geheim123"));
  TEST_ASSERT_NOT_EQUAL((int)credentials::Source::Stored, (int)credentials::wifiSource());
}

void test_an_open_network_is_a_valid_configuration(void) {
  // a network without a password is a real thing, and must not be mistaken for
  // "no password stored yet"
  TEST_ASSERT_TRUE(credentials::setWifi("Gastnetz", ""));

  TEST_ASSERT_EQUAL_INT((int)credentials::Source::Stored, (int)credentials::wifiSource());
  TEST_ASSERT_EQUAL_STRING("Gastnetz", credentials::wifiSsid().c_str());
  TEST_ASSERT_FALSE(credentials::hasWifiPassword());
  TEST_ASSERT_EQUAL_STRING("", credentials::wifiPasswordForConnecting().c_str());
}

void test_a_failing_write_is_reported(void) {
  storage.writesFail = true;
  TEST_ASSERT_FALSE(credentials::setWifi("Wohnzimmer", "geheim123"));
}

void test_without_a_storage_nothing_crashes(void) {
  credentials::begin(NULL);
  TEST_ASSERT_FALSE(credentials::setWifi("x", "y"));
  TEST_ASSERT_FALSE(credentials::clearWifi());
  credentials::wifiSsid();
  credentials::statusText();

  credentials::begin(&storage);
}

// --- MQTT --------------------------------------------------------------------

void test_a_stored_mqtt_login_wins(void) {
  TEST_ASSERT_TRUE(credentials::setMqtt("omote", "brokerpass"));

  TEST_ASSERT_EQUAL_INT((int)credentials::Source::Stored, (int)credentials::mqttSource());
  TEST_ASSERT_EQUAL_STRING("omote", credentials::mqttUser().c_str());
  TEST_ASSERT_TRUE(credentials::hasMqttPassword());
  TEST_ASSERT_EQUAL_STRING("brokerpass", credentials::mqttPasswordForConnecting().c_str());
}

void test_a_broker_without_a_login_is_a_valid_configuration(void) {
  // the common case for a broker on the home network
  TEST_ASSERT_TRUE(credentials::setMqtt("", ""));
  TEST_ASSERT_EQUAL_STRING("", credentials::mqttUser().c_str());
  TEST_ASSERT_FALSE(credentials::hasMqttPassword());
}

void test_clearing_the_mqtt_login_falls_back(void) {
  credentials::setMqtt("omote", "brokerpass");
  TEST_ASSERT_TRUE(credentials::clearMqtt());
  TEST_ASSERT_NOT_EQUAL((int)credentials::Source::Stored, (int)credentials::mqttSource());
}

// --- the access point fallback -----------------------------------------------

void test_without_any_network_the_access_point_comes_up_at_once(void) {
  // no point counting to three when there is nothing to try
  if (credentials::hasWifi()) {
    TEST_IGNORE_MESSAGE("secrets.h has real credentials on this checkout");
  }
  TEST_ASSERT_TRUE(credentials::shouldStartAccessPoint());
}

void test_one_or_two_failures_are_not_enough(void) {
  // a router rebooting must not throw the user into a configuration mode
  credentials::setWifi("Wohnzimmer", "geheim123");
  TEST_ASSERT_FALSE(credentials::shouldStartAccessPoint());

  credentials::reportConnectFailed();
  TEST_ASSERT_FALSE(credentials::shouldStartAccessPoint());
  credentials::reportConnectFailed();
  TEST_ASSERT_FALSE(credentials::shouldStartAccessPoint());
}

void test_three_failures_bring_up_the_access_point(void) {
  credentials::setWifi("Wohnzimmer", "falschesPasswort");

  for (int i = 0; i < 3; i++) credentials::reportConnectFailed();

  TEST_ASSERT_EQUAL_UINT8(3, credentials::failedConnects());
  TEST_ASSERT_TRUE(credentials::shouldStartAccessPoint());
}

void test_a_successful_connect_clears_the_count(void) {
  credentials::setWifi("Wohnzimmer", "geheim123");
  credentials::reportConnectFailed();
  credentials::reportConnectFailed();
  credentials::reportConnected();

  TEST_ASSERT_EQUAL_UINT8(0, credentials::failedConnects());
  credentials::reportConnectFailed();
  TEST_ASSERT_FALSE(credentials::shouldStartAccessPoint());
}

void test_new_credentials_get_a_fresh_set_of_attempts(void) {
  // the user just fixed the password; making them wait out the old failures
  // would be absurd
  credentials::setWifi("Wohnzimmer", "falsch");
  for (int i = 0; i < 3; i++) credentials::reportConnectFailed();
  TEST_ASSERT_TRUE(credentials::shouldStartAccessPoint());

  credentials::setWifi("Wohnzimmer", "richtig");
  TEST_ASSERT_EQUAL_UINT8(0, credentials::failedConnects());
  TEST_ASSERT_FALSE(credentials::shouldStartAccessPoint());
}

// --- nothing leaks -----------------------------------------------------------

void test_the_status_text_never_contains_a_password(void) {
  // it goes to the log and onto the display
  credentials::setWifi("Wohnzimmer", "sehrGeheim");
  std::string status = credentials::statusText();

  TEST_ASSERT_TRUE_MESSAGE(status.find("sehrGeheim") == std::string::npos, status.c_str());
  TEST_ASSERT_TRUE_MESSAGE(status.find("Wohnzimmer") != std::string::npos, status.c_str());
}

void test_a_password_is_stored_under_its_own_key(void) {
  // so that "clear the WiFi" cannot leave the password behind under a key
  // somebody forgot about
  credentials::setWifi("Wohnzimmer", "sehrGeheim");
  credentials::clearWifi();

  for (std::map<std::string, std::string>::const_iterator it = storage.values.begin();
       it != storage.values.end(); ++it) {
    TEST_ASSERT_TRUE_MESSAGE(it->second != "sehrGeheim", it->first.c_str());
  }
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_without_anything_stored_secrets_h_decides);
  RUN_TEST(test_a_stored_network_wins_over_secrets_h);
  RUN_TEST(test_clearing_falls_back_to_what_was_there_before);
  RUN_TEST(test_an_empty_network_name_is_refused);
  RUN_TEST(test_an_open_network_is_a_valid_configuration);
  RUN_TEST(test_a_failing_write_is_reported);
  RUN_TEST(test_without_a_storage_nothing_crashes);
  RUN_TEST(test_a_stored_mqtt_login_wins);
  RUN_TEST(test_a_broker_without_a_login_is_a_valid_configuration);
  RUN_TEST(test_clearing_the_mqtt_login_falls_back);
  RUN_TEST(test_without_any_network_the_access_point_comes_up_at_once);
  RUN_TEST(test_one_or_two_failures_are_not_enough);
  RUN_TEST(test_three_failures_bring_up_the_access_point);
  RUN_TEST(test_a_successful_connect_clears_the_count);
  RUN_TEST(test_new_credentials_get_a_fresh_set_of_attempts);
  RUN_TEST(test_the_status_text_never_contains_a_password);
  RUN_TEST(test_a_password_is_stored_under_its_own_key);
  return UNITY_END();
}
