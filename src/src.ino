#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_BMP280.h>
#include <string.h>

// ------------ Configuration -----------
// #define WIFI_SSID      "MYSSID"
// #define WIFI_PASSWD    "***** ***"
// #define WIFI_HOST      "Gate-Assist"
// #define MQTT_SERVER    "ADDRESS"
// #define MQTT_PORT      1883
// #define MQTT_USER      "USER"
// #define MQTT_PASSWD    "***** ***"
// #define MQTT_CLIENT_ID "GateAssistant"

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

WiFiClient netClient;
BH1750 lightMeter;   // Light sensor
Adafruit_BMP280 bmp; // Temperature and pressure sensor

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
}

void shortPress(int sw) {
  digitalWrite(getPin(sw), LOW);
  digitalWrite(LED, HIGH);
  delay(1000);
  digitalWrite(getPin(sw), HIGH);
  digitalWrite(LED, LOW);
  mqttClient.publish(getTopic(sw).c_str(), "HIGH");
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
  int sw_number = int(topic[strlen(topic)-1]) - '0';
  if(message == "LOW") digitalWrite(getPin(sw_number), LOW);
  if(message == "HIGH") digitalWrite(getPin(sw_number), HIGH);
  if(message == "PRESS") shortPress(sw_number);
}

void setup() {
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

}

void loop() {
  mqttClient.loop();

  if (millis() - target_time >= PERIOD) {
    target_time += PERIOD;

    // Check the sensors
    float lux = lightMeter.readLightLevel();
    float hpa_local = bmp.readPressure()/100;
    float hpa = hpa_local / pow(1-(altitude/44330), 5.225);
    float temp = bmp.readTemperature();
    Serial.printf("Light: %f Lux, Temp: %f C, Pressure: %f hPa\n", lux, temp, hpa);
    mqttClient.publish(mqtt_topic_light, String(lux, 2).c_str());
    mqttClient.publish(mqtt_topic_temperature, String(temp, 2).c_str());
    mqttClient.publish(mqtt_topic_pressure, String(hpa, 2).c_str());
    blink(1);

    // Check WIFI connection
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WIFI Reconnecting...");
      WiFi.disconnect();
      WiFi.reconnect();
      blink(10, 100);
    }

    // Check MQTT connection
    if (!mqttClient.connected()) {
      Serial.println("MQTT Reconnecting...");
      if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWD)) {
        Serial.println("MQTT connected");
      } else {
        Serial.print("MQTT connection failed with state: ");
        Serial.println(mqttClient.state());
        delay(2000);
      }
    }

  }
}
