// Copyright (c) 2022 Inaba
// This software is released under the MIT License.
// http://opensource.org/licenses/mit-license.php

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

static String SSID(""), PSK("");
static String INFLUX_MDNS(""), INFLUX_IP("");
static int INFLUX_PORT = 8086;
static String DB_NAME(""), MEASUREMENT(""), ID("");
static Adafruit_NeoPixel strip(8, 32, NEO_GRB + NEO_KHZ800);

template<typename T>
T Key(const char* key, const T initial, const DynamicJsonDocument& json) {
  return json.containsKey(key) ? static_cast<T>(json[key]) : initial;
}

void load() {
  if (!SPIFFS.begin(true)) {
    return;
  }
  auto file = SPIFFS.open("/setting.json", "r");
  if (!file || file.size() == 0) {
    return;
  }
  DynamicJsonDocument json(1300);
  if (::deserializeJson(json, file)) {
    return;
  }
  SSID = Key<const char*>("SSID", "", json);
  PSK = Key<const char*>("PSK", "", json);
  INFLUX_MDNS = Key<const char*>("influx_mdns_addr", "", json);
  INFLUX_IP = Key<const char*>("influx_ip_addr", "", json);
  INFLUX_PORT = Key<int>("influx_port", 8086, json);
  DB_NAME = Key<const char*>("db_name", "", json);
  MEASUREMENT = Key<const char*>("measurement", "", json);
  ID = Key<const char*>("id", "", json);
}

bool connectWiFi() {
  if (SSID.equals("") || PSK.equals("")) {
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID.c_str(), PSK.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    ::delay(500);
  }
  return true;
}

void disconnectWiFi() {
  MDNS.end();
  WiFi.disconnect();
}

String address() {
  if (!INFLUX_MDNS.equals("")) {
    while (!MDNS.begin(WiFi.macAddress().c_str())) {
      ::delay(100);
    }
    return MDNS.queryHost(INFLUX_MDNS).toString();
  } else if (!INFLUX_IP.equals("")) {
    return INFLUX_IP;
  } else {
    return "";
  }
}

void show(bool on) {
# define ON  0xff2000
# define OFF 0
  uint32_t color[][8] = {
    { OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF },
    {  ON,  ON,  ON, OFF, OFF,  ON,  ON,  ON }
  };
  strip.begin();
  for (auto i = 0; i < strip.numPixels(); ++i) {
    strip.setPixelColor(i, color[on ? 1 : 0][i]);
  }
  strip.show();
}

bool update() {
  if (!::connectWiFi()) {
    return false;
  }
  const auto addr = ::address();
  if (addr.equals("") || addr.equals("0.0.0.0")) {
    ::disconnectWiFi();
    return false;
  }
  const String url = "http://" + ::address() + ":" + String(INFLUX_PORT, 10)
      + "/query?db=" + DB_NAME
      + "&q=SELECT%20*%20FROM%20" + MEASUREMENT
      + "%20WHERE%20id='" + ID + "'"
      + "%20ORDER%20BY%20time%20DESC%20LIMIT%201";
  HTTPClient http;
  http.begin(url.c_str());
  if (http.GET() > 0) {
    String payload = http.getString();
    StaticJsonDocument<1000> doc;
    if (!deserializeJson(doc, payload.c_str())) {
      auto r = doc["results"][0];
      auto s = r["series"][0];
      auto v = s["values"][0];
      const int t = v[1];
      ::show(t != 0);
    }
  }
  ::disconnectWiFi();
  return true;
}

void setup() {
  ::load();
}

void loop() {
  if (!update()) {
    ::esp_restart();
  } else {
    ::esp_sleep_enable_timer_wakeup(5 * 1000000);
    ::esp_deep_sleep_start();
  }
  for (;;) {}
  // never reach...
}
