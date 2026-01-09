#include <WiFi.h>
#include <Wire.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <math.h>

/*
  ESP32 + PCA9548A + 4x I2C pressure/temperature sensors (addr 0x78)
  MQTT + Home Assistant AutoDiscovery + per-sensor availability + PCA availability + device LWT + ArduinoOTA.

  Topics:
    sanshkaf/status             -> online/offline   (DEVICE LWT ONLY)
    sanshkaf/pca9548a_ok        -> ON/OFF           (retained)
    sanshkaf/<name>/status      -> online/offline   (retained)
    sanshkaf/<name>/pressure    -> bar (2 decimals) (retained)
    sanshkaf/<name>/temperature -> °C  (1 decimal)  (retained)
    sanshkaf/<name>/overpressure-> ON/OFF           (retained)

  Availability for each entity = ALL of:
    - sanshkaf/status (online/offline)
    - sanshkaf/pca9548a_ok (ON/OFF)
    - sanshkaf/<name>/status (online/offline)

  Reboot button:
    command_topic: sanshkaf/command/reboot
    payload: PRESS
*/

struct Reading {
  uint8_t b[6];
  uint32_t rawP24;
  uint16_t rawT16;
  float p_bar;
  float p_bar_calc;
  float t_c;
  bool valid;
};

struct SensorDef {
  uint8_t muxCh;
  const char* name;
  uint8_t tries;
};

/* ===================== CONFIG ===================== */
/* Wi-Fi */
static const char* WIFI_SSID = "MyWiFi";
static const char* WIFI_PASS = "pass_from_MyWiFi";

/* MQTT */
static const char* MQTT_HOST = "10.0.0.10";
static const uint16_t MQTT_PORT = 1883;
static const char* MQTT_USER = "sanshkaf";
static const char* MQTT_PASS = "sanshkafsanshkaf";

/* IDs */
static const char* NODE_ID  = "sanshkaf";
static const char* HOSTNAME = "esp32-sanshkaf";

/* OTA */
static const char* OTA_PASS = "123456";

/* I2C */
static const int SDA_PIN = 21;
static const int SCL_PIN = 22;
static const uint32_t I2C_CLOCK = 10000;

static const uint8_t PCA_ADDR = 0x70;
static const uint8_t I2C_ADDR = 0x78;
static const uint8_t CMD_MEASURE = 0xAC;

/* Calibration */
static const float PMIN = 0.0f;
static const float PMAX = 10.0f;
static const uint32_t DMIN = 1677722UL;
static const uint32_t DMAX = 15099494UL;

/* Timing */
// 250ms = target 4Hz per-sensor (best effort). Set 1000 for stable 1Hz.
static const uint32_t SAMPLE_PERIOD_MS = 250;
static const uint16_t AFTER_MUX_SWITCH_MS = 5;
static const uint8_t  MEASURE_DELAY_MS = 50;

static const uint32_t SENSOR_OFFLINE_MS = 5000;
static const uint32_t PCA_CHECK_MS = 2000;

/* Overpressure hysteresis */
static const float OVERP_ON_BAR  = 8.0f;
static const float OVERP_OFF_BAR = 7.6f;

/* Sensors */
static const SensorDef SENSORS[] = {
  {1, "cold_top",    3},
  {2, "cold_bottom", 3},
  {3, "hot_top",     6},
  {4, "hot_bottom",  6},
};

/* Topics */
static const char* DEV_AVAIL_TOPIC = "sanshkaf/status";       // LWT device
static const char* PCA_OK_TOPIC    = "sanshkaf/pca9548a_ok";  // ON/OFF retained
static const char* REBOOT_CMD_TOPIC = "sanshkaf/command/reboot";
static const char* REBOOT_PAYLOAD   = "PRESS";

/* ===================== GLOBALS ===================== */
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

uint32_t lastSampleByCh[8] = {0};
uint32_t lastGoodReadMs[8] = {0};
bool overpressureState[8]  = {false};

bool pcaOk = false;
uint32_t lastPcaCheckMs = 0;

// stable clientId
char mqttClientId[96] = {0};

// reboot debounce
uint32_t lastRebootCmdMs = 0;

/* ===================== HELPERS ===================== */
static void i2cFlushReadBuffer() { while (Wire.available()) (void)Wire.read(); }

static bool i2cPing(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission(true) == 0);
}

static bool mqttPublishRetained(const char* topic, const char* payload) {
  return mqtt.publish(topic, payload, true);
}

static void makeTopic(char* out, size_t outSz, const char* fmt, const char* name) {
  snprintf(out, outSz, fmt, name);
}

static void makeSensorStatusTopic(char* out, size_t outSz, const char* name) {
  makeTopic(out, outSz, "sanshkaf/%s/status", name);
}
static void makeOverpressureTopic(char* out, size_t outSz, const char* name) {
  makeTopic(out, outSz, "sanshkaf/%s/overpressure", name);
}
static void makePressureTopic(char* out, size_t outSz, const char* name) {
  makeTopic(out, outSz, "sanshkaf/%s/pressure", name);
}
static void makeTempTopic(char* out, size_t outSz, const char* name) {
  makeTopic(out, outSz, "sanshkaf/%s/temperature", name);
}

/* ===================== PCA9548A ===================== */
static bool pcaWriteMask(uint8_t mask) {
  Wire.beginTransmission(PCA_ADDR);
  Wire.write(mask);
  return (Wire.endTransmission(true) == 0);
}

static bool pcaSelectChannelStrict(uint8_t ch) {
  if (ch > 7) return false;
  uint8_t mask = (uint8_t)(1 << ch);
  if (!pcaWriteMask(0x00)) return false;
  delay(1);
  if (!pcaWriteMask(mask)) return false;
  return true;
}

static bool checkPcaOkOnce() { return i2cPing(PCA_ADDR); }

// Debounce PCA state changes
static bool updatePcaHealthDebounced() {
  static uint8_t okStreak = 0;
  static uint8_t failStreak = 0;

  bool ok = checkPcaOkOnce();
  if (ok) {
    okStreak = (okStreak < 5) ? (uint8_t)(okStreak + 1) : 5;
    failStreak = 0;
  } else {
    failStreak = (failStreak < 5) ? (uint8_t)(failStreak + 1) : 5;
    okStreak = 0;
  }

  if (!pcaOk && okStreak >= 3) { pcaOk = true;  return true; }
  if ( pcaOk && failStreak >= 3) { pcaOk = false; return true; }
  return false;
}

static void publishPcaOk() {
  mqttPublishRetained(PCA_OK_TOPIC, pcaOk ? "ON" : "OFF");
}

/* ===================== SENSOR I2C READ ===================== */
static bool i2cWriteMeasureCmd() {
  Wire.beginTransmission(I2C_ADDR);
  Wire.write(CMD_MEASURE);
  return (Wire.endTransmission(true) == 0);
}

static bool i2cRead6(uint8_t out6[6]) {
  int n = Wire.requestFrom((int)I2C_ADDR, 6, true);
  if (n != 6) {
    i2cFlushReadBuffer();
    return false;
  }
  for (int i = 0; i < 6; i++) out6[i] = Wire.read();
  i2cFlushReadBuffer();
  return true;
}

static bool looksLikeEmptyFrame(const uint8_t b[6]) {
  bool all0 = true, allFF = true;
  for (int i = 0; i < 6; i++) {
    if (b[i] != 0x00) all0 = false;
    if (b[i] != 0xFF) allFF = false;
  }
  return all0 || allFF;
}

static float rawPressureToBar(float rawP24) {
  const float spanD = (float)(DMAX - DMIN);
  const float spanP = (PMAX - PMIN);
  return ((rawP24 - (float)DMIN) * spanP / spanD) + PMIN;
}

// switchable temperature formula
#define TEMP_MODE_SIMPLE   1   // raw/1000
#define TEMP_MODE_DOC      2   // datasheet formula

#define TEMP_MODE TEMP_MODE_DOC

static float rawTempToC(uint16_t rawT16) {
#if TEMP_MODE == TEMP_MODE_SIMPLE
  return rawT16 / 1000.0f;
#elif TEMP_MODE == TEMP_MODE_DOC
  return (rawT16 / 65536.0f) * 190.0f - 40.0f;
#endif
}

static Reading readWithRetries(uint8_t tries) {
  Reading r{};
  memset(r.b, 0, sizeof(r.b));
  r.rawP24 = 0;
  r.rawT16 = 0;
  r.p_bar = NAN;
  r.p_bar_calc = NAN;
  r.t_c = NAN;
  r.valid = false;

  if (!i2cPing(I2C_ADDR)) return r;

  for (uint8_t a = 0; a < tries; a++) {
    if (!i2cWriteMeasureCmd()) continue;
    delay(MEASURE_DELAY_MS);

    if (!i2cRead6(r.b)) continue;
    if (looksLikeEmptyFrame(r.b)) continue;

    r.rawP24 =
      ((uint32_t)r.b[1] << 16) |
      ((uint32_t)r.b[2] << 8)  |
       (uint32_t)r.b[3];

    r.rawT16 =
      ((uint16_t)r.b[4] << 8) |
       (uint16_t)r.b[5];

    if (r.rawP24 == 0) continue;

    r.p_bar_calc = rawPressureToBar((float)r.rawP24);
    r.p_bar = (r.p_bar_calc < 0.0f) ? 0.0f : r.p_bar_calc;
    r.t_c = rawTempToC(r.rawT16);

    r.valid = true;
    return r;
  }

  return r;
}

/* ===================== MQTT RX ===================== */
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, REBOOT_CMD_TOPIC) != 0) return;

  char buf[32];
  unsigned int n = (length < sizeof(buf)-1) ? length : (sizeof(buf)-1);
  memcpy(buf, payload, n);
  buf[n] = '\0';

  if (strcmp(buf, REBOOT_PAYLOAD) == 0) {
    uint32_t now = millis();
    if (now - lastRebootCmdMs > 10000) {
      lastRebootCmdMs = now;

      // graceful disconnect -> broker should NOT fire LWT
      mqtt.disconnect();
      delay(100);
      ESP.restart();
    }
  }
}

/* ===================== HA DISCOVERY ===================== */
static void mqttPublishDiscoveryOnce() {
  char dev[240];
  snprintf(dev, sizeof(dev),
    "\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"BagTech\",\"model\":\"ESP32 + PCA9548A\"",
    NODE_ID, NODE_ID
  );

  // PCA OK sensor
  {
    char cfgTopic[220];
    snprintf(cfgTopic, sizeof(cfgTopic), "homeassistant/binary_sensor/%s/pca9548a_ok/config", NODE_ID);

    char payload[820];
    snprintf(payload, sizeof(payload),
      "{"
        "\"name\":\"%s PCA9548A OK\","
        "\"object_id\":\"%s_pca9548a_ok\","
        "\"unique_id\":\"%s_pca9548a_ok\","
        "\"state_topic\":\"%s\","
        "\"payload_on\":\"ON\","
        "\"payload_off\":\"OFF\","
        "\"device_class\":\"connectivity\","
        "\"availability_topic\":\"%s\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"device\":{%s}"
      "}",
      NODE_ID, NODE_ID, NODE_ID, PCA_OK_TOPIC, DEV_AVAIL_TOPIC, dev
    );
    mqttPublishRetained(cfgTopic, payload);
  }

  // Reboot button
  {
    char cfgTopic[220];
    snprintf(cfgTopic, sizeof(cfgTopic), "homeassistant/button/%s/reboot/config", NODE_ID);

    char payload[820];
    snprintf(payload, sizeof(payload),
      "{"
        "\"name\":\"%s Reboot\","
        "\"object_id\":\"%s_reboot\","
        "\"unique_id\":\"%s_reboot\","
        "\"command_topic\":\"%s\","
        "\"payload_press\":\"%s\","
        "\"availability_topic\":\"%s\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"device\":{%s}"
      "}",
      NODE_ID, NODE_ID, NODE_ID, REBOOT_CMD_TOPIC, REBOOT_PAYLOAD, DEV_AVAIL_TOPIC, dev
    );
    mqttPublishRetained(cfgTopic, payload);
  }

  for (const auto &s : SENSORS) {
    char sensorAvail[140];
    makeSensorStatusTopic(sensorAvail, sizeof(sensorAvail), s.name);

    // Availability = device LWT + PCA OK + per-sensor status
    char availBlock[620];
    snprintf(availBlock, sizeof(availBlock),
      "\"availability\":["
        "{"
          "\"topic\":\"%s\","
          "\"payload_available\":\"online\","
          "\"payload_not_available\":\"offline\""
        "},"
        "{"
          "\"topic\":\"%s\","
          "\"payload_available\":\"ON\","
          "\"payload_not_available\":\"OFF\""
        "},"
        "{"
          "\"topic\":\"%s\","
          "\"payload_available\":\"online\","
          "\"payload_not_available\":\"offline\""
        "}"
      "],"
      "\"availability_mode\":\"all\",",
      DEV_AVAIL_TOPIC, PCA_OK_TOPIC, sensorAvail
    );

    // Pressure
    {
      char cfgTopic[240];
      char stateTopic[160];
      char uniqueId[200];
      char objId[200];
      snprintf(cfgTopic, sizeof(cfgTopic), "homeassistant/sensor/%s/%s_pressure/config", NODE_ID, s.name);
      makePressureTopic(stateTopic, sizeof(stateTopic), s.name);
      snprintf(uniqueId, sizeof(uniqueId), "%s_%s_pressure", NODE_ID, s.name);
      snprintf(objId, sizeof(objId), "%s_%s_pressure", NODE_ID, s.name);

      char payload[1100];
      snprintf(payload, sizeof(payload),
        "{"
          "\"name\":\"%s %s pressure\","
          "\"object_id\":\"%s\","
          "\"unique_id\":\"%s\","
          "\"state_topic\":\"%s\","
          "\"unit_of_measurement\":\"bar\","
          "\"device_class\":\"pressure\","
          "\"state_class\":\"measurement\","
          "%s"
          "\"device\":{%s}"
        "}",
        NODE_ID, s.name, objId, uniqueId, stateTopic, availBlock, dev
      );
      mqttPublishRetained(cfgTopic, payload);
    }

    // Temperature
    {
      char cfgTopic[240];
      char stateTopic[160];
      char uniqueId[220];
      char objId[220];
      snprintf(cfgTopic, sizeof(cfgTopic), "homeassistant/sensor/%s/%s_temperature/config", NODE_ID, s.name);
      makeTempTopic(stateTopic, sizeof(stateTopic), s.name);
      snprintf(uniqueId, sizeof(uniqueId), "%s_%s_temperature", NODE_ID, s.name);
      snprintf(objId, sizeof(objId), "%s_%s_temperature", NODE_ID, s.name);

      char payload[1150];
      snprintf(payload, sizeof(payload),
        "{"
          "\"name\":\"%s %s temperature\","
          "\"object_id\":\"%s\","
          "\"unique_id\":\"%s\","
          "\"state_topic\":\"%s\","
          "\"unit_of_measurement\":\"°C\","
          "\"device_class\":\"temperature\","
          "\"state_class\":\"measurement\","
          "%s"
          "\"device\":{%s}"
        "}",
        NODE_ID, s.name, objId, uniqueId, stateTopic, availBlock, dev
      );
      mqttPublishRetained(cfgTopic, payload);
    }

    // Overpressure
    {
      char cfgTopic[260];
      char stateTopic[180];
      char uniqueId[240];
      char objId[240];
      snprintf(cfgTopic, sizeof(cfgTopic), "homeassistant/binary_sensor/%s/%s_overpressure/config", NODE_ID, s.name);
      makeOverpressureTopic(stateTopic, sizeof(stateTopic), s.name);
      snprintf(uniqueId, sizeof(uniqueId), "%s_%s_overpressure", NODE_ID, s.name);
      snprintf(objId, sizeof(objId), "%s_%s_overpressure", NODE_ID, s.name);

      char payload[1180];
      snprintf(payload, sizeof(payload),
        "{"
          "\"name\":\"%s %s overpressure\","
          "\"object_id\":\"%s\","
          "\"unique_id\":\"%s\","
          "\"state_topic\":\"%s\","
          "\"device_class\":\"problem\","
          "\"payload_on\":\"ON\","
          "\"payload_off\":\"OFF\","
          "%s"
          "\"device\":{%s}"
        "}",
        NODE_ID, s.name, objId, uniqueId, stateTopic, availBlock, dev
      );
      mqttPublishRetained(cfgTopic, payload);
    }
  }
}

/* ===================== MQTT CONNECT ===================== */
static void mqttBuildStableClientId() {
  uint8_t mac[6];
  WiFi.macAddress(mac); // работает в Arduino-ESP32
  snprintf(mqttClientId, sizeof(mqttClientId),
           "%s-%02X%02X%02X%02X%02X%02X",
           HOSTNAME, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void mqttEnsureConnected() {
  while (!mqtt.connected()) {
    mqtt.setBufferSize(1400);
    mqtt.setCallback(mqttCallback);

    // LWT only. Broker publishes "offline" if this client disappears ungracefully.
    if (mqtt.connect(mqttClientId, MQTT_USER, MQTT_PASS, DEV_AVAIL_TOPIC, 0, true, "offline")) {
      mqttPublishRetained(DEV_AVAIL_TOPIC, "online");

      mqtt.subscribe(REBOOT_CMD_TOPIC, 0);

      mqttPublishDiscoveryOnce();

      // initial retained states to avoid "unknown"
      publishPcaOk();
      for (const auto &s : SENSORS) {
        char st[160];
        makeSensorStatusTopic(st, sizeof(st), s.name);
        mqttPublishRetained(st, "offline");

        char op[160];
        makeOverpressureTopic(op, sizeof(op), s.name);
        mqttPublishRetained(op, "OFF");
      }
      return;
    }

    delay(1500);
  }
}

/* ===================== OTA ===================== */
static void otaSetup() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.begin();
}

/* ===================== PUBLISH DATA ===================== */
static void publishSensorOnline(const SensorDef& s) {
  char st[160];
  makeSensorStatusTopic(st, sizeof(st), s.name);
  mqttPublishRetained(st, "online");
}

static void publishSensorOffline(const SensorDef& s) {
  char st[160];
  makeSensorStatusTopic(st, sizeof(st), s.name);
  mqttPublishRetained(st, "offline");
}

static void publishSensorValues(const SensorDef& s, const Reading& r) {
  char tP[160];
  char pStr[24];
  snprintf(pStr, sizeof(pStr), "%.2f", (double)r.p_bar);
  makePressureTopic(tP, sizeof(tP), s.name);
  mqtt.publish(tP, pStr, true);

  char tT[160];
  char tStr[24];
  snprintf(tStr, sizeof(tStr), "%.1f", (double)r.t_c);
  makeTempTopic(tT, sizeof(tT), s.name);
  mqtt.publish(tT, tStr, true);
}

static void publishOverpressureState(const SensorDef& s, bool state) {
  char topic[160];
  makeOverpressureTopic(topic, sizeof(topic), s.name);
  mqtt.publish(topic, state ? "ON" : "OFF", true);
}

static void updateAndPublishOverpressure(const SensorDef& s, float p_bar) {
  bool cur = overpressureState[s.muxCh];
  bool next = cur;

  if (!cur && p_bar >= OVERP_ON_BAR) next = true;
  if (cur && p_bar <= OVERP_OFF_BAR) next = false;

  if (next != cur) {
    overpressureState[s.muxCh] = next;
    publishOverpressureState(s, next);
  }
}

/* ===================== POLLING ===================== */
static void pollSensor(const SensorDef& s) {
  if (!pcaOk) return;

  if (!pcaSelectChannelStrict(s.muxCh)) return;
  delay(AFTER_MUX_SWITCH_MS);
  i2cFlushReadBuffer();

  Reading r = readWithRetries(s.tries);
  if (!r.valid) return;

  lastGoodReadMs[s.muxCh] = millis();
  publishSensorOnline(s);

  publishSensorValues(s, r);
  updateAndPublishOverpressure(s, r.p_bar);
}

static void handleSensorExpiry() {
  uint32_t now = millis();
  for (const auto &s : SENSORS) {
    uint32_t last = lastGoodReadMs[s.muxCh];
    if (last == 0) continue;
    if (now - last > SENSOR_OFFLINE_MS) {
      publishSensorOffline(s);
      lastGoodReadMs[s.muxCh] = 0;
    }
  }
}

/* ===================== PCA HEALTH LOOP ===================== */
static void handlePcaHealth() {
  uint32_t now = millis();
  if (now - lastPcaCheckMs < PCA_CHECK_MS) return;
  lastPcaCheckMs = now;

  bool changed = updatePcaHealthDebounced();
  if (changed) {
    publishPcaOk();
    if (!pcaOk) {
      for (const auto &s : SENSORS) {
        publishSensorOffline(s);
        lastGoodReadMs[s.muxCh] = 0;
      }
    }
  }
}

/* ===================== SETUP/LOOP ===================== */
void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  otaSetup();

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_CLOCK);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqttBuildStableClientId();

  // initial PCA state
  pcaOk = checkPcaOkOnce();

  for (const auto &s : SENSORS) {
    overpressureState[s.muxCh] = false;
    lastGoodReadMs[s.muxCh] = 0;
  }

  mqttEnsureConnected();

  // publish PCA retained state once connected
  publishPcaOk();
}

void loop() {
  ArduinoOTA.handle();

  mqttEnsureConnected();
  mqtt.loop();

  handlePcaHealth();

  uint32_t now = millis();
  for (const auto &s : SENSORS) {
    if (now - lastSampleByCh[s.muxCh] >= SAMPLE_PERIOD_MS) {
      lastSampleByCh[s.muxCh] = now;
      pollSensor(s);
    }
  }

  handleSensorExpiry();
}
