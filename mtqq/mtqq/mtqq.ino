#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>

const char* ssid = "AKUT SB";
const char* password = "samganteng#77";
const char* mqtt_server1 = "47.254.170.208";
const char* mqtt_server2 = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_user = "";
const char* mqtt_password = "";
const char* topic_publish = "binus/garden/sensor/data";
const char* topic_subscribe = "binus/garden/pump/cmd";

#define SDA_PIN 32
#define SCL_PIN 27
#define DHTPIN 4
#define DHTTYPE DHT11
#define ONE_WIRE_PIN 26
const unsigned long INTERVAL_DS18B20 = 1000;

const int soilPin = 34, waterPin = 15, pumpPin = 23, lampu1 = 25, lampu2 = 14;
const int ldrPin = 35;
const int ledCahaya = 2;
const int buzzerPin = 13;
#define BUZZER_PASIF true
#define BUZZER_AKTIF_HIGH true
#define BUZZER_NYALA_TERUS false
#define ALARM_BUTUH_KEDUANYA false
const int BUZZER_FREQ = 2700;
const unsigned long BEEP_ON = 300, BEEP_OFF = 200;

const int SOIL_DRY = 3050, SOIL_WET = 1450, BATAS_KERING = 35;
const long DURASI_SIRAM = 8000, JEDA_SIRAM = 7200000;
#define FLOAT_HABIS LOW
#define RELAY_LOW true

const unsigned long INTERVAL_SENSOR = 500;
const unsigned long INTERVAL_PUBLISH = 10000;
const unsigned long INTERVAL_MQTT_RETRY = 5000;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
DHT dht(DHTPIN, DHTTYPE);
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature ds18b20(&oneWire);
WiFiClient espClient1, espClient2;
PubSubClient client1(espClient1), client2(espClient2);

unsigned long lastSend = 0, lastSiram = 0, lastWiFiCheck = 0, lastSensor = 0, lastMqttRetry1 = 0, lastMqttRetry2 = 0;
bool sedangSiram = false; unsigned long mulaiSiram = 0;
String pumpCommand = "auto"; float suhuTanah = NAN; bool dsValid = false;
unsigned long lastDsRequest = 0; bool alarmAktif = false; bool buzzerState = false; unsigned long beepTimer = 0;

void setPump(bool nyala) { digitalWrite(pumpPin, RELAY_LOW ? (nyala ? LOW : HIGH) : (nyala ? HIGH : LOW)); }
void setBuzzer(bool nyala) {
  if (nyala == buzzerState) return; buzzerState = nyala;
  if (BUZZER_PASIF) { if (nyala) tone(buzzerPin, BUZZER_FREQ); else noTone(buzzerPin); }
  else { digitalWrite(buzzerPin, BUZZER_AKTIF_HIGH ? (nyala ? HIGH : LOW) : (nyala ? LOW : HIGH)); }
}
void updateBuzzer() {
  unsigned long now = millis(); if (!alarmAktif) { setBuzzer(false); return; }
  if (BUZZER_NYALA_TERUS) { setBuzzer(true); return; }
  if (buzzerState) { if (now - beepTimer >= BEEP_ON) { setBuzzer(false); beepTimer = now; } }
  else { if (now - beepTimer >= BEEP_OFF) { setBuzzer(true); beepTimer = now; } }
}
void updateDS18B20() {
  unsigned long now = millis(); if (now - lastDsRequest < INTERVAL_DS18B20) return;
  if (lastDsRequest != 0) {
    float t = ds18b20.getTempCByIndex(0);
    if (t == DEVICE_DISCONNECTED_C || t == 85.0f || t < -55.0f || t > 125.0f) dsValid = false;
    else { suhuTanah = t; dsValid = true; }
  }
  ds18b20.requestTemperatures(); lastDsRequest = now;
}
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = ""; for (unsigned int i = 0; i < length; i++) message += (char)payload[i]; message.trim();
  if (message == "auto" || message == "pump_on" || message == "pump_off") pumpCommand = message;
}
void reconnectAll() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!client1.connected() && millis() - lastMqttRetry1 > INTERVAL_MQTT_RETRY) {
    lastMqttRetry1 = millis(); String clientId = "ESP32-VPS-" + String(random(0xffff), HEX);
    if (client1.connect(clientId.c_str(), mqtt_user, mqtt_password)) client1.subscribe(topic_subscribe);
  }
  if (!client2.connected() && millis() - lastMqttRetry2 > INTERVAL_MQTT_RETRY) {
    lastMqttRetry2 = millis(); String clientId = "ESP32-HIVE-" + String(random(0xffff), HEX);
    if (client2.connect(clientId.c_str())) client2.subscribe(topic_subscribe);
  }
}
void ensureWiFi(){ if (WiFi.status() != WL_CONNECTED) { WiFi.disconnect(true); delay(500); WiFi.begin(ssid, password); WiFi.setSleep(false); } }
int bacaSoil() { long t = 0; for (int i = 0; i < 10; i++) { t += analogRead(soilPin); delay(10); } return t / 10; }

void setup() {
  Serial.begin(115200); delay(1000);
  Serial.println("\n\n========== BINUS GARDEN START ==========");
  pinMode(pumpPin, OUTPUT); pinMode(lampu1, OUTPUT); pinMode(lampu2, OUTPUT);
  pinMode(waterPin, INPUT_PULLUP); pinMode(buzzerPin, OUTPUT);
  pinMode(ldrPin, INPUT);
  pinMode(ledCahaya, OUTPUT);
  buzzerState = true; setBuzzer(false);
  setPump(false); digitalWrite(lampu1, LOW); digitalWrite(lampu2, LOW); digitalWrite(ledCahaya, LOW);
  Wire.begin(SDA_PIN, SCL_PIN); display.begin(SSD1306_SWITCHCAPVCC, 0x3C); dht.begin();
  ds18b20.begin(); ds18b20.setResolution(12); ds18b20.setWaitForConversion(false);
  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.begin(ssid, password);
  client1.setServer(mqtt_server1, mqtt_port); client1.setCallback(mqttCallback); client1.setBufferSize(1024);
  client2.setServer(mqtt_server2, mqtt_port); client2.setCallback(mqttCallback); client2.setBufferSize(1024);
  lastSiram = millis() - JEDA_SIRAM;
}

void loop() {
  if (millis() - lastWiFiCheck > 15000) { lastWiFiCheck = millis(); ensureWiFi(); }
  reconnectAll(); client1.loop(); client2.loop(); updateBuzzer(); updateDS18B20();
  if (millis() - lastSensor < INTERVAL_SENSOR) return; lastSensor = millis();

  int raw = bacaSoil();
  int persen = constrain(map(raw, SOIL_DRY, SOIL_WET, 0, 100), 0, 100);
  int wRaw = digitalRead(waterPin); bool habis = (wRaw == FLOAT_HABIS);
  float hum = dht.readHumidity(); float temp = dht.readTemperature(); bool valid = !isnan(hum) && !isnan(temp);

  long total = 0; for(int i=0;i<20;i++){ total+=analogRead(ldrPin); delay(10); }
  int ldrRaw = total/20;
  int cahayaPersen = constrain(map(ldrRaw, 0, 4095, 0, 100), 0, 100);
  bool gelap = ldrRaw < 1000;
  digitalWrite(ledCahaya, gelap ? HIGH : LOW);

  bool pompaOn = false; unsigned long now = millis(); String stat;
  if (pumpCommand == "pump_on") { setPump(true); pompaOn = true; stat = "MANUAL ON"; sedangSiram = false; }
  else if (pumpCommand == "pump_off") { setPump(false); stat = "MANUAL OFF"; sedangSiram = false; }
  else {
    if (sedangSiram) {
      if (now - mulaiSiram >= DURASI_SIRAM) { setPump(false); sedangSiram = false; lastSiram = now; stat = "SELESAI"; }
      else { setPump(true); pompaOn = true; stat = "AUTO SIRAM"; }
    } else {
      if (habis) { stat = "AIR HABIS"; setPump(false); }
      else if (persen < BATAS_KERING && now - lastSiram >= JEDA_SIRAM) { setPump(true); sedangSiram = true; mulaiSiram = now; pompaOn = true; stat = "MULAI AUTO"; }
      else { stat = persen < BATAS_KERING ? "TUNGGU" : "LEMBAB"; setPump(false); }
    }
  }
  digitalWrite(lampu1, pompaOn ? HIGH : LOW); digitalWrite(lampu2, habis ? HIGH : LOW);
  bool kering = persen < BATAS_KERING; alarmAktif = ALARM_BUTUH_KEDUANYA ? (habis && kering) : (habis || kering);

  display.clearDisplay(); display.setCursor(0,0); display.setTextColor(1);
  display.printf("M1:%s M2:%s\n", client1.connected() ? "ON" : "OFF", client2.connected() ? "ON" : "OFF");
  display.printf("Soil %d%% %s\n", persen, stat.c_str());
  display.printf("Cah %d%% %s\n", cahayaPersen, gelap ? "GELAP" : "TERANG");
  display.printf("LED D2:%s R:%d\n", digitalRead(ledCahaya)?"ON":"OFF", ldrRaw);
  display.display();

  // ===== SERIAL MONITOR BARU YANG RAPI =====
  if (millis() - lastSend > INTERVAL_PUBLISH) {
    lastSend = millis();
    
    Serial.println("========================================");
    Serial.printf(" WiFi : %s | %s\n", WiFi.status()==WL_CONNECTED?"CONNECTED":"DISCONNECT", ssid);
    Serial.printf(" MQTT1: %s (VPS) | MQTT2: %s (HiveMQ)\n", client1.connected()?"ON":"OFF", client2.connected()?"ON":"OFF");
    Serial.println("----------------------------------------");
    Serial.printf(" Soil  : %d%% (RAW %d) -> %s\n", persen, raw, stat.c_str());
    Serial.printf(" Air   : %s | Pompa : %s (%s)\n", habis?"HABIS":"OK", pompaOn?"ON":"OFF", pumpCommand.c_str());
    if(valid) Serial.printf(" DHT11 : %.1f C | %.1f %%\n", temp, hum);
    else Serial.println(" DHT11 : ERROR");
    if(dsValid) Serial.printf(" Tanah : %.1f C\n", suhuTanah);
    else Serial.println(" Tanah : n/a");
    Serial.println("----------------------------------------");
    Serial.printf(" LDR   : RAW %4d | %3d%% => %s\n", ldrRaw, cahayaPersen, gelap?"GELAP":"TERANG");
    Serial.printf(" LED D2: %s (%s)\n", digitalRead(ledCahaya)?"NYALA":"MATI", gelap?"karena gelap":"karena terang");
    Serial.println("========================================\n");

    if (WiFi.status() != WL_CONNECTED) return;
    JsonDocument doc;
    doc["soil_percent"] = persen; doc["soil_raw"] = raw;
    doc["soil_temperature"] = dsValid ? suhuTanah : 0;
    doc["temperature"] = valid ? temp : 0; doc["humidity"] = valid ? hum : 0;
    doc["water_level_status"] = habis ? "EMPTY" : "OK"; doc["is_water_empty"] = habis;
    doc["pump_status"] = pompaOn ? "ON" : "OFF"; doc["pump_command"] = pumpCommand;
    doc["light_raw"] = ldrRaw; doc["light_percent"] = cahayaPersen; doc["is_dark"] = gelap;
    doc["led_d2"] = digitalRead(ledCahaya) ? "ON" : "OFF";
    String payload; serializeJson(doc, payload);
    if (client1.connected()) client1.publish(topic_publish, payload.c_str());
    if (client2.connected()) client2.publish(topic_publish, payload.c_str());
  }
}