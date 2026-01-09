/*
  Minimal example: ESP32 + PCA9548A (0x70) + I2C pressure sensor (0x78)

  - Reads ALL PCA channels (0..7)
  - Prints ONLY successful readings to Serial
  - No Wi-Fi, no OTA, no syslog, no WebServer

  Wiring (ESP32 default):
    SDA = GPIO21
    SCL = GPIO22
*/

#include <Wire.h>
#include <math.h>

/* ===== I2C pins/clock ===== */
static const int      SDA_PIN   = 21;
static const int      SCL_PIN   = 22;
static const uint32_t I2C_CLOCK = 10000;

/* ===== PCA9548A ===== */
static const uint8_t PCA_ADDR = 0x70;

/* ===== Sensor ===== */
static const uint8_t I2C_ADDR     = 0x78;  // pressure sensor address
static const uint8_t CMD_MEASURE  = 0xAC;  // start measurement

/* ===== Calibration (from your "battle" sketch) ===== */
static const float    PMIN = 0.0f;
static const float    PMAX = 10.0f;
static const uint32_t DMIN = 1677722UL;
static const uint32_t DMAX = 15099494UL;

/* ===== Timing ===== */
static const uint16_t AFTER_MUX_SWITCH_MS = 5;
static const uint8_t  MEASURE_DELAY_MS    = 50;
static const uint32_t SAMPLE_PERIOD_MS    = 500;

/* ===== Types ===== */
struct Reading {
  uint8_t  b[6];
  uint32_t rawP24;
  uint16_t rawT16;
  float    p_bar;       // clamped >= 0
  float    p_bar_calc;  // may be negative
  float    temp_c;       // temperature in Celsius (inaccurate!)
  bool     valid;
};

/* ===== Helpers ===== */
static void i2cFlushReadBuffer() {
  while (Wire.available()) (void)Wire.read();
}

static bool i2cPing(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission(true) == 0;
}

/* ===== PCA9548A ===== */
static bool pcaWriteMask(uint8_t mask) {
  Wire.beginTransmission(PCA_ADDR);
  Wire.write(mask);
  return Wire.endTransmission(true) == 0;
}

static uint8_t pcaReadMask() {
  uint8_t v = 0xFF;
  int n = Wire.requestFrom((int)PCA_ADDR, 1, true);
  if (n == 1) v = Wire.read();
  i2cFlushReadBuffer();
  return v;
}

static bool pcaSelectChannelStrict(uint8_t ch) {
  if (ch > 7) return false;
  uint8_t mask = (uint8_t)(1u << ch);

  // выключаем все каналы, затем включаем нужный
  if (!pcaWriteMask(0x00)) return false;
  delay(1);
  if (!pcaWriteMask(mask)) return false;
  delay(1);

  return (pcaReadMask() == mask);
}

/* ===== Sensor protocol ===== */
static bool i2cWriteMeasureCmd() {
  Wire.beginTransmission(I2C_ADDR);
  Wire.write((uint8_t)CMD_MEASURE);
  Wire.write((uint8_t)0x00);
  Wire.write((uint8_t)0x00);
  return Wire.endTransmission(true) == 0;
}

static bool i2cRead6(uint8_t *buf) {
  int n = Wire.requestFrom((int)I2C_ADDR, 6, true);
  if (n != 6) {
    i2cFlushReadBuffer();
    return false;
  }
  for (int i = 0; i < 6; i++) buf[i] = Wire.read();
  return true;
}

static bool looksLikeEmptyFrame(const uint8_t b[6]) {
  // если все данные нули — считаем "пусто"
  return (b[1] == 0 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[5] == 0);
}

static float rawPressureToBar(float raw) {
  float k = (PMAX - PMIN) / (float)(DMAX - DMIN);
  return k * (raw - (float)DMIN) + PMIN;
}

/* ⚠️ ВАЖНО: Температура показывает очень неточно!
 * Производитель рекомендует не использовать значение температуры как реальный термометр.
 * Температура среды должна быть ниже 50°C.
 * Формула из документации: T = (rawT / 65536) * 190 - 40
 */
static float rawTemperatureToCelsius(uint16_t rawT) {
  return (rawT / 65536.0f) * 190.0f - 40.0f;
}

/* Read on CURRENT selected PCA channel.
   Returns valid=true only if a real frame was received. */
static Reading readWithRetries(uint8_t tries) {
  Reading r{};
  for (int i = 0; i < 6; i++) r.b[i] = 0;
  r.rawP24 = 0;
  r.rawT16 = 0;
  r.p_bar = NAN;
  r.p_bar_calc = NAN;
  r.temp_c = NAN;
  r.valid = false;

  // датчик должен отвечать на текущем канале
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
    r.temp_c = rawTemperatureToCelsius(r.rawT16);

    r.valid = true;
    return r; // первый успешный кадр
  }

  return r;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_CLOCK);

  Serial.println();
  Serial.println("ESP32 + PCA9548A + I2C pressure sensor (minimal)");

  if (i2cPing(PCA_ADDR)) {
    Serial.println("PCA9548A: OK (0x70)");
  } else {
    Serial.println("PCA9548A: FAIL (0x70) — check wiring/power!");
  }
}

void loop() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < SAMPLE_PERIOD_MS) return;
  last = now;

  for (uint8_t ch = 0; ch < 8; ch++) {
    if (!pcaSelectChannelStrict(ch)) {
      // молчим — минимальный лог
      continue;
    }
    delay(AFTER_MUX_SWITCH_MS);

    Reading r = readWithRetries(3);
    if (!r.valid) continue; // печатаем только успешные чтения

    // status — это первый байт ответа (как у тебя в логах 0x04)
    uint8_t status = r.b[0];

    Serial.printf("ch=%u status=0x%02X rawP=%lu rawT=%u Pbar=%.3f T=%.1fC(inaccurate!)\n",
                  ch, status, (unsigned long)r.rawP24, (unsigned)r.rawT16, r.p_bar, r.temp_c);
  }
}

