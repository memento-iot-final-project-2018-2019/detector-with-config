/*******************************************************************************
 * Copyright (c) 2014, 2015 IBM Corp, 2019 MEmento Team.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial API and implementation and/or initial documentation
 *    Ian Craggs - make sure QoS2 processing works, and add device headers
 *    Gabriele Cervelli - Door's magnetic sensor 
 *    Giovanni De Luca - RFID handling
 *******************************************************************************/


#include "mbed.h"
#include "NTPClient.h"
#include "MQTTNetwork.h"
#include "MQTTmbed.h"
#include "MQTTClient.h"
#include "MQTT_server_setting.h"
#include "mbed_events.h"
#include "mbedtls/error.h"
#include "MFRC522.h"
 
//Pin for MFRC522 reset (pick another D pin if you need D5)
#define MF_RESET D5

//Serial out for debug
Serial pc(SERIAL_TX, SERIAL_RX);

//Construct MFRC Object
MFRC522    RfChip   (SPI_MOSI, SPI_MISO, SPI_SCK, SPI_CS, MF_RESET);

static volatile bool stopAlarm = false;

// An event queue is a very useful structure to debounce information between contexts (e.g. ISR and normal threads)
// This is great because things such as network operations are illegal in ISR, so updating a resource in a button's fall() function is not allowed
EventQueue eventQueue;
Thread thread1;

/*
 * Callback function called when the button1 is clicked.
 */
void btn1_rise_handler() {
    stopAlarm = true;
}


int main(int argc, char* argv[])
{
    RfChip.PCD_Init();
        
    const float version = 0.9;
    bool isSubscribed = false;
    
    //INIT PINs and LED
    DigitalIn  doorSensor(D1);
    DigitalOut led(LED2);
    led = 0;
    
    //Check on magnetic sensor
    if(doorSensor.is_connected()) {
        pc.printf("Door sensor pin is connected and initialized! \r\n");
    }
    //Set magnetic sensor in PullUp mode
    doorSensor.mode(PullUp);
    pc.printf("Pull up mode setted\r\n");
    
    NetworkInterface* network = NULL;
    MQTTNetwork* mqttNetwork = NULL;
    MQTT::Client<MQTTNetwork, Countdown>* mqttClient = NULL;

    pc.printf("HelloMQTT: version is %.2f\r\n", version);
    pc.printf("\r\n");

    pc.printf("Opening network interface...\r\n");
    {
        network = NetworkInterface::get_default_instance();
        if (!network) {
            pc.printf("Error! No network inteface found.\r\n");
            return -1;
        }

        pc.printf("Connecting to network\r\n");
        nsapi_size_or_error_t ret = network->connect();
        if (ret) {
            pc.printf("Unable to connect! returned %d\r\n", ret);
            return -1;
        }
    }
    pc.printf("Network interface opened successfully.\r\n");
    pc.printf("\r\n");

    // sync the real time clock (RTC)
    NTPClient ntp(network);
    ntp.set_server("time.google.com", 123);
    time_t now = ntp.get_timestamp();
    set_time(now);
    pc.printf("Time is now %s", ctime(&now));
    
        

    pc.printf("Connecting to host %s:%d ...\r\n", MQTT_SERVER_HOST_NAME, MQTT_SERVER_PORT);
    {
        mqttNetwork = new MQTTNetwork(network);
        int rc = mqttNetwork->connect(MQTT_SERVER_HOST_NAME, MQTT_SERVER_PORT, SSL_CA_PEM,
                SSL_CLIENT_CERT_PEM, SSL_CLIENT_PRIVATE_KEY_PEM);
        if (rc != MQTT::SUCCESS){
            const int MAX_TLS_ERROR_CODE = -0x1000;
            // Network error
            if((MAX_TLS_ERROR_CODE < rc) && (rc < 0)) {
                pc.printf("ERROR from MQTTNetwork connect is %d.", rc);
            }
            // TLS error - mbedTLS error codes starts from -0x1000 to -0x8000.
            if(rc <= MAX_TLS_ERROR_CODE) {
                const int buf_size = 256;
                char *buf = new char[buf_size];
                mbedtls_strerror(rc, buf, buf_size);
                pc.printf("TLS ERROR (%d) : %s\r\n", rc, buf);
            }
            return -1;
        }
    }
    pc.printf("Connection established.\r\n");
    pc.printf("\r\n");


    pc.printf("MQTT client is trying to connect the server ...\r\n");
    {
        MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
        data.MQTTVersion = 3;
        data.clientID.cstring = (char *)MQTT_CLIENT_ID;
        data.username.cstring = (char *)MQTT_USERNAME;
        data.password.cstring = (char *)MQTT_PASSWORD;

        mqttClient = new MQTT::Client<MQTTNetwork, Countdown>(*mqttNetwork);
        int rc = mqttClient->connect(data);
        if (rc != MQTT::SUCCESS) {
            pc.printf("ERROR: rc from MQTT connect is %d\r\n", rc);
            return -1;
        }
    }
    pc.printf("Client connected.\r\n");
    pc.printf("\r\n");


    // Enable button 1 on the board as emergency alert shutdown
    InterruptIn btn1 = InterruptIn(MBED_CONF_APP_USER_BUTTON);
    btn1.rise(btn1_rise_handler);
    
    

    while(1) {
        /* Check connection */
        if(!mqttClient->isConnected()){
            break;
        }
        /* Pass control to other thread. */
        if(mqttClient->yield() != MQTT::SUCCESS) {
            break;
        }
        /* Publish data */

        // IF DOOR IS OPEN
        if (doorSensor.read() == 1) {
            static unsigned short id = 0;
            char payload[] = "{ \"payload\": TWITTER-ID-GOES-HERE }";

            //START LED ALERT (A SOUND ALERT COULD BE IMPLEMENTED AS WELL)
            led = 1;

            MQTT::Message message;
            message.retained = false;
            message.dup = false;

            const size_t buf_size = 100;
            char *buf = new char[buf_size];
            message.payload = (void*)buf;

            message.qos = MQTT::QOS0;
            message.id = id++;
            int ret = snprintf(buf, buf_size, "%s", payload);
            if(ret < 0) {
                pc.printf("ERROR: snprintf() returns %d.", ret);
                continue;
            }
            message.payloadlen = ret;
            // Publish a message.
            pc.printf("Publishing message.\r\n");
            int rc = mqttClient->publish(MQTT_TOPIC_SUB, message);
            if(rc != MQTT::SUCCESS) {
                pc.printf("ERROR: rc from MQTT publish is %d\r\n", rc);
            }
            pc.printf("Message published.\r\n");
            delete[] buf;
            
            // Wait for the RFID tag to stop the alert
            pc.printf("Wait alert to stop\r\n");
            while(! (RfChip.PICC_IsNewCardPresent() || stopAlarm)) {
                wait(0.5);
            }
            pc.printf("Alert stopped\r\n");

            // Reset led to off state and stopAlarm to false
            led = 0;
            stopAlarm = false;
            
            // Wait until the door is closed before restarting the cycle
            pc.printf("Wait for door to be closed again\r\n");
            while(doorSensor.read() == 1) {
                wait(0.5);
            }
        }
        wait(0.5);
    }

    pc.printf("The client has disconnected.\r\n");

    if(mqttClient) {
        if(isSubscribed) {
            mqttClient->unsubscribe(MQTT_TOPIC_SUB);
            mqttClient->setMessageHandler(MQTT_TOPIC_SUB, 0);
        }
        if(mqttClient->isConnected())
            mqttClient->disconnect();
        delete mqttClient;
    }
    if(mqttNetwork) {
        mqttNetwork->disconnect();
        delete mqttNetwork;
    }
    if(network) {
        network->disconnect();
        // network is not created by new.
    }
}
