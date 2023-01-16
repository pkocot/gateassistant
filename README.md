# Home Gate Assistant
Allows you to use the original remote control to control your devices.

Easy to integrate with Home Assistant.

## Description:
- Controls relays using the MQTT broker, allowing for example opening or closing gates.
  In my case it controls home gates via connected remote control.
- Provides data from sensors, i.e. light level, temperature, atmospheric pressure.
  Sends metrics to MQTT broker.


## Hardware:
1. NodeMcu v3 (ESP8266)
2. HW-316 (4 Relay Module)
3. BH1750 (Light sensor)
4. BMP280 (Temperature/Pressure sensor)
5. PCB from remote controller
6. Green LED


## Base Wiring:
```
(NodeMcu) <----> (HW-316) <----> (Remote PCB)
  D5                IN1             1
  D6                IN2             2
  D7                IN3             3
  D8                IN4             4
  3V                VCC             -
  G                 GND             GND
  VIN (5V)          JD-VCC          VCC

(NodeMcu) <----> (BMP280)
  D1                SCL
  D2                SDA
  3V                VIN     5V ?
  G                 GND

(NodeMcu) <----> (BH1750)
  3V                VCC
  G                 GND
  D1                SCL
  D2                SDA

(NodeMcu) <----> (LED diode)
  D3                Anode (+)
  G                 Cathode (-)


```
