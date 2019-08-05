# **detector**

This component is part of the MEmento project, a system to help the user remember to take his house keys and to lock his front door whenever he's leaving his house. 

This detector component allows a [DISCO-L475VG-IOT01A](https://os.mbed.com/platforms/ST-Discovery-L475E-IOT01A/#board-pinout) board running [mbedOS](https://www.mbed.com/en/) to establish an **MQTT** connection to your [AWS IoT Core](https://aws.amazon.com/iot-core/), and to publish data every time it detects high impedence on a simple **magnetic switch**. 
The goal is reached by checking the **integer value returned by the switch**, that's connected to a **digital pin** (in our case the D1 pin, we could obviously use any of the digital pins of the board to do that). Also, **a led is turned on** every time **the door is opened** (this could be replaced by a sound alert). The led is turned off as soon as an **RFID tag** is brought **close to the RFID reader**.

## Build instructions

Our supported **build platform** is the [mbedOS online compiler](https://ide.mbed.com/compiler).

For detailed instructions on **how to setup the board for AWS IoT Core** follow this [link](https://os.mbed.com/users/coisme/notebook/aws-iot-from-mbed-os-device/). For a complete guide on how to set up a complete key-reminding system, check out our [blogpost](https://www.hackster.io/memento-team/memento-07ff93).
That also includes instructions for the board Wi-Fi module setup.

## First boot configuration (also valid for re-configuration)

- Once the firmware is flashed to the board, **set up a Wi-Fi Hotspot** with **ssid = memento** and **pswd = 123456789**.
- Now boot the board, if it's the **first boot** it will automatically run the **HTTP** server.
-**If it's not,** **press the blue button within 3 seconds from boot**, then **reboot** the board with the **black button**.
- Wait some seconds, so that the board can connect to the Hotspot and setup the http server.
- **Identify** the **local IP address of the board** (you can find it in the admin panel of the board), then, on any Web Browser, **insert the IP address in the address bar** and press enter.
- **Fill the form** with your real **Wi-fi credentials** and your **Twitter ID**, and **deliver the form**.
- **Reboot the board.**
