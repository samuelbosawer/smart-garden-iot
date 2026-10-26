#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ================= WIFI =================
const char* ssid = "AKUT SB";
const char* password = "samganteng#77";

// ================= MQTT (3 BROKER) =================
const int JUMLAH_BROKER = 3;
const char* brokerHost[JUMLAH_BROKER] = {
  "101.50.1.12",       // VPS 1
  "47.254.170.208",    // VPS 2
  "broker.hivemq.com"  // PUBLIC FALLBACK
};
const char* brokerNama[JUMLAH_BROKER] = {"BARU", "LAMA", "HIVE"};
// Broker publik bisa dipakai siapa saja, jadi perintah pompa dari sana diabaikan.
// Data sensor tetap dikirim ke semua broker.
const bool brokerTerimaPerintah[JUMLAH_BROKER] = {true, true, false};

const int mqtt_port = 1883;
const char* mqtt_user = "";
const char* mqtt_password = "";
const char* topic_publish = "binus/garden/sensor/data";
const char* topic_subscribe = "binus/garden/pump/cmd";

const unsigned long RETRY_MIN = 5000;   // jeda coba ulang awal
const unsigned long RETRY_MAX = 60000;  // jeda maksimum kalau broker terus gagal

// ================= PIN =================
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

// ================= BUZZER =================
#define BUZZER_PASIF true
#define BUZZER_AKTIF_HIGH true
#define BUZZER_NYALA_TERUS false
const int BUZZER_FREQ = 2700;
const unsigned long BEEP_ON = 300, BEEP_OFF = 200;

// ================= KALIBRASI & BATAS (BAWANG) =================
const int SOIL_DRY = 3050, SOIL_WET = 1450;  // nilai RAW sensor saat kering & basah
const int BATAS_KERING = 55;        // mulai siram jika kelembapan < 55%
const int BATAS_BASAH = 75;         // berhenti siram lebih awal jika sudah >= 75%
const int BATAS_ALARM_KERING = 40;  // buzzer bunyi jika tanah sangat kering
const float SUHU_TANAH_MAX = 32.0;  // buzzer bunyi jika suhu tanah terlalu panas

const unsigned long DURASI_SIRAM = 8000;      // lama pompa menyala per siklus otomatis
const unsigned long JEDA_SIRAM = 1800000;     // 30 menit, beri waktu air meresap
const unsigned long MAKS_MANUAL_ON = 60000;   // pompa manual mati sendiri setelah 60 detik

#define FLOAT_HABIS LOW
#define RELAY_LOW true

const unsigned long INTERVAL_SENSOR = 500;
const unsigned long INTERVAL_PUBLISH = 10000;
const unsigned long INTERVAL_WIFI_CHECK = 15000;

// ================= OBJEK =================
Adafruit_SSD1306 display(128, 64, &Wire, -1);
DHT dht(DHTPIN, DHTTYPE);
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature ds18b20(&oneWire);
WiFiClient netClient[JUMLAH_BROKER];
PubSubClient mqtt[JUMLAH_BROKER];

// ================= STATE =================
unsigned long lastSend = 0, lastSiram = 0, lastWiFiCheck = 0, lastSensor = 0;
unsigned long lastRetry[JUMLAH_BROKER] = {0, 0, 0};
unsigned long jedaRetry[JUMLAH_BROKER] = {RETRY_MIN, RETRY_MIN, RETRY_MIN};

bool sedangSiram = false;
unsigned long mulaiSiram = 0;
String pumpCommand = "auto";
unsigned long manualMulai = 0;

float suhuTanah = NAN;
bool dsValid = false;
unsigned long lastDsRequest = 0;

bool alarmAktif = false, buzzerState = false;
unsigned long beepTimer = 0;
bool oledOk = false;

// ================= AKTUATOR =================
void setPump(bool nyala) {
  digitalWrite(pumpPin, RELAY_LOW ? (nyala ? LOW : HIGH) : (nyala ? HIGH : LOW));
}

void setBuzzer(bool nyala) {
  if (nyala == buzzerState) return;
  buzzerState = nyala;
  if (BUZZER_PASIF) {
    if (nyala) tone(buzzerPin, BUZZER_FREQ);
    else noTone(buzzerPin);
  } else {
    digitalWrite(buzzerPin, BUZZER_AKTIF_HIGH ? (nyala ? HIGH : LOW) : (nyala ? LOW : HIGH));
  }
}

void updateBuzzer() {
  unsigned long now = millis();
  if (!alarmAktif) { setBuzzer(false); return; }
  if (BUZZER_NYALA_TERUS) { setBuzzer(true); return; }
  if (buzzerState) {
    if (now - beepTimer >= BEEP_ON) { setBuzzer(false); beepTimer = now; }
  } else {
    if (now - beepTimer >= BEEP_OFF) { setBuzzer(true); beepTimer = now; }
  }
}

// ================= SENSOR =================
void updateDS18B20() {
  unsigned long now = millis();
  if (now - lastDsRequest < INTERVAL_DS18B20) return;
  if (lastDsRequest != 0) {
    float t = ds18b20.getTempCByIndex(0);
    if (t == DEVICE_DISCONNECTED_C || t == 85.0f || t < -55.0f || t > 125.0f) dsValid = false;
    else { suhuTanah = t; dsValid = true; }
  }
  ds18b20.requestTemperatures();
  lastDsRequest = now;
}

int bacaRataRata(int pin, int sampel) {
  long t = 0;
  for (int i = 0; i < sampel; i++) { t += analogRead(pin); delay(5); }
  return t / sampel;
}

// ================= MQTT =================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  message.trim();
  if (message == "auto" || message == "pump_on" || message == "pump_off") {
    if (message == "pump_on") manualMulai = millis();
    pumpCommand = message;
    Serial.printf("Perintah pompa: %s\n", message.c_str());
  }
}

// Mencoba paling banyak satu broker per putaran loop, dengan jeda yang makin panjang
// untuk broker yang terus gagal, supaya broker mati tidak menahan loop berulang-ulang.
void reconnectBroker() {
  if (WiFi.status() != WL_CONNECTED) return;
  unsigned long now = millis();
  for (int i = 0; i < JUMLAH_BROKER; i++) {
    if (mqtt[i].connected()) { jedaRetry[i] = RETRY_MIN; continue; }
    if (now - lastRetry[i] < jedaRetry[i]) continue;

    String clientId = String("ESP32-") + brokerNama[i] + "-" + String(random(0xffff), HEX);
    bool ok = (mqtt_user[0] == '\0')
                ? mqtt[i].connect(clientId.c_str())
                : mqtt[i].connect(clientId.c_str(), mqtt_user, mqtt_password);
    lastRetry[i] = millis();

    if (ok) {
      jedaRetry[i] = RETRY_MIN;
      if (brokerTerimaPerintah[i]) mqtt[i].subscribe(topic_subscribe);
      Serial.printf("MQTT %s terhubung\n", brokerNama[i]);
    } else {
      jedaRetry[i] = min(jedaRetry[i] * 2, RETRY_MAX);
      Serial.printf("MQTT %s gagal (rc=%d), coba lagi %lu detik\n",
                    brokerNama[i], mqtt[i].state(), jedaRetry[i] / 1000);
    }
    return;
  }
}

void ensureWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi putus, menyambung ulang...");
    WiFi.reconnect();
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n========== BINUS GARDEN 3 BROKER ==========");

  pinMode(pumpPin, OUTPUT); pinMode(lampu1, OUTPUT); pinMode(lampu2, OUTPUT);
  pinMode(waterPin, INPUT_PULLUP); pinMode(buzzerPin, OUTPUT);
  pinMode(ldrPin, INPUT); pinMode(ledCahaya, OUTPUT);

  buzzerState = true; setBuzzer(false);
  setPump(false);
  digitalWrite(lampu1, LOW); digitalWrite(lampu2, LOW); digitalWrite(ledCahaya, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!oledOk) Serial.println("OLED tidak terdeteksi, tampilan dilewati");

  dht.begin();
  ds18b20.begin();
  ds18b20.setResolution(12);
  ds18b20.setWaitForConversion(false);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  for (int i = 0; i < JUMLAH_BROKER; i++) {
    mqtt[i].setClient(netClient[i]);
    mqtt[i].setServer(brokerHost[i], mqtt_port);
    mqtt[i].setCallback(mqttCallback);
    mqtt[i].setBufferSize(1024);
    mqtt[i].setSocketTimeout(5);  // default 15 detik terlalu lama kalau broker mati
  }

  lastSiram = millis() - JEDA_SIRAM;  // boleh langsung siram setelah nyala
}

// ================= LOOP =================
void loop() {
  if (millis() - lastWiFiCheck > INTERVAL_WIFI_CHECK) { lastWiFiCheck = millis(); ensureWiFi(); }
  reconnectBroker();
  for (int i = 0; i < JUMLAH_BROKER; i++) mqtt[i].loop();
  updateBuzzer();
  updateDS18B20();

  if (millis() - lastSensor < INTERVAL_SENSOR) return;
  lastSensor = millis();

  // ---- Baca sensor ----
  int raw = bacaRataRata(soilPin, 10);
  int persen = constrain(map(raw, SOIL_DRY, SOIL_WET, 0, 100), 0, 100);
  bool habis = (digitalRead(waterPin) == FLOAT_HABIS);
  float hum = dht.readHumidity();
  float temp = dht.readTemperature();
  bool valid = !isnan(hum) && !isnan(temp);

  int ldrRaw = bacaRataRata(ldrPin, 10);
  int cahayaPersen = constrain(map(ldrRaw, 0, 4095, 0, 100), 0, 100);
  bool gelap = ldrRaw < 1000;
  digitalWrite(ledCahaya, gelap ? HIGH : LOW);

  // ---- Logika pompa ----
  bool pompaOn = false;
  unsigned long now = millis();
  String stat;

  if (pumpCommand == "pump_on") {
    sedangSiram = false;
    if (habis) {
      setPump(false); pumpCommand = "auto"; lastSiram = now; stat = "AIR HABIS";
    } else if (now - manualMulai >= MAKS_MANUAL_ON) {
      setPump(false); pumpCommand = "auto"; lastSiram = now; stat = "MANUAL TIMEOUT";
    } else {
      setPump(true); pompaOn = true; stat = "MANUAL ON";
    }
  } else if (pumpCommand == "pump_off") {
    setPump(false); sedangSiram = false; stat = "MANUAL OFF";
  } else {
    if (sedangSiram) {
      if (habis || persen >= BATAS_BASAH || now - mulaiSiram >= DURASI_SIRAM) {
        setPump(false); sedangSiram = false; lastSiram = now;
        stat = habis ? "AIR HABIS" : "SELESAI";
      } else {
        setPump(true); pompaOn = true; stat = "AUTO SIRAM";
      }
    } else if (habis) {
      setPump(false); stat = "AIR HABIS";
    } else if (persen < BATAS_KERING && now - lastSiram >= JEDA_SIRAM) {
      setPump(true); sedangSiram = true; mulaiSiram = now; pompaOn = true; stat = "MULAI AUTO";
    } else {
      setPump(false); stat = persen < BATAS_KERING ? "TUNGGU" : "LEMBAB";
    }
  }

  digitalWrite(lampu1, pompaOn ? HIGH : LOW);
  digitalWrite(lampu2, habis ? HIGH : LOW);

  bool sangatKering = persen < BATAS_ALARM_KERING;
  bool tanahPanas = dsValid && suhuTanah > SUHU_TANAH_MAX;
  alarmAktif = habis || sangatKering || tanahPanas;

  // ---- OLED ----
  if (oledOk) {
    display.clearDisplay();
    display.setTextColor(1); display.setTextSize(1); display.setCursor(0, 0);
    display.printf("B:%s L:%s H:%s\n",
                   mqtt[0].connected() ? "ON" : "OFF",
                   mqtt[1].connected() ? "ON" : "OFF",
                   mqtt[2].connected() ? "ON" : "OFF");
    display.printf("Soil %d%% %s\n", persen, stat.c_str());
    if (dsValid) display.printf("Tanah %.1fC%s\n", suhuTanah, tanahPanas ? " PANAS" : "");
    else display.println("Tanah n/a");
    if (valid) display.printf("Udara %.1fC %.0f%%\n", temp, hum);
    else display.println("Udara n/a");
    display.printf("Cah %d%% %s\n", cahayaPersen, gelap ? "GELAP" : "TERANG");
    display.printf("Air %s Pompa %s\n", habis ? "HABIS" : "OK", pompaOn ? "ON" : "OFF");
    display.display();
  }

  // ---- Serial & publish ----
  if (millis() - lastSend > INTERVAL_PUBLISH) {
    lastSend = millis();
    Serial.println("========================================");
    Serial.printf(" WiFi : %s | %s\n", WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECT", ssid);
    Serial.printf(" MQTT BARU: %s | LAMA: %s | HIVE: %s\n",
                  mqtt[0].connected() ? "ON" : "OFF",
                  mqtt[1].connected() ? "ON" : "OFF",
                  mqtt[2].connected() ? "ON" : "OFF");
    Serial.println("----------------------------------------");
    Serial.printf(" Soil  : %d%% (RAW %d) -> %s\n", persen, raw, stat.c_str());
    Serial.printf(" Air   : %s | Pompa : %s (%s)\n", habis ? "HABIS" : "OK", pompaOn ? "ON" : "OFF", pumpCommand.c_str());
    if (valid) Serial.printf(" DHT11 : %.1f C | %.1f %%\n", temp, hum);
    else Serial.println(" DHT11 : ERROR");
    if (dsValid) Serial.printf(" Tanah : %.1f C\n", suhuTanah);
    else Serial.println(" Tanah : n/a");
    Serial.println("----------------------------------------");
    Serial.printf(" LDR   : RAW %4d | %3d%% => %s\n", ldrRaw, cahayaPersen, gelap ? "GELAP" : "TERANG");
    Serial.printf(" Alarm : %s\n", alarmAktif ? "AKTIF" : "-");
    Serial.println("========================================\n");

    if (WiFi.status() != WL_CONNECTED) return;

    JsonDocument doc;
    doc["soil_percent"] = persen;
    doc["soil_raw"] = raw;
    // Kirim null (bukan 0) kalau sensor error, supaya dashboard tidak menampilkan 0 °C
    if (dsValid) doc["soil_temperature"] = suhuTanah; else doc["soil_temperature"] = nullptr;
    if (valid) { doc["temperature"] = temp; doc["humidity"] = hum; }
    else { doc["temperature"] = nullptr; doc["humidity"] = nullptr; }
    doc["water_level_status"] = habis ? "EMPTY" : "OK";
    doc["is_water_empty"] = habis;
    doc["pump_status"] = pompaOn ? "ON" : "OFF";
    doc["pump_command"] = pumpCommand;
    doc["status"] = stat;
    doc["light_raw"] = ldrRaw;
    doc["light_percent"] = cahayaPersen;
    doc["is_dark"] = gelap;
    doc["led_d2"] = digitalRead(ledCahaya) ? "ON" : "OFF";
    doc["alarm"] = alarmAktif;

    String payload;
    serializeJson(doc, payload);
    for (int i = 0; i < JUMLAH_BROKER; i++) {
      if (mqtt[i].connected()) mqtt[i].publish(topic_publish, payload.c_str());
    }
  }
}