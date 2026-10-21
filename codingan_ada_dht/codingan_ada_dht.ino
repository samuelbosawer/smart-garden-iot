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
const char* andrewSensorURL = "https://smart-garden-andrew.vercel.app/api/sensor";
const char* andrewPumpStatusURL = "https://smart-garden-andrew.vercel.app/api/pump/status";

#define SDA_PIN 32
#define SCL_PIN 27
#define DHTPIN 4
#define DHTTYPE DHT11
const int soilPin = 34, waterPin = 15, pumpPin = 23, lampu1 = 25, lampu2 = 14;

const int SOIL_DRY = 3050, SOIL_WET = 1450, BATAS_KERING = 35;
const long DURASI_SIRAM = 8000, JEDA_SIRAM = 7200000;
#define FLOAT_HABIS LOW
#define RELAY_LOW true

Adafruit_SSD1306 display(128, 64, &Wire, -1);
DHT dht(DHTPIN, DHTTYPE);
unsigned long lastSend=0, lastSiram=0, lastPumpCheck=0, lastWiFiCheck=0;
bool sedangSiram=false; unsigned long mulaiSiram=0;
String pumpCommand="auto";

void setPump(bool nyala){ digitalWrite(pumpPin, RELAY_LOW ? (nyala?LOW:HIGH) : (nyala?HIGH:LOW)); }
void ensureWiFi(){
  if(WiFi.status()==WL_CONNECTED) return;
  WiFi.disconnect(true); delay(500); WiFi.begin(ssid,password); WiFi.setSleep(false);
  int c=0; while(WiFi.status()!=WL_CONNECTED && c<20){ delay(500); c++; }
}
bool postData(const char* url, JsonDocument& doc, const char* key=NULL){
  if(WiFi.status()!=WL_CONNECTED) return false;
  WiFiClientSecure client; client.setInsecure(); client.setTimeout(5000);
  HTTPClient http; http.setTimeout(8000);
  if(!http.begin(client, url)) return false;
  http.addHeader("Content-Type","application/json");
  if(key) http.addHeader("x-api-key", key);
  String j; serializeJson(doc,j);
  Serial.printf("POST %s\n%s\n",url,j.c_str());
  int code=http.POST(j); Serial.printf("-> %d %s\n",code,http.getString().c_str());
  http.end(); delay(600); return code>=200 && code<300;
}
bool postAndrew(JsonDocument& doc){
  if(WiFi.status()!=WL_CONNECTED) return false;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.begin(client, andrewSensorURL);
  http.addHeader("Content-Type","application/json");
  String j; serializeJson(doc,j);
  int code=http.POST(j); String p=http.getString();
  Serial.printf("[ANDREW %d] %s\n",code,p.c_str());
  if(code>=200 && code<300){ JsonDocument r; if(!deserializeJson(r,p) && r["pump_command"].is<String>()) pumpCommand=r["pump_command"].as<String>(); }
  http.end(); delay(600); return code>=200 && code<300;
}
void getPumpStatus(){
  if(WiFi.status()!=WL_CONNECTED) return;
  WiFiClientSecure c; c.setInsecure(); HTTPClient http;
  if(!http.begin(c, andrewPumpStatusURL)) return;
  if(http.GET()==200){ String p=http.getString(); JsonDocument r; if(!deserializeJson(r,p)){ if(r["pump_command"].is<String>()) pumpCommand=r["pump_command"].as<String>(); else if(r["command"].is<String>()) pumpCommand=r["command"].as<String>(); } }
  http.end();
}
int bacaSoil(){ long t=0; for(int i=0;i<10;i++){ t+=analogRead(soilPin); delay(10);} return t/10; }

void setup(){
  Serial.begin(115200);
  pinMode(pumpPin,OUTPUT); pinMode(lampu1,OUTPUT); pinMode(lampu2,OUTPUT); pinMode(waterPin,INPUT_PULLUP);
  setPump(false); digitalWrite(lampu1,LOW); digitalWrite(lampu2,LOW);
  Wire.begin(SDA_PIN,SCL_PIN); display.begin(SSD1306_SWITCHCAPVCC,0x3C); dht.begin();
  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.begin(ssid,password);
  while(WiFi.status()!=WL_CONNECTED){ delay(500); Serial.print("."); }
  Serial.println("\nWiFi OK - FULL DATA SAME"); lastSiram=millis()-JEDA_SIRAM;
}

void loop(){
  if(millis()-lastWiFiCheck>15000){ lastWiFiCheck=millis(); ensureWiFi(); }
  int raw=bacaSoil(); int persen=constrain(map(raw,SOIL_DRY,SOIL_WET,0,100),0,100);
  int wRaw=digitalRead(waterPin); bool habis=(wRaw==FLOAT_HABIS);
  float hum=dht.readHumidity(); float temp=dht.readTemperature();
  bool valid=!isnan(hum)&&!isnan(temp);
  if(millis()-lastPumpCheck>10000){ lastPumpCheck=millis(); getPumpStatus(); }

  bool pompaOn=false; unsigned long now=millis(); String stat;
  if(pumpCommand=="pump_on"){ setPump(true); pompaOn=true; stat="MANUAL ON"; sedangSiram=false; }
  else if(pumpCommand=="pump_off"){ setPump(false); stat="MANUAL OFF"; sedangSiram=false; }
  else{
    if(sedangSiram){ if(now-mulaiSiram>=DURASI_SIRAM){ setPump(false); sedangSiram=false; lastSiram=now; stat="SELESAI"; } else { setPump(true); pompaOn=true; stat="AUTO SIRAM"; } }
    else{ if(habis){ stat="AIR HABIS"; setPump(false);} else if(persen<BATAS_KERING && now-lastSiram>=JEDA_SIRAM){ setPump(true); sedangSiram=true; mulaiSiram=now; pompaOn=true; stat="MULAI AUTO"; } else { stat= persen<BATAS_KERING ? "TUNGGU" : "LEMBAB AMAN"; setPump(false); } }
  }
  digitalWrite(lampu1,pompaOn?HIGH:LOW); digitalWrite(lampu2,habis?HIGH:LOW);

  display.clearDisplay(); display.setCursor(0,0); display.setTextColor(1);
  display.println("3 API SAME DATA"); display.printf("Soil %d%% %s\n",persen,pumpCommand.c_str());
  if(valid) display.printf("T%.1f H%.0f\n",temp,hum); display.printf("%s %s\n",habis?"HABIS":"OK",stat.c_str()); display.display();

  if(millis()-lastSend>25000){
    lastSend=millis(); if(WiFi.status()!=WL_CONNECTED){ ensureWiFi(); return; }
    static int g=0; g++; int mode=g%3;

    // DATA MASTER - SAMA UNTUK SEMUA API
    JsonDocument master;
    master["device_id"]="ESP32-BAMER-01";
    master["tanaman"]="bawang_merah";
    master["soil_percent"]=persen;
    master["soil_raw"]=raw;
    master["temperature"]= valid?temp:0;
    master["humidity"]= valid?hum:0;
    master["water_raw"]=wRaw;
    master["water_level_status"]= habis?"EMPTY":"OK";
    master["is_water_empty"]=habis;
    master["is_soil_dry"]= persen<BATAS_KERING;
    master["pump_status"]= pompaOn?"ON":"OFF";
    master["relay_d23"]=pompaOn;
    master["pump_command"]=pumpCommand;
    master["pump_command_local"]=pumpCommand;

    if(mode==0){ Serial.println("== KIRIM BINUS =="); postData(serverName, master, apiKey); }
    else if(mode==1){ Serial.println("== KIRIM DASHBOARD LAMA =="); postData(sensorURL, master); delay(1000); postData(irrigationURL, master); }
    else { Serial.println("== KIRIM ANDREW =="); postAndrew(master); }
  }
  delay(500);
}