#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <ScioSense_ENS160.h>
#include <PMS.h>
#include <WiFi.h>
#include <HTTPClient.h>

// =============================================================
// ENV STATION FIRMWARE v2.0
// =============================================================
// Added: WiFi + InfluxDB HTTP POST
// All sensor code unchanged from v1.1
// =============================================================

// =============================================================
// CONFIGURATION — fill these in before uploading
// =============================================================

// Your WiFi network name and password
const char* WIFI_SSID     = "your_wifi_network_name";
const char* WIFI_PASS     = "your_wifi_password";

// Your Mac's local IP — find it with: ipconfig getifaddr en0
// Consider assigning a static IP in your router's DHCP settings
// so you don't have to reflash when it changes
const char* INFLUX_URL    = "http://YOUR_MAC_IP:8086";

// InfluxDB API token — copy from InfluxDB UI: Data > API Tokens
const char* INFLUX_TOKEN  = "your_influxdb_api_token";

const char* INFLUX_ORG    = "weatherroom";
const char* INFLUX_BUCKET = "sensors";

// Read interval in milliseconds
const int READ_INTERVAL_MS = 5000;

// =============================================================
// HARDWARE
// =============================================================
TwoWire I2C_2 = TwoWire(1);

Adafruit_AHTX0  aht21;
Adafruit_AHTX0  aht20;
Adafruit_BMP280 bmp(&I2C_2);
ScioSense_ENS160 ens160;
PMS pms(Serial2);
PMS::DATA pmsData;

bool aht21_ok  = false;
bool aht20_ok  = false;
bool bmp280_ok = false;
bool ens160_ok = false;
bool wifi_ok   = false;

// =============================================================
// WIFI
// =============================================================
void connectWiFi() {
  Serial.print("── Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifi_ok = true;
    Serial.println();
    Serial.print("  [OK] WiFi connected — IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("  [WARN] WiFi not connected — running Serial-only mode");
  }
}

// =============================================================
// INFLUXDB WRITE
// =============================================================
void writeToInflux(String lineProtocol) {
  if (!wifi_ok || WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(INFLUX_URL) + "/api/v2/write?org=" +
               INFLUX_ORG + "&bucket=" + INFLUX_BUCKET + "&precision=s";

  http.begin(url);
  http.addHeader("Authorization", String("Token ") + INFLUX_TOKEN);
  http.addHeader("Content-Type", "text/plain; charset=utf-8");

  int code = http.POST(lineProtocol);

  if (code != 204) {
    Serial.print("  [WARN] InfluxDB write failed — HTTP ");
    Serial.println(code);
  }

  http.end();
}

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
  Serial.println("║      ENV STATION BOOT v2.0       ║");
  Serial.println("╚══════════════════════════════════╝");
  Serial.println();

  // ── WiFi ──
  Serial.println("── Network ──");
  connectWiFi();
  Serial.println();

  // ── Bus 1 ──
  Serial.println("── I2C Bus 1 (GPIO21 SDA / GPIO22 SCL) ──");

  if (!aht21.begin(&Wire)) {
    Serial.println("  [ERROR] AHT21 not found at 0x38");
  } else {
    aht21_ok = true;
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
  }

  if (!bmp.begin(0x77)) {
    Serial.println("  [ERROR] BMP280 not found at 0x77");
  } else {
    bmp280_ok = true;
    Serial.println("  [OK]    BMP280 0x77 — pressure, altitude, temperature");
  }

  Serial.println();

  // ── UART ──
  Serial.println("── UART (GPIO25 RX) ──");
  Serial.println("  [OK]    PMS5003 — PM1.0, PM2.5, PM10");
  Serial.println();

  // ── Summary ──
  Serial.println("── System Summary ──");
  Serial.print("  WiFi                                 : "); Serial.println(wifi_ok    ? "ONLINE" : "OFFLINE");
  Serial.print("  AHT21   temp/humidity (Bus 1)        : "); Serial.println(aht21_ok  ? "ONLINE" : "OFFLINE");
  Serial.print("  ENS160  CO2/TVOC/AQI (Bus 1)         : "); Serial.println(ens160_ok ? "ONLINE" : "OFFLINE");
  Serial.print("  AHT20   temp/humidity (Bus 2)        : "); Serial.println(aht20_ok  ? "ONLINE" : "OFFLINE");
  Serial.print("  BMP280  pressure/temp (Bus 2)        : "); Serial.println(bmp280_ok ? "ONLINE" : "OFFLINE");
  Serial.println("  PMS5003 particulates (UART)         : ONLINE");

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

  // ── AHT21 ──
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

  // ── AHT20 ──
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

  // ── BMP280 ──
  float p     = NAN;
  float alt   = NAN;
  float t_bmp = NAN;
  if (bmp280_ok) {
    p     = bmp.readPressure() / 100.0F;
    alt   = bmp.readAltitude(1013.25);
    t_bmp = bmp.readTemperature();
    Serial.print("  [BMP280] Pressure           : "); Serial.print(p, 2);   Serial.println(" hPa");
    Serial.print("  [BMP280] Altitude           : "); Serial.print(alt, 1); Serial.println(" m");
    Serial.print("  [BMP280] Temperature        : "); Serial.print(t_bmp, 2); Serial.println(" °C");
  }

  // ── ENS160 ──
  uint8_t  aqi  = 0;
  uint16_t co2  = 0;
  uint16_t tvoc = 0;
  bool ens_ready = false;
  if (ens160_ok) {
    if (!isnan(t_aht20) && !isnan(h_aht20)) {
      ens160.set_envdata(t_aht20, h_aht20);
    }
    ens160.measure(true);
    aqi  = ens160.getAQI();
    co2  = ens160.geteCO2();
    tvoc = ens160.getTVOC();

    if (co2 == 65535 || aqi == 255) {
      Serial.println("  [ENS160] CO2               : (warming up...)");
      Serial.println("  [ENS160] TVOC              : (warming up...)");
      Serial.println("  [ENS160] AQI               : (warming up...)");
    } else {
      ens_ready = true;
      Serial.print("  [ENS160] CO2               : "); Serial.print(co2);  Serial.println(" ppm");
      Serial.print("  [ENS160] TVOC              : "); Serial.print(tvoc); Serial.println(" ppb");
      Serial.print("  [ENS160] AQI               : "); Serial.println(aqi);
    }
  }

  // ── PMS5003 ──
  float pm1 = NAN, pm25 = NAN, pm10 = NAN;
  if (pms.readUntil(pmsData, 500)) {
    pm1  = pmsData.PM_AE_UG_1_0;
    pm25 = pmsData.PM_AE_UG_2_5;
    pm10 = pmsData.PM_AE_UG_10_0;
    Serial.print("  [PMS]    PM1.0             : "); Serial.print(pm1);  Serial.println(" µg/m³");
    Serial.print("  [PMS]    PM2.5             : "); Serial.print(pm25); Serial.println(" µg/m³");
    Serial.print("  [PMS]    PM10              : "); Serial.print(pm10); Serial.println(" µg/m³");
  } else {
    Serial.println("  [PMS]    PMS5003          : (no data)");
  }

  // ── InfluxDB write ──
  String line = "air_quality";
  line += " ";

  bool first = true;
  auto addField = [&](String name, float val) {
    if (!isnan(val)) {
      if (!first) line += ",";
      line += name + "=" + String(val, 2);
      first = false;
    }
  };
  auto addFieldInt = [&](String name, int val, bool valid) {
    if (valid) {
      if (!first) line += ",";
      line += name + "=" + String(val);
      first = false;
    }
  };

  addField("temp_aht21", t_aht21);
  addField("temp_aht20", t_aht20);
  addField("temp_bmp280", t_bmp);
  addField("humidity_aht21", h_aht21);
  addField("humidity_aht20", h_aht20);
  addField("pressure", p);
  addField("altitude", alt);
  addFieldInt("co2", co2, ens_ready);
  addFieldInt("tvoc", tvoc, ens_ready);
  addFieldInt("aqi", aqi, ens_ready);
  addField("pm1", pm1);
  addField("pm25", pm25);
  addField("pm10", pm10);

  if (!first) {
    writeToInflux(line);
    if (wifi_ok) Serial.println("  [WiFi]   InfluxDB write sent");
  }

  Serial.println();
  delay(READ_INTERVAL_MS);
}
