# Node-WoT Firmware

The nodemcu firmware fork. This project modify the [NodeMCU-Firmware](https://github.com/nodemcu/nodemcu-firmware) to fit the needs of WoT.City project. 

* Replace libcoap with er-coap-13 which has good API design.
* When ESP8266 is deployed as a data sender object, only CoAP client implementation is needed. Node-WoT firmware has only er-coap-13 client but we will put CoAP server back in the near future.

Node-WoT provides a hi-level API CoAP SDK environment for ESP8266. Read [Use er-coap-13 for NodeMCU](http://www.jollen.org/blog/2015/12/nodemcu-firmware-er-coap-13.html) for details (Traidtional Chinese).

## Install and Usage

Please read [NodeMCU-Firmware](https://github.com/nodemcu/nodemcu-firmware) for compiling and updating firmware.
