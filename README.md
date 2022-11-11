# gateassistant
Home Gate controller

## Description:
- Controls relays using the MQTT broker allowing for example opening or closing gates.
- Providing data from sensors, i.e. light level, temperature, atmospheric pressure.
- Full compatibility with Home Assistant.

## Hardware:
1. NodeMcu v3 (ESP8266)
2. HW-316 (4 Relay Module)


## Wiring:
```
(NodeMcu) <----> (HW-316)
  D0                IN1
  D1                IN2
  D2                IN3
  D3                IN4
  3V                VCC
  G                 GND
  VIN               JD-VCC
```
