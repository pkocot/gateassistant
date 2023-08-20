#include <ESP8266WiFi.h>
#include <Pinger.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_BMP280.h>
#include <string.h>
#include <ESP8266WebServer.h>

// ------------ Configuration -----------
#define WIFI_SSID      "MYSSID"
#define WIFI_PASSWD    "***** ***"
#define WIFI_HOST      "Gate-Assist"
#define MQTT_SERVER    "ADDRESS"
#define MQTT_PORT      1883
#define MQTT_USER      "USER"
#define MQTT_PASSWD    "***** ***"
#define MQTT_CLIENT_ID "GateAssistant"

#define BMP_ADDR    0x76
#define BH1750_ADDR 0x23

#define altitude  227.0 // in meters above sea level
// --------------------------------------

#define LED D3

// measurement frequency (every 30s):
#define PERIOD 30*1000L
unsigned long target_time = 0L;

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

Pinger pinger;

WiFiClient netClient;
BH1750 lightMeter;   // Light sensor
Adafruit_BMP280 bmp; // Temperature and pressure sensor

ESP8266WebServer server(80);

PubSubClient mqttClient(netClient);

String getTopic(int sw) {
  String topic = mqtt_topic_prefix + String(sw);
  return topic;
}

int getPin(int sw) {
  switch(sw) {
    case 1:
      return D5;
      break;
    case 2:
      return D6;
      break;
    case 3:
      return D7;
      break;
    case 4:
      return D8;
      break;
  }
  return 0;
}

void shortPress(int sw) {
  if(!locked) {
    digitalWrite(getPin(sw), LOW);
    digitalWrite(LED, HIGH);
    delay(1000);
    digitalWrite(getPin(sw), HIGH);
    digitalWrite(LED, LOW);
    mqttClient.publish(getTopic(sw).c_str(), "HIGH");
  } else {
    mqttClient.publish(getTopic(sw).c_str(), "HIGH");
  }
}

void blink(int count, int period=200) {
    while(count>0) {
      digitalWrite(LED, HIGH);
      delay(period);
      digitalWrite(LED, LOW);
      count--;
    }
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

// ----- HTTP -----
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
  <head>
    <title>Gate Assistant</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.1/dist/css/bootstrap.min.css" rel="stylesheet" integrity="sha384-4bw+/aepP/YC94hEpVNVgiZdgIC5+VKNBQNGCHeKRQN+PtmoHDEXuppvnDJzQIu9" crossorigin="anonymous">
  </head>
  <body>
    <center>
      <h2>Gate Assistant</h2>
      <button type="button" class="btn btn-primary" onclick="location.href='/temp/';">Temperature</button>
      <button type="button" class="btn btn-primary" onclick="location.href='/light/';">Light</button>
      <button type="button" class="btn btn-primary" onclick="location.href='/pressure/';">Pressure</button>
      <p></p>
      <button type="button" class="btn btn-warning" onclick="location.href='/reset/';">Reset</button>
      <button type="button" class="btn btn-warning" onclick="location.href='/restart/';">Restart</button>
  </body>
</html> )rawliteral";

void mainSite() {
  server.send(200, "text/html", index_html);
}

void httpReset() {
  ESP.reset();
}

void httpRestart() {
  ESP.restart();
}

void httpTemp() {
  server.send(404, "text/plain", String(temp));
}

void httpLight() {
  server.send(404, "text/plain", String(lux));
}

void httpPressure() {
  server.send(404, "text/plain", String(hpa));
}

void handleNotFound() { 
   String message = "404 ...sorry";
   server.send(404, "text/plain", message);
}
// --- HTTP END ---

void setup() {

  server.on("/", mainSite);
  server.on("/reset/", httpReset);
  server.on("/restart/", httpRestart);
  server.on("/temp/", httpTemp);
  server.on("/light/", httpLight);
  server.on("/pressure/", httpPressure);


  server.onNotFound(handleNotFound);
  server.begin();
  
  for (int i=1; i<5; i++) {
    pinMode(getPin(i), OUTPUT);
    digitalWrite(getPin(i), HIGH);

    // signal LED
    pinMode(LED, OUTPUT);
    digitalWrite(LED, LOW);
  }

  blink(10, 100);
  Serial.begin (9600);
  Wire.begin();
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

  delay(2000);
  Serial.println();
  Serial.println("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.hostname(WIFI_HOST);
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  while(WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(callback);

  while (!mqttClient.connected()) {
    Serial.println("Connecting to MQTT...");
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWD)) {
      Serial.println("MQTT connected");
    } else {
      Serial.print("MQTT connection failed with state: ");
      Serial.println(mqttClient.state());
      delay(2000);
    }
  }

  // Setup HIGH state for Relay Module
  for(int i=1; i<5; i++) {
    mqttClient.publish(getTopic(i).c_str(), "HIGH");
    mqttClient.subscribe(getTopic(i).c_str());
  }

  // Default locked
  mqttClient.publish(mqtt_topic_lock, "LOCK");
  mqttClient.subscribe(mqtt_topic_lock);

}

void loop() {
  mqttClient.loop();
  server.handleClient();

  // Publish sensor states every PERIOD seconds
  if (millis() - target_time >= PERIOD) {
    target_time += PERIOD;

    // Check the sensors
    lux = lightMeter.readLightLevel();
    hpa_local = bmp.readPressure()/100;
    hpa = hpa_local / pow(1-(altitude/44330), 5.225);
    temp = bmp.readTemperature();
    Serial.printf("Light: %f Lux, Temp: %f C, Pressure: %f hPa\n", lux, temp, hpa);
    mqttClient.publish(mqtt_topic_light, String(lux, 2).c_str());
    mqttClient.publish(mqtt_topic_temperature, String(temp, 2).c_str());
    mqttClient.publish(mqtt_topic_pressure, String(hpa, 2).c_str());
    blink(1);

    // Ping test
    if (!pinger.Ping(MQTT_SERVER)) {
      Serial.println("Ping problem...");
      blink(20, 100);
      ESP.reset();
    } else {
      Serial.println("Ping OK");
    }

    // Check WIFI connection
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WIFI Reconnecting...");
      ESP.reset();
      blink(10, 100);
    }

    // Check MQTT connection
    if (!mqttClient.connected()) {
      Serial.println("MQTT Reconnecting...");
      if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWD)) {
        Serial.println("MQTT connected");
      } else {
        ESP.reset();
        Serial.print("MQTT connection failed with state: ");
        Serial.println(mqttClient.state());
        delay(2000);
      }
      for(int i=1; i<5; i++) {
        mqttClient.publish(getTopic(i).c_str(), "HIGH");
        mqttClient.subscribe(getTopic(i).c_str());
      }
    }

  }

  // Lock again 5 seconds after UNLOCK message
  if(!(locked) && millis() - unlock_timestamp >= UNLOCK_TIME) {
    locked = true;
    mqttClient.publish(mqtt_topic_lock, "LOCK");
  }

}
