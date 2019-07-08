# **detector**

This component allows [DISCO-L475VG-IOT01A](https://os.mbed.com/platforms/ST-Discovery-L475E-IOT01A/#board-pinout)  board running [mbedOS](https://www.mbed.com/en/) to enstablish an **MQTT** connection to your [AWS IoT Core](https://aws.amazon.com/iot-core/) and to publish data every time a simple **magnetic sensor** detects high impedence case on the circuit.  
The goal is reached by checking the **integer value returned by the sensor** connected to a **digital pin** (in our case the D1 pin, we could obviously use any of the board's digital pins to do that). Also **a led is turned on**, it could be replaced by a sound alert, **every time the door is open**.

//COMPLETE WITH RFID/NFC INTRO


## Usage instructions

Our supported **build platform** is [mbedOS online compiler](https://ide.mbed.com/compiler).

For detailed instructions on **how to setup the board for AWS IoT Core** follow this [link](https://os.mbed.com/users/coisme/notebook/aws-iot-from-mbed-os-device/).
That also includes instructions for the board Wi-Fi module setup.

The custom message that will be pushed to AWS endpoint has to be specified here between the two double apices in `main.cpp` file.

`char payload[] = "{ \"payload\": TWITTER-ID-GOES-HERE }";`

In our case, it's a `JSON` message containing a **field named payload** with a **Twitter ID valued** associated.
