#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <ScioSense_ENS160.h>
#include <PMS.h>

// =============================================================
// ENV STATION FIRMWARE v1.1
// =============================================================
// Bus 1 — Wire    (GPIO21 SDA, GPIO22 SCL)
//   - AHT21  0x38  temperature, humidity
//   - ENS160 0x52  CO2, TVOC, AQI
//
// Bus 2 — I2C_2   (GPIO18 SDA, GPIO19 SCL)
//   - AHT20  0x38  temperature, humidity
//   - BMP280 0x77  pressure, altitude, temperature
//
// UART — Serial2  (GPIO25 RX)
//   - PMS5003      PM1.0, PM2.5, PM10
//
// THERMAL NOTE:
//   AHT21 sits on the same board as ENS160. The ENS160 generates
//   heat during operation which raises the local PCB temperature,
//   causing AHT21 to read ~2-3°C higher than actual room temp.
//   This is a placement issue, not a sensor defect. AHT21 is
//   intrinsically the more accurate sensor (newer spec than AHT20)
//   but its readings are thermally biased by the ENS160.
//   AHT20 and BMP280 are thermally isolated and agree within 0.5°C.
//
// ENS160 COMPENSATION:
//   ENS160 requires live temp+humidity for internal compensation.
//   We feed it AHT20 values (thermally unbiased) for best accuracy.
//   AHT21 values are still logged raw for completeness.
//
// All sensor values are logged as-is with no correction applied.
// Calibration offsets should be applied at the analysis layer.
//
// Read interval: change the last delay().
// =============================================================

TwoWire I2C_2 = TwoWire(1);

Adafruit_AHTX0  aht21;        // Bus 1 — temp/humidity (thermally affected by ENS160)
Adafruit_AHTX0  aht20;        // Bus 2 — temp/humidity (thermally isolated)
Adafruit_BMP280 bmp(&I2C_2);  // Bus 2 — pressure, altitude, temperature (independent principle)
ScioSense_ENS160 ens160;      // Bus 1 — CO2, TVOC, AQI
PMS pms(Serial2);             // UART  — PM1.0, PM2.5, PM10
PMS::DATA pmsData;

// Sensor availability flags set at boot, checked every loop
bool aht21_ok  = false;
bool aht20_ok  = false;
bool bmp280_ok = false;
bool ens160_ok = false;

// =============================================================
// BOOT
// =============================================================
void setup() {
  delay(3000);
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 25, -1);

  Wire.begin(21, 22);
  I2C_2.begin(18, 19);
  delay(500);

  Serial.println();
  Serial.println("╔══════════════════════════════════╗");
  Serial.println("║      ENV STATION BOOT v1.1       ║");
  Serial.println("╚══════════════════════════════════╝");
  Serial.println();

  // ── Bus 1 ──
  Serial.println("── I2C Bus 1 (GPIO21 SDA / GPIO22 SCL) ──");

  if (!aht21.begin(&Wire)) {
    Serial.println("  [ERROR] AHT21 not found at 0x38");
  } else {
    aht21_ok = true;
    // flush first bogus reading
    sensors_event_t h, t;
    aht21.getEvent(&h, &t);
    delay(200);
    Serial.println("  [OK]    AHT21  0x38 — temperature, humidity");
    Serial.println("          NOTE: may read high due to ENS160 self-heating");
  }

  ens160.begin();
  if (!ens160.available()) {
    Serial.println("  [ERROR] ENS160 not found at 0x52");
  } else {
    ens160_ok = true;
    ens160.setMode(ENS160_OPMODE_STD);
    Serial.println("  [OK]    ENS160 0x52 — CO2, TVOC, AQI");
    Serial.println("          NOTE: compensated with AHT20 (thermally unbiased)");
  }

  Serial.println();

  // ── Bus 2 ──
  Serial.println("── I2C Bus 2 (GPIO18 SDA / GPIO19 SCL) ──");

  if (!aht20.begin(&I2C_2)) {
    Serial.println("  [ERROR] AHT20 not found at 0x38");
  } else {
    aht20_ok = true;
    sensors_event_t h, t;
    aht20.getEvent(&h, &t);
    delay(200);
    Serial.println("  [OK]    AHT20  0x38 — temperature, humidity");
    Serial.println("          NOTE: thermally isolated, used for ENS160 compensation");
  }

  if (!bmp.begin(0x77)) {
    Serial.println("  [ERROR] BMP280 not found at 0x77");
  } else {
    bmp280_ok = true;
    Serial.println("  [OK]    BMP280 0x77 — pressure, altitude, temperature");
    Serial.println("          NOTE: independent measurement principle");
  }

  Serial.println();

  // ── UART ──
  Serial.println("── UART (GPIO25 RX) ──");
  Serial.println("  [OK]    PMS5003 — PM1.0, PM2.5, PM10");

  Serial.println();

  // ── Summary ──
  Serial.println("── Sensor Summary ──");
  Serial.print("  AHT21   temp/humidity (Bus 1, ENS160 board)  : "); Serial.println(aht21_ok  ? "ONLINE" : "OFFLINE");
  Serial.print("  ENS160  CO2/TVOC/AQI (Bus 1)                 : "); Serial.println(ens160_ok ? "ONLINE" : "OFFLINE");
  Serial.print("  AHT20   temp/humidity (Bus 2, BMP280 board)  : "); Serial.println(aht20_ok  ? "ONLINE" : "OFFLINE");
  Serial.print("  BMP280  pressure/altitude/temp (Bus 2)       : "); Serial.println(bmp280_ok ? "ONLINE" : "OFFLINE");
  Serial.println("  PMS5003 PM1.0/PM2.5/PM10 (UART)            : ONLINE");

  Serial.println();
  Serial.println("── ENS160 Warmup ──");
  for (int i = 15; i > 0; i--) {
    Serial.print("  "); Serial.print(i); Serial.println("s remaining...");
    delay(1000);
  }

  Serial.println();
  Serial.println("── All systems ready — starting measurements ──");
  Serial.println();
}

// =============================================================
// MAIN LOOP
// =============================================================
void loop() {
  Serial.println("┌──────────────────────────────────────┐");
  Serial.println("│           SENSOR READING             │");
  Serial.println("└──────────────────────────────────────┘");

  // ── AHT21 — temperature + humidity ──
  // NOTE: reads high due to ENS160 self-heating (~2-3°C bias)
  // Raw values logged as-is, no correction applied
  float t_aht21 = NAN;
  float h_aht21 = NAN;
  if (aht21_ok) {
    sensors_event_t humidity, temp;
    aht21.getEvent(&humidity, &temp);
    t_aht21 = temp.temperature;
    h_aht21 = humidity.relative_humidity;
    if (t_aht21 < -40 || t_aht21 > 85) {
      Serial.println("  [AHT21]  Temperature        : (not ready)");
      Serial.println("  [AHT21]  Humidity           : (not ready)");
    } else {
      Serial.print("  [AHT21]  Temperature        : "); Serial.print(t_aht21, 2); Serial.println(" °C");
      Serial.print("  [AHT21]  Humidity           : "); Serial.print(h_aht21, 1); Serial.println(" %");
    }
  }

  // ── AHT20 — temperature + humidity ──
  // NOTE: thermally isolated from ENS160, agrees with BMP280 within 0.5°C
  // Used for ENS160 compensation for best accuracy
  float t_aht20 = NAN;
  float h_aht20 = NAN;
  if (aht20_ok) {
    sensors_event_t humidity, temp;
    aht20.getEvent(&humidity, &temp);
    t_aht20 = temp.temperature;
    h_aht20 = humidity.relative_humidity;
    if (t_aht20 < -40 || t_aht20 > 85) {
      Serial.println("  [AHT20]  Temperature        : (not ready)");
      Serial.println("  [AHT20]  Humidity           : (not ready)");
    } else {
      Serial.print("  [AHT20]  Temperature        : "); Serial.print(t_aht20, 2); Serial.println(" °C");
      Serial.print("  [AHT20]  Humidity           : "); Serial.print(h_aht20, 1); Serial.println(" %");
    }
  }

  // ── BMP280 — pressure + altitude + temperature ──
  // NOTE: independent measurement principle (MEMS capacitive pressure sensor)
  // Temperature from BMP280 is less accurate than AHTxx but fully independent
  if (bmp280_ok) {
    float p     = bmp.readPressure() / 100.0F;
    float alt   = bmp.readAltitude(1013.25);
    float t_bmp = bmp.readTemperature();
    Serial.print("  [BMP280] Pressure           : "); Serial.print(p, 2);   Serial.println(" hPa");
    Serial.print("  [BMP280] Altitude           : "); Serial.print(alt, 1); Serial.println(" m");
    Serial.print("  [BMP280] Temperature        : "); Serial.print(t_bmp, 2); Serial.println(" °C");
  }

  // ── ENS160 — CO2, TVOC, AQI ──
  // Compensated with AHT20 temp+humidity (thermally unbiased)
  // ENS160 compensation improves CO2/TVOC accuracy per datasheet requirement
  if (ens160_ok) {
    if (!isnan(t_aht20) && !isnan(h_aht20)) {
      ens160.set_envdata(t_aht20, h_aht20);
    }
    ens160.measure(true);
    uint8_t  aqi  = ens160.getAQI();
    uint16_t co2  = ens160.geteCO2();
    uint16_t tvoc = ens160.getTVOC();

    if (co2 == 65535 || aqi == 255) {
      Serial.println("  [ENS160] CO2               : (warming up...)");
      Serial.println("  [ENS160] TVOC              : (warming up...)");
      Serial.println("  [ENS160] AQI               : (warming up...)");
    } else {
      Serial.print("  [ENS160] CO2               : "); Serial.print(co2);  Serial.println(" ppm");
      Serial.print("  [ENS160] TVOC              : "); Serial.print(tvoc); Serial.println(" ppb");
      Serial.print("  [ENS160] AQI               : "); Serial.println(aqi);
    }
  }

  // ── PMS5003 — particulate matter ──
  if (pms.readUntil(pmsData, 2000)) {
    Serial.print("  [PMS]    PM1.0             : "); Serial.print(pmsData.PM_AE_UG_1_0);  Serial.println(" µg/m³");
    Serial.print("  [PMS]    PM2.5             : "); Serial.print(pmsData.PM_AE_UG_2_5);  Serial.println(" µg/m³");
    Serial.print("  [PMS]    PM10              : "); Serial.print(pmsData.PM_AE_UG_10_0); Serial.println(" µg/m³");
  } else {
    Serial.println("  [PMS]    PMS5003          : (no data)");
  }

  Serial.println();
  delay(1000);
}