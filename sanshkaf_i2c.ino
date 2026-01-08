#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

#include <ArduinoOTA.h>
#include <ESPmDNS.h>

#include <Wire.h>
#include <WiFiUdp.h>

/* ===== Wi-Fi ===== */
const char* WIFI_SSID = "BagTech";
const char* WIFI_PASS = "Dgd6wjMDt4!";

/* ===== OTA ===== */
const char* HOSTNAME = "esp32-pressure";
const char* OTA_PASS = "123456";

/* ===== Syslog ===== */
WiFiUDP syslogUdp;
const char* SYSLOG_HOST = "10.2.90.210";
const uint16_t SYSLOG_PORT = 1514;

/* ===== I2C ===== */
static const uint8_t I2C_ADDR = 0x78;
static const uint8_t CMD_MEASURE = 0xAC;
static const int SDA_PIN = 21;
static const int SCL_PIN = 22;
static const uint32_t I2C_CLOCK = 10000;

/* ===== Pressure calibration ===== */
static const float PMIN = 0.0f;      // bar
static const float PMAX = 10.0f;     // bar
static const uint32_t DMIN = 1677722UL;
static const uint32_t DMAX = 15099494UL;

/* ===== Timing ===== */
static const uint32_t SAMPLE_PERIOD_MS = 250; // 4 Hz
static const uint8_t MEASURE_DELAY_MS = 10;

/* ===== Web ===== */
WebServer server(80);

/* ===== Utils ===== */
void logln(const String& s) {
  Serial.println(s);
  syslogUdp.beginPacket(SYSLOG_HOST, SYSLOG_PORT);
  syslogUdp.print(HOSTNAME);
  syslogUdp.print(" ");
  syslogUdp.print(s);
  syslogUdp.endPacket();
}

/* ===== I2C helpers ===== */
bool i2cWriteCmd(uint8_t cmd) {
  Wire.beginTransmission(I2C_ADDR);
  Wire.write(cmd);
  return Wire.endTransmission(true) == 0;
}

bool i2cRead6(uint8_t* buf) {
  int n = Wire.requestFrom((int)I2C_ADDR, 6, true);
  if (n != 6) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (int i = 0; i < 6; i++) buf[i] = Wire.read();
  return true;
}

float rawPressureToBar(uint32_t raw) {
  float k = (PMAX - PMIN) / (float)(DMAX - DMIN);
  return k * ((float)raw - (float)DMIN) + PMIN;
}

/* ===== Web OTA ===== */
void setupWebOTA() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/plain",
      "ok\n"
      "Web OTA: /update\n"
      "Arduino OTA hostname: esp32-pressure\n"
    );
  });

  server.on("/update", HTTP_GET, []() {
    server.send(200, "text/html",
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<input type='file' name='update'>"
      "<input type='submit' value='Update'>"
      "</form>"
    );
  });

  server.on("/update", HTTP_POST,
    []() {
      server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
      delay(300);
      ESP.restart();
    },
    []() {
      HTTPUpload& u = server.upload();
      if (u.status == UPLOAD_FILE_START) {
        logln("WebOTA start");
        Update.begin(UPDATE_SIZE_UNKNOWN);
      } else if (u.status == UPLOAD_FILE_WRITE) {
        Update.write(u.buf, u.currentSize);
      } else if (u.status == UPLOAD_FILE_END) {
        Update.end(true);
        logln("WebOTA done");
      }
    }
  );

  server.begin();
  logln("Web OTA ready");
}

/* ===== Arduino OTA ===== */
void setupArduinoOTA() {
  MDNS.begin(HOSTNAME);
  MDNS.addService("arduino", "tcp", 3232);

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASS);

  ArduinoOTA.onStart([]() { logln("ArduinoOTA start"); });
  ArduinoOTA.onEnd([]() { logln("ArduinoOTA end"); });
  ArduinoOTA.onError([](ota_error_t e) {
    logln("ArduinoOTA error=" + String((int)e));
  });

  ArduinoOTA.begin();
  logln("Arduino OTA ready");
}

/* ===== Setup ===== */
void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) delay(300);
  logln("WiFi OK IP=" + WiFi.localIP().toString());

  setupWebOTA();
  setupArduinoOTA();

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_CLOCK);
  delay(50);

  logln("Setup done");
}

/* ===== Loop ===== */
uint32_t lastSample = 0;

void loop() {
  server.handleClient();
  ArduinoOTA.handle();

  uint32_t now = millis();
  if (now - lastSample < SAMPLE_PERIOD_MS) return;
  lastSample = now;

  if (!i2cWriteCmd(CMD_MEASURE)) {
    logln("I2C write failed");
    return;
  }

  delay(MEASURE_DELAY_MS);

  uint8_t buf[6];
  if (!i2cRead6(buf)) {
    logln("I2C read failed");
    return;
  }

  uint32_t rawP =
    ((uint32_t)buf[1] << 16) |
    ((uint32_t)buf[2] << 8)  |
     (uint32_t)buf[3];

  uint16_t rawT =
    ((uint16_t)buf[4] << 8) |
     (uint16_t)buf[5];

  float pBar = rawPressureToBar(rawP);

  logln(
    String("pressure=") + String(pBar, 3) + " bar"
    + " rawP=" + String(rawP)
    + " rawT=" + String(rawT)
  );
}
