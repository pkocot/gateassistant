#include <ESP8266WiFi.h>
#include <PubSubClient.h>

const char *ssid        ="SSID";
const char *wifi_passwd = "***** ***";
const char *mqtt_server = "mqtt.lan";
const int   mqtt_port   = 1833;
const char *mqtt_user   = "user";
const char *mqtt_passwd = "***** ***";
const char *mqtt_topic  = "/root/GateAssistant";


int switch1 = D0;
int switch2 = D1;
int switch3 = D3;
int switch4 = D4;

WiFiClient netClient;
PubSubClient mqttClient(netClient);


void callback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("Switch number: %s", topic);

  String message;
  for (int i = 0; i < length; i++) {
    message = message + (char)payload[i];
  }
  Serial.printf("State: %s", message)

  if(message == "0") digitalWrite(topic, LOW);
  if(message == "1") digitalWrite(topic, HIGH);
}

void setup() {
  Serial.begin (9600);

  pinMode(switch1, OUTPUT);
  pinMode(switch2, OUTPUT);
  pinMode(switch3, OUTPUT);
  pinMode(switch4, OUTPUT);

  Serial.print("WiFi connecting ")
  WiFi.begin(ssid,passwd);
  while(WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(callback);

  while (!mqttClient.connected()) {
    Serial.println("Connecting to MQTT...");
    if (client.connect("GateAssistant", mqtt_user, mqtt_passwd)) {
      Serial.println("connected");
    } else {
      Serial.print("failed with state ");
      Serial.print(client.state());
      delay(2000);
    }
  }

  // mqttClient.publish(mqtt_topic, "Nice message");
  mqttClient.subscribe(mqtt_topic);

}

void loop() {
  mqttClient.loop();
}
