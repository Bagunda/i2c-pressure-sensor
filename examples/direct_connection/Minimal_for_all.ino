#include <Wire.h>

/* ===== I2C ===== */
static const uint8_t I2C_ADDR = 0x78;
static const uint8_t CMD_MEASURE = 0xAC;
static const int SDA_PIN = 21;
static const int SCL_PIN = 22;
static const uint32_t I2C_CLOCK = 10000;

/* ===== Pressure calibration ===== */
static const float PMIN = 0.0f;    // bar
static const float PMAX = 10.0f;   // bar
static const uint32_t DMIN = 1677722UL;   // 10%
static const uint32_t DMAX = 15099494UL;  // 90%

/* ===== Timing ===== */
static const uint32_t SAMPLE_PERIOD_MS = 250;  // 4 Hz
static const uint8_t MEASURE_DELAY_MS = 10;

uint32_t lastSample = 0;

bool i2cWriteCmd(uint8_t cmd) {
  Wire.beginTransmission(I2C_ADDR);
  Wire.write(cmd);
  return Wire.endTransmission(true) == 0;
}

bool i2cRead6(uint8_t *buf) {
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

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_CLOCK);

  Serial.println("Pressure sensor start");
}

void loop() {
  uint32_t now = millis();
  if (now - lastSample < SAMPLE_PERIOD_MS) return;
  lastSample = now;

  // start measurement
  if (!i2cWriteCmd(CMD_MEASURE)) {
    Serial.println("I2C write failed");
    return;
  }

  delay(MEASURE_DELAY_MS);

  uint8_t buf[6];
  if (!i2cRead6(buf)) {
    Serial.println("I2C read failed");
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

  Serial.print("P=");
  Serial.print(pBar, 3);
  Serial.print(" bar | rawP=");
  Serial.print(rawP);
  Serial.print(" | rawT=");
  Serial.println(rawT);
}

