#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

const char* ssid = "Gas";
const char* password = "SamBos777";
const char* serverName = "https://iotbinus.projectbos.web.id/api/sensor";
const char* apiKey = "binus-iot-2025";
const char* sensorURL = "https://smart-irrigation-dashboard-phi.vercel.app/api/sensor-data";
const char* irrigationURL = "https://smart-irrigation-dashboard-phi.vercel.app/api/irrigation-data";

#define SDA_PIN 32
#define SCL_PIN 27
#define OLED_ADDR 0x3C
#define DHTPIN 4
#define DHTTYPE DHT11
const int soilPin = 34;
const int waterPin = 15;
const int pumpPin = 23;
const int lampu1 = 25;
const int lampu2 = 14;
const int buzzerPin = 13;

const int SOIL_DRY = 2900;
const int SOIL_WET = 1300;
#define FLOAT_AIR_HABIS LOW

// --- FIX UTAMA DI SINI ---
#define RELAY_ACTIVE_LOW true // true untuk relay biru, kalau masih kebalik ganti jadi false

Adafruit_SSD1306 display(128, 64, &Wire, -1);
DHT dht(DHTPIN, DHTTYPE);
unsigned long lastSend = 0;

// Fungsi anti kebalik
void setPump(bool nyala) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(pumpPin, nyala ? LOW : HIGH);
  } else {
    digitalWrite(pumpPin, nyala ? HIGH : LOW);
  }
}

bool sendJSON(const char* url, JsonDocument& doc, const char* apiKeyHeader = NULL) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");
  if (apiKeyHeader) http.addHeader("x-api-key", apiKeyHeader);
  String json;
  serializeJson(doc, json);
  int httpCode = http.POST(json);
  Serial.printf("[API] %d %s\n", httpCode, url);
  http.end();
  return httpCode >= 200 && httpCode < 300;
}

int bacaSoil() {
  long total = 0;
  for (int i = 0; i < 10; i++) { total += analogRead(soilPin); delay(20); }
  return total / 10;
}

void setup() {
  Serial.begin(115200);
  pinMode(pumpPin, OUTPUT);
  pinMode(lampu1, OUTPUT);
  pinMode(lampu2, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(waterPin, INPUT_PULLUP);
  setPump(false); // PASTIKAN MATI DARI AWAL
  digitalWrite(lampu1, LOW);
  digitalWrite(lampu2, LOW);
  Wire.begin(SDA_PIN, SCL_PIN);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  dht.begin();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi OK - FIX RELAY ACTIVE LOW");
}

void loop() {
  int rawSoil = bacaSoil();
  int persenSoil = map(rawSoil, SOIL_DRY, SOIL_WET, 0, 100);
  persenSoil = constrain(persenSoil, 0, 100);

  int floatStatus = digitalRead(waterPin);
  bool airHabis = (floatStatus == FLOAT_AIR_HABIS);
  bool airAda = !airHabis;
  bool tanahKering = persenSoil < 30;

  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  bool dhtValid = !isnan(humidity) && !isnan(temperature);

  String statusSistem;
  bool pompaHarusNyala = false;

  // --- LOGIKA KUNCI - HANYA 1 SYARAT NYALA ---
  if (airHabis) {
    statusSistem = "AIR HABIS!";
    pompaHarusNyala = false;
  } else if (airAda && tanahKering) {
    statusSistem = "SIRAM ON";
    pompaHarusNyala = true;
  } else {
    statusSistem = "AMAN - BASAH";
    pompaHarusNyala = false;
  }

  // Eksekusi
  setPump(pompaHarusNyala);

  if (airHabis) {
    digitalWrite(lampu1, LOW); digitalWrite(lampu2, HIGH); tone(buzzerPin, 2500, 400);
  } else if (pompaHarusNyala) {
    digitalWrite(lampu1, HIGH); digitalWrite(lampu2, LOW); tone(buzzerPin, 1200, 150);
  } else {
    digitalWrite(lampu1, LOW); digitalWrite(lampu2, LOW); noTone(buzzerPin);
  }

  display.clearDisplay();
  display.setCursor(0,0);
  display.printf("Soil:%d%% %d\n", persenSoil, rawSoil);
  display.printf("T:%.1fC H:%.0f%%\n", dhtValid?temperature:0, dhtValid?humidity:0);
  display.printf("%s\n", airHabis ? "AIR:HABIS!" : "AIR:OK");
  display.println(statusSistem);
  display.display();

  Serial.printf("Soil %d%% | Air %s | Pompa %s | %s\n", persenSoil, airHabis?"HABIS":"OK", pompaHarusNyala?"ON":"OFF", statusSistem.c_str());

  if (millis() - lastSend > 10000) {
    lastSend = millis();
    DynamicJsonDocument doc(768);
    doc["soil_percent"] = persenSoil;
    doc["soil_raw"] = rawSoil;
    doc["water_raw"] = floatStatus;
    doc["water_level_status"] = airHabis ? "EMPTY" : "OK";
    doc["is_water_empty"] = airHabis;
    doc["is_soil_dry"] = tanahKering;
    doc["pump_status"] = pompaHarusNyala ? 1 : 0;
    sendJSON(serverName, doc, apiKey);

    DynamicJsonDocument irrigationDoc(512);
    irrigationDoc["soil_percent"] = persenSoil;
    irrigationDoc["water_level_status"] = airHabis ? "EMPTY" : "OK";
    irrigationDoc["is_water_empty"] = airHabis;
    irrigationDoc["relay_d23"] = pompaHarusNyala;
    irrigationDoc["pump_status"] = pompaHarusNyala ? "ON" : "OFF";
    sendJSON(irrigationURL, irrigationDoc);
  }
  delay(2000);
}