#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_BMP280.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include "LittleFS.h"

#define MQTT_CLIENT_ID "GateAssistant"

#define BUTTON_1 4
#define BUTTON_2 5
#define BUTTON_3 6
#define BUTTON_4 7
#define SDA      17
#define SCL      18
#define RGB_LED  38

#define BMP_ADDR    0x76
#define BH1750_ADDR 0x23

#define altitude  227.0 // in meters above sea level

const char *mqtt_topic_prefix      = "gateassistant/sw";
const char *mqtt_topic_light       = "gateassistant/light";
const char *mqtt_topic_temperature = "gateassistant/temperature";
const char *mqtt_topic_pressure    = "gateassistant/pressure";
const char *mqtt_topic_lock        = "gateassistant/lock";

float lux = 0.0;
float hpa_local = 0.0;
float hpa = 0.0;
float temp = 0.0;


// Unlock time: 5s
bool locked = true;
unsigned long unlock_timestamp;
#define UNLOCK_TIME 5*1000L

// measurement frequency (every 30s):
#define PERIOD 30*1000L
unsigned long target_time = 0L;


WiFiClient espClient;
WebServer server(80);
PubSubClient mqttClient(espClient);
BH1750 lightMeter;   // Light sensor
Adafruit_BMP280 bmp; // Temperature and pressure sensor
Adafruit_NeoPixel strip(1, RGB_LED, NEO_RGB + NEO_KHZ800);

struct Config {
  String wifi_ssid;
  String wifi_pass;
  String mqtt_server;
  String mqtt_port;
  String mqtt_topic;
  String net_addr;
  String net_gw;
  String net_subnet;
  String net_dns;
  String mqtt_user;
  String mqtt_pass;
};

Config config;

bool led1_state = false;
bool led2_state = false;
bool led3_state = false;
bool led4_state = false;

unsigned long startTime;


int getPin(int sw) {
  switch(sw) {
    case 1:
      return BUTTON_1;
      break;
    case 2:
      return BUTTON_2;
      break;
    case 3:
      return BUTTON_3;
      break;
    case 4:
      return BUTTON_4;
      break;
  }
  return 0;
}

String getTopic(int sw) {
  String topic = mqtt_topic_prefix + String(sw);
  return topic;
}

void ledColor(const String &colorName) {
  uint8_t r = 0, g = 0, b = 0;
  if (colorName.equalsIgnoreCase("red"))        { r = 255; g = 0;   b = 0;   }
  else if (colorName.equalsIgnoreCase("green")) { r = 0;   g = 255; b = 0;   }
  else if (colorName.equalsIgnoreCase("blue"))  { r = 0;   g = 0;   b = 255; }
  else if (colorName.equalsIgnoreCase("yellow")){ r = 255; g = 255; b = 0;   }
  else if (colorName.equalsIgnoreCase("cyan"))  { r = 0;   g = 255; b = 255; }
  else if (colorName.equalsIgnoreCase("magenta")){r = 255; g = 0;   b = 255; }
  else if (colorName.equalsIgnoreCase("white")) { r = 255; g = 255; b = 255; }
  else if (colorName.equalsIgnoreCase("off"))   { r = 0;   g = 0;   b = 0;   }
  strip.setPixelColor(0, strip.Color(r, g, b));
  strip.show();
}

void blink(int count=1, const String color="red", const int period=200) {
    while(count>0) {
      ledColor(color);
      delay(period);
      ledColor("off");
      count--;
    }
}

void connectBestAP() {
  // Find AP with The Best Signal
  int n = WiFi.scanNetworks();
  int bestRSSI = -999;
  String bestBSSID = "";
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  delay(100);
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == config.wifi_ssid) {
      int rssi = WiFi.RSSI(i);
      String bssidStr = WiFi.BSSIDstr(i);
      Serial.printf("Znaleziono %s (BSSID %s, RSSI %d)\n", WiFi.SSID(i).c_str(), bssidStr.c_str(), rssi);
      if (rssi > bestRSSI) {
        bestRSSI = rssi;
        bestBSSID = bssidStr;
      }
    }
  }

  IPAddress local_IP, gateway, subnet, dns;
  if (!local_IP.fromString(config.net_addr)) Serial.println("Błąd IP");
  if (!gateway.fromString(config.net_gw)) Serial.println("Błąd gateway");
  if (!subnet.fromString(config.net_subnet)) Serial.println("Błąd maski");
  if (!dns.fromString(config.net_dns)) Serial.println("Błąd DNS");
  WiFi.config(local_IP, gateway, subnet, dns);

  if (bestBSSID != "") {
    Serial.printf("Łączenie z najlepszym AP (%s, RSSI %d)\n", bestBSSID.c_str(), bestRSSI);
    uint8_t bssid[6];
    sscanf(bestBSSID.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4], &bssid[5]);
    WiFi.begin(config.wifi_ssid, config.wifi_pass, 0, bssid);
  } else {
    Serial.printf("Nie znaleziono sieci o nazwie %s\n", config.wifi_ssid.c_str());
    WiFi.begin(config.wifi_ssid.c_str(), config.wifi_pass.c_str());
  }
}


bool loadConfig() {
  if (!LittleFS.begin()) {
    Serial.println("Błąd LittleFS");
    return false;
  }

  File file = LittleFS.open("/config.json", "r");
  if (!file) {
    Serial.println("Brak pliku config.json");
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.println("Błąd parsowania JSON");
    return false;
  }

  config.wifi_ssid   = doc["wifi_ssid"].as<String>();
  config.wifi_pass   = doc["wifi_pass"].as<String>();
  config.mqtt_server = doc["mqtt_server"].as<String>();
  config.mqtt_port = doc["mqtt_port"].as<String>();
  config.mqtt_topic  = doc["mqtt_topic"].as<String>();
  config.mqtt_user  = doc["mqtt_user"].as<String>();
  config.mqtt_pass  = doc["mqtt_pass"].as<String>();
  config.net_addr  = doc["net_addr"].as<String>();
  config.net_gw  = doc["net_gw"].as<String>();
  config.net_subnet  = doc["net_subnet"].as<String>();
  config.net_dns  = doc["net_dns"].as<String>();
  
  Serial.println("Config wczytany OK");
  return true;
}

void logMessage(const String &msg) {
  const char* logFile = "/gate.log";
  const size_t MAX_LOG_SIZE = 50 * 1024; // 50 KB limit

  if (!LittleFS.begin()) {
    Serial.println("Błąd LittleFS podczas logowania");
    return;
  }

  if (LittleFS.exists(logFile)) {
    File check = LittleFS.open(logFile, "r");
    if (check && check.size() > MAX_LOG_SIZE) {
      check.close();
      Serial.println("Plik logu za duży — usuwam stary log");
      LittleFS.remove(logFile);
    } else {
      check.close();
    }
  }

  File file = LittleFS.open(logFile, FILE_APPEND);
  if (!file) {
    Serial.println("Nie można otworzyć gate.log do zapisu");
    return;
  }

  String logEntry = msg + "\n";
  file.print(logEntry);
  file.close();

  Serial.println(logEntry);
}

void handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    Serial.println("Brak pliku /index.html w LittleFS!");
    server.send(404, "text/plain", "Brak pliku index.html");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}


void shortPress(int sw) {
  if(!locked) {
    logMessage(String("Otwieranie: ") + sw);
    digitalWrite(getPin(sw), LOW);
    ledColor("magenta");
    delay(1000);
    digitalWrite(getPin(sw), HIGH);
    ledColor("off");
    mqttClient.publish(getTopic(sw).c_str(), "HIGH");
  } else {
    mqttClient.publish(getTopic(sw).c_str(), "HIGH");
  }
}


void handleToggle() {
  if (server.hasArg("button")) {
    int button = server.arg("button").toInt();
    Serial.printf("* Wciśnięto: %i\n", button);
    locked = false;
    shortPress(button);
    locked = true;
  }
  server.send(200, "text/plain", "OK");
}



void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message = message + (char)payload[i];
  }
  if(message == "UNLOCK") {
    locked = false;
    unlock_timestamp = millis();
  } else if(message == "LOCK") {
    locked = true;
  } else {
    int sw_number = int(topic[strlen(topic)-1]) - '0';
    if(message == "LOW") digitalWrite(getPin(sw_number), LOW);
    if(message == "HIGH") digitalWrite(getPin(sw_number), HIGH);
    if(message == "PRESS") shortPress(sw_number);
  }
}

String getStatsJson() {
  JsonDocument doc;
  unsigned long uptimeSec = (millis() - startTime) / 1000;
  doc["uptime_s"]  = uptimeSec;
  doc["heap_free"] = ESP.getFreeHeap();
  doc["wifi_rssi"] = WiFi.isConnected() ? WiFi.RSSI() : 0;
  doc["wifi_ip"]   = WiFi.isConnected() ? WiFi.localIP().toString() : "0.0.0.0";
  String output;
  serializeJson(doc, output);
  return output;
}

void httpTemp() {
  server.send(200, "text/plain", String(temp));
}

void httpLight() {
  server.send(200, "text/plain", String(lux));
}

void httpPressure() {
  server.send(200, "text/plain", String(hpa));
}

void httpStats() {
  server.send(200, "text/plain", getStatsJson());
}

void handleLogDownload() {
  const char* logFile = "/gate.log";
  if (!LittleFS.exists(logFile)) {
    server.send(404, "text/plain", "Brak pliku logu");
    return;
  }

  File file = LittleFS.open(logFile, "r");
  if (!file) {
    server.send(500, "text/plain", "Nie można otworzyć logu");
    return;
  }

  server.streamFile(file, "text/plain");
  file.close();
}

void handleNotFound() { 
   String message = "404 ...sorry";
   server.send(404, "text/plain", message);
}


void reconnectMqtt() {
    while (!mqttClient.connected()) {
        Serial.println("Próba połączenia z MQTT...");
        blink(1, "yellow", 500); 
        if (mqttClient.connect(MQTT_CLIENT_ID, config.mqtt_user.c_str(), config.mqtt_pass.c_str())) {
            Serial.println("Połączono z MQTT.");
            mqttClient.publish(mqtt_topic_lock, locked ? "LOCK" : "UNLOCK");
            mqttClient.subscribe(mqtt_topic_lock);
            for(int i=1; i<5; i++) {
                mqttClient.subscribe(getTopic(i).c_str());
            }
            blink(2, "green", 100);
        } else {
            Serial.print("Nieudana próba, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" spróbuję ponownie za 5s.");
            delay(5000);
        }
    }
}



void setup() {
  for (int i=1; i<5; i++) {
    pinMode(getPin(i), INPUT_PULLUP);
    pinMode(getPin(i), OUTPUT);
    digitalWrite(getPin(i), HIGH);
  }

  startTime = millis();

  loadConfig();

  Serial.begin(115200);
  Wire.begin(SDA, SCL);
  Serial.println("I2C start!");

  // i2c
  lightMeter.begin();
  if(!bmp.begin(BMP_ADDR)) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while(1);
  }
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */

  connectBestAP();
  Serial.print("Łączenie z WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nPołączono!");
  Serial.print("Adres IP: ");
  Serial.println(WiFi.localIP());

  LittleFS.begin();
  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.on("/temp/", httpTemp);
  server.on("/light/", httpLight);
  server.on("/pressure/", httpPressure);
  server.on("/stats/", httpStats);
  server.on("/log", handleLogDownload);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Serwer wystartował!");

  mqttClient.setServer(config.mqtt_server.c_str(), config.mqtt_port.toInt());
  mqttClient.setCallback(callback);

  unsigned long startAttemptTime = millis();
  while (!mqttClient.connected() && millis() - startAttemptTime < 10000) {
    if (mqttClient.connect(MQTT_CLIENT_ID, config.mqtt_user.c_str(), config.mqtt_pass.c_str())) break;
    delay(500);
  }

  // Default locked
  mqttClient.publish(mqtt_topic_lock, "LOCK");
  mqttClient.subscribe(mqtt_topic_lock);
  for(int i=1; i<5; i++) {
    mqttClient.publish(getTopic(i).c_str(), "HIGH");
    mqttClient.subscribe(getTopic(i).c_str());
  }

}



void loop() {

  if (!mqttClient.connected()) {
    Serial.print("MQTT Reconnecting");
    reconnectMqtt(); 
  }

  mqttClient.loop();
  server.handleClient();

  static unsigned long lastHourlyLog = 0;
  if (millis() - target_time >= PERIOD) {
    target_time += PERIOD;
    lux = lightMeter.readLightLevel();
    temp = bmp.readTemperature();
    hpa_local = bmp.readPressure() / 100.0;
    hpa = hpa_local * pow(1.0 - (0.0065 * altitude) / (temp + 273.15 + 0.0065 * altitude), -5.257);
    Serial.printf("Light: %f Lux, Temp: %f C, Pressure: %f hPa (QNH)\n", lux, temp, hpa);
    mqttClient.publish(mqtt_topic_pressure, String(hpa, 2).c_str());
    mqttClient.publish(mqtt_topic_light, String(lux, 2).c_str());
    mqttClient.publish(mqtt_topic_temperature, String(temp, 2).c_str());
    blink();
    // logowanie co godzinę
    if (millis() - lastHourlyLog >= 3600000UL) { // 1 godzina
        lastHourlyLog = millis();
        logMessage(String("Light: ") + lux + " Lux, Temp: " + temp + " C, Pressure: " + hpa + " hPa (QNH)");
        blink(3, "magenta");
    }
  }

  // Automatic Lock
  if(!(locked) && millis() - unlock_timestamp >= UNLOCK_TIME) {
    locked = true;
    mqttClient.publish(mqtt_topic_lock, "LOCK");
  }
  
  yield();

}
