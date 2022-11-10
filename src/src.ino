#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <string.h>

#define WIFI_SSID      "MYSSID"
#define WIFI_PASSWD    "***** ***"
#define WIFI_HOST      "Gate-Assist"
#define MQTT_SERVER    "ADDRESS"
#define MQTT_PORT      1883
#define MQTT_USER      "USER"
#define MQTT_PASSWD    "***** ***"
#define MQTT_CLIENT_ID "GateAssistant"

const char *mqtt_topic_prefix  = "gateassistant/sw";

WiFiClient netClient;
PubSubClient mqttClient(netClient);


String getTopic(int sw) {
  String topic = mqtt_topic_prefix + String(sw);
  return topic;
}

int getPin(int sw) {
  switch(sw) {
    case 1:
      return D0;
      break;
    case 2:
      return D1;
      break;
    case 3:
      return D2;
      break;
    case 4:
      return D3;
      break;
  }
}

void shortPress(int sw) {
  digitalWrite(getPin(sw), LOW);
  delay(1000);
  digitalWrite(getPin(sw), HIGH);
  mqttClient.publish(getTopic(sw).c_str(), "HIGH");
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
  Serial.begin (9600);
  delay(10);
  Serial.println('\n');

  for (int i=1; i<5; i++) pinMode(getPin(i), OUTPUT);

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
      Serial.println("connected");
    } else {
      Serial.print("failed with state ");
      Serial.println(mqttClient.state());
      delay(2000);
    }
  }

  for(int i=1; i<5; i++) {
    mqttClient.publish(getTopic(i).c_str(), "HIGH");
    mqttClient.subscribe(getTopic(i).c_str());
  }

  for (int i=1; i<5; i++) digitalWrite(getPin(i), HIGH);

}

void loop() {
  mqttClient.loop();
}
