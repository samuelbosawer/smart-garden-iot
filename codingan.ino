#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// --- WIFI KAKA GANTI DISINI ---
const char* ssid = "Gas";
const char* password = "SamBos777";
const char* serverName = "https://iotbinus.projectbos.web.id/api/sensor";
const char* apiKey = "binus-iot-2025";

// --- PIN SESUAI KAKA ---
#define SDA_PIN 32
#define SCL_PIN 27
#define OLED_ADDR 0x3C
const int soilPin = 34;
const int waterPin = 33;
const int pumpPin = 23;
const int lampu1 = 25; // D25
const int lampu2 = 14; // D14
const int buzzerPin = 13; // D13

Adafruit_SSD1306 display(128, 64, &Wire, -1);
unsigned long lastSend = 0;

void setup() {
  Serial.begin(115200);
  pinMode(pumpPin, OUTPUT);
  pinMode(lampu1, OUTPUT);
  pinMode(lampu2, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(pumpPin, LOW);
  digitalWrite(lampu1, LOW);
  digitalWrite(lampu2, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(1);
  display.setCursor(0,0);
  display.println("Connect WiFi...");
  display.display();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi OK");
  display.println("WiFi OK!");
  display.println(WiFi.localIP());
  display.display();
  delay(2000);
}

void loop() {
  int rawSoil = analogRead(soilPin);
  int rawWater = analogRead(waterPin);
  int persenSoil = map(rawSoil, 2900, 1300, 0, 100);
  persenSoil = constrain(persenSoil, 0, 100);

  bool airHabis = rawWater < 300;
  bool tanahKering = persenSoil < 30;

  // LOGIC POMPA LAMPU BUZZER
  if (airHabis) {
    digitalWrite(pumpPin, LOW);
    digitalWrite(lampu1, LOW);
    digitalWrite(lampu2, HIGH);
    tone(buzzerPin, 2500, 400);
  } else if (tanahKering) {
    digitalWrite(pumpPin, HIGH);
    digitalWrite(lampu1, HIGH);
    digitalWrite(lampu2, LOW);
    tone(buzzerPin, 1200, 150);
  } else {
    digitalWrite(pumpPin, LOW);
    digitalWrite(lampu1, LOW);
    digitalWrite(lampu2, LOW);
    noTone(buzzerPin);
  }

  // TAMPIL OLED
  display.clearDisplay();
  display.setCursor(0,0);
  display.printf("Soil:%d%% W:%d\n", persenSoil, rawWater);
  display.printf("L1 D25:%s L2 D14:%s\n", digitalRead(lampu1)?"ON":"OFF", digitalRead(lampu2)?"ON":"OFF");
  display.printf("WiFi:%s\n", WiFi.status()==WL_CONNECTED?"OK":"FAIL");
  display.printf("Kirim: %s\n", (millis()-lastSend)/1000 > 10 ? "YA" : "WAIT");
  if(airHabis) display.println("ALERT AIR HABIS");
  else if(tanahKering) display.println("SIRAM ON");
  else display.println("AMAN");
  display.display();

  // KIRIM KE LARAVEL SETIAP 10 DETIK
  if (millis() - lastSend > 10000) {
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(serverName);
      http.addHeader("Content-Type", "application/json");
      http.addHeader("x-api-key", apiKey);

      DynamicJsonDocument doc(512);
      doc["soil_percent"] = persenSoil;
      doc["soil_raw"] = rawSoil;
      doc["water_raw"] = rawWater;
      doc["is_water_empty"] = airHabis;
      doc["is_soil_dry"] = tanahKering;
      doc["pump_status"] = digitalRead(pumpPin);
      doc["lampu1_d25"] = digitalRead(lampu1);
      doc["lampu2_d14"] = digitalRead(lampu2);

      String json;
      serializeJson(doc, json);

      int httpCode = http.POST(json);
      Serial.printf("Kirim: %s -> %d\n", json.c_str(), httpCode);
      Serial.println(http.getString());
      http.end();
    }
    lastSend = millis();
  }

  delay(1000);
}