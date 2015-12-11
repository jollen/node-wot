# Node-WoT Firmware

The nodemcu firmware fork. This project modify the [NodeMCU-Firmware](https://github.com/nodemcu/nodemcu-firmware) to fit the needs of WoT.City project. 

* Replace libcoap with er-coap-13 which has good API design.
* When ESP8266 is deployed as a data sender object, only CoAP client implementation is needed. Node-WoT firmware has only er-coap-13 client but we will put CoAP back in the near future.

Node-WoT provides a good CoAP SDK environment for ESP8266.

## Install and Usage

Please read [NodeMCU-Firmware](https://github.com/nodemcu/nodemcu-firmware) for compiling and updating firmware.
