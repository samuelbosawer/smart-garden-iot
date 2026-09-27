#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* ssid = "Gas";
const char* password = "SamBos777";
const char* serverName = "https://iotbinus.projectbos.web.id/api/sensor";
const char* apiKey = "binus-iot-2025";

#define SDA_PIN 32
#define SCL_PIN 27
#define OLED_ADDR 0x3C
const int soilPin = 34;
const int waterPin = 33;
const int relayPin = 23;
const int lampu1 = 25;
const int lampu2 = 14;
const int buzzerPin = 13;
const bool RELAY_ACTIVE_LOW = true;

#define SOIL_KERING_UDARA 2900
#define SOIL_BASAH_AIR 1350
#define WATER_THRESHOLD 30
#define WATER_PUTUS 20

Adafruit_SSD1306 display(128, 64, &Wire, -1);
unsigned long lastSend = 0;
unsigned long lastPageSwitch = 0;
bool showPage2 = false;

String stWiFi = "INIT";
String stAPI = "INIT";
String stSoil = "OK";
String stWater = "OK";
bool pompaNyala = false; // FIX KAKA

int lastSoilRaw = -1;
int lastWaterRaw = -1;
unsigned long lastSoilChange = 0;
unsigned long lastWaterChange = 0;

void nyalakanPompa(bool nyala) {
  pompaNyala = nyala;
  if (nyala) {
    pinMode(relayPin, OUTPUT);
    if (RELAY_ACTIVE_LOW) digitalWrite(relayPin, LOW);
    else digitalWrite(relayPin, HIGH);
  } else {
    pinMode(relayPin, INPUT);
  }
}

int bacaSoilMedian() {
  int bacaan[10];
  for(int i=0; i<10; i++){ bacaan[i] = analogRead(soilPin); delay(80); }
  for(int i=0; i<9; i++) for(int j=i+1; j<10; j++) if(bacaan[i] > bacaan[j]){ int t=bacaan[i]; bacaan[i]=bacaan[j]; bacaan[j]=t; }
  return bacaan[5];
}

void setup() {
  Serial.begin(115200);
  pinMode(relayPin, INPUT);
  pinMode(lampu1, OUTPUT);
  pinMode(lampu2, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  nyalakanPompa(false);
  Wire.begin(SDA_PIN, SCL_PIN);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(1);
  display.setCursor(0,0);
  display.println("SISTEM 60CM START");
  display.display();
  WiFi.begin(ssid, password);
  int retry=0; while(WiFi.status()!=WL_CONNECTED && retry<20){ delay(500); Serial.print("."); retry++; }
  stWiFi = (WiFi.status()==WL_CONNECTED)? "OK" : "GAGAL";
  lastSoilChange = millis();
  lastWaterChange = millis();
}

void loop() {
  int rawSoil = bacaSoilMedian();
  long tot=0; for(int i=0;i<20;i++){ tot+=analogRead(waterPin); delay(5); }
  int rawWater = tot/20;

  if(rawSoil<=5 || rawSoil>=4090) stSoil="LEPAS";
  else {
    if(abs(rawSoil-lastSoilRaw)>15){ lastSoilChange=millis(); lastSoilRaw=rawSoil; }
    stSoil = (millis()-lastSoilChange>30000)? "STUCK" : "OK";
  }
  if(rawWater>=4090) stWater="LEPAS";
  else {
    if(abs(rawWater-lastWaterRaw)>10){ lastWaterChange=millis(); lastWaterRaw=rawWater; }
    stWater = (millis()-lastWaterChange>30000)? "STUCK" : "OK";
  }
  stWiFi = (WiFi.status()!=WL_CONNECTED)? "PUTUS" : "OK";

  int persenSoil = map(rawSoil, SOIL_KERING_UDARA, SOIL_BASAH_AIR, 0, 100);
  persenSoil = constrain(persenSoil, 0, 100);

  bool kabelPutus = rawWater > WATER_PUTUS;
  bool airHabis = (rawWater <= WATER_THRESHOLD) || kabelPutus;
  bool tanahKering = persenSoil < 30;

  String statusPompa = "OFF";
  String statusAkhir = "AMAN";
  if(airHabis){
    nyalakanPompa(false); digitalWrite(lampu1,LOW); digitalWrite(lampu2,HIGH); statusPompa="OFF"; statusAkhir="AIR HABIS";
    tone(buzzerPin,2500,200);
  } else if(tanahKering){
    nyalakanPompa(true); digitalWrite(lampu1,HIGH); digitalWrite(lampu2,LOW); statusPompa="ON"; statusAkhir="SIRAM ON";
  } else {
    nyalakanPompa(false); digitalWrite(lampu1,LOW); digitalWrite(lampu2,LOW); statusPompa="OFF"; statusAkhir="AMAN";
    noTone(buzzerPin);
  }

  if(millis()-lastPageSwitch>3000){ showPage2=!showPage2; lastPageSwitch=millis(); }

  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(1);
  if(!showPage2){
    display.printf("LEMBAB:%d%% %s\n", persenSoil, stSoil.c_str());
    display.printf("AIR:%d %s\n", rawWater, airHabis?"HABIS":"ADA");
    display.printf("POMPA:%s\n", statusPompa.c_str());
    display.printf("WIFI:%s\n", stWiFi.c_str());
    display.printf("API:%s\n", stAPI.c_str());
    display.printf(">>%s\n", statusAkhir.c_str());
  } else {
    display.println("-- DIAGNOSA --");
    display.printf("SoilRaw:%d\n", rawSoil);
    display.printf("WaterRaw:%d\n", rawWater);
    display.printf("L1:%s L2:%s\n", digitalRead(lampu1)?"ON":"OFF", digitalRead(lampu2)?"ON":"OFF");
    display.printf("Rly:D23 %s\n", pompaNyala?"ON":"OFF");
    display.printf("IP:%s\n", WiFi.localIP().toString().c_str());
  }
  display.display();

  if(millis()-lastSend>10000){
    if(WiFi.status()==WL_CONNECTED){
      HTTPClient http; http.begin(serverName);
      http.addHeader("Content-Type","application/json");
      http.addHeader("x-api-key", apiKey);
      DynamicJsonDocument doc(768);
      doc["soil_percent"]=persenSoil; doc["soil_raw"]=rawSoil; doc["water_raw"]=rawWater;
      doc["is_water_empty"]=airHabis; doc["is_soil_dry"]=tanahKering;
      doc["relay_d23"]=pompaNyala; doc["pump_status"]=(tanahKering &&!airHabis);
      String json; serializeJson(doc,json);
      int code=http.POST(json);
      stAPI = (code>=200 && code<300)? "OK "+String(code) : "FAIL "+String(code);
      http.end();
    } else { stAPI="SKIP"; WiFi.reconnect(); }
    lastSend=millis();
    Serial.printf("Soil:%d%% (%d) Water:%d %s Pompa:%s WiFi:%s API:%s\n", persenSoil, rawSoil, rawWater, airHabis?"HABIS":"ADA", statusPompa.c_str(), stWiFi.c_str(), stAPI.c_str());
  }
  delay(200);
}
