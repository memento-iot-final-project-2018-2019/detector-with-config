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
 *    Gabriele Cervelli - Door's magnetic sensor, http configurator and file parsing
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

#include "BlockDevice.h"
#include "LittleFileSystem.h"
#include <stdio.h>
#include <errno.h>
#include <string>
#include "wifi.h"

//Pin for MFRC522 reset (pick another D pin if you need D5)
#define MF_RESET D5

/* Private defines -----------------------------------------------------------*/
#define WIFI_WRITE_TIMEOUT 10000
#define WIFI_READ_TIMEOUT  10000
#define PORT           80

/* Private typedef------------------------------------------------------------*/
typedef enum
{
  WS_IDLE = 0,
  WS_CONNECTED,
  WS_DISCONNECTED,
  WS_ERROR,
} WebServerState_t;

/* Private macro -------------------------------------------------------------*/
static int wifi_sample_run(void);
static void WebServerProcess(void);
static WIFI_Status_t SendWebPage();
/* Private variables ---------------------------------------------------------*/
Serial pc(SERIAL_TX, SERIAL_RX);
static   uint8_t http[1024];
static   uint8_t resp[1024];
uint16_t respLen;
uint8_t  IP_Addr[4];
uint8_t  MAC_Addr[6];
int32_t Socket = -1;
static   WebServerState_t  State = WS_ERROR;
char     ModuleName[32];

// This will take the system's default block device (Flash Memory in our case)
BlockDevice *bd = BlockDevice::get_default_instance();
LittleFileSystem fs("fs");

//Construct MFRC Object
MFRC522    RfChip   (SPI_MOSI, SPI_MISO, SPI_SCK, SPI_CS, MF_RESET);

// An event queue is a very useful structure to debounce information between contexts (e.g. ISR and normal threads)
// This is great because things such as network operations are illegal in ISR, so updating a resource in a button's fall() function is not allowed
EventQueue eventQueue;
Thread thread1;


/*
 * Callback function called when the button1 (blue) is clicked.
 */
void btn1_rise_handler() {
    printf("Initializing the block device... ");
    fflush(stdout);
    int err = bd->init();
    printf("%s\n", (err ? "Fail :(" : "OK"));
    if (err) {
        error("error: %s (%d)\n", strerror(-err), err);
    }

    printf("Erasing the block device... ");
    fflush(stdout);
    err = bd->erase(0, bd->size());
    printf("%s\n", (err ? "Fail :(" : "OK"));
    if (err) {
        error("error: %s (%d)\n", strerror(-err), err);
    }

    printf("Deinitializing the block device... ");
    fflush(stdout);
    err = bd->deinit();
    printf("%s\n", (err ? "Fail :(" : "OK"));
    if (err) {
        error("error: %s (%d)\n", strerror(-err), err);
    }
}

//####################### HTTP SERVER FUNCTIONS ################################

//Connect board to default WIfi (WEB SERVER ONLY)
int wifi_sample_run(void)
{

    /*Initialize and use WIFI module */
    if(WIFI_Init() ==  WIFI_STATUS_OK) {
        printf("ES-WIFI Initialized.\n");

        if(WIFI_GetMAC_Address(MAC_Addr) == WIFI_STATUS_OK) {
            printf("> es-wifi module MAC Address : %X:%X:%X:%X:%X:%X\n",
                   MAC_Addr[0],
                   MAC_Addr[1],
                   MAC_Addr[2],
                   MAC_Addr[3],
                   MAC_Addr[4],
                   MAC_Addr[5]);
        } else {
            printf("> ERROR : CANNOT get MAC address\n");
        }

        if( WIFI_Connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, WIFI_ECN_WPA2_PSK) == WIFI_STATUS_OK) {
            printf("> es-wifi module connected \n");

            if(WIFI_GetIP_Address(IP_Addr) == WIFI_STATUS_OK) {
                printf("> es-wifi module got IP Address : %d.%d.%d.%d\n",
                       IP_Addr[0],
                       IP_Addr[1],
                       IP_Addr[2],
                       IP_Addr[3]);

                printf(">Start HTTP Server... \n");
                printf(">Wait for connection...  \n");
                State = WS_IDLE;
            } else {
                printf("> ERROR : es-wifi module CANNOT get IP address\n");
                return -1;
            }
        } else {
            printf("> ERROR : es-wifi module NOT connected\n");
            return -1;
        }
    } else {
        printf("> ERROR : WIFI Module cannot be initialized.\n");
        return -1;
    }
    return 0;
}

//Run the HTTP Web Server
static void WebServerProcess(void)
{
  switch(State)
  {
  case WS_IDLE:
    Socket = 0;
    WIFI_StartServer(Socket, WIFI_TCP_PROTOCOL, "", PORT);

    if(Socket != -1)
    {
      printf("> HTTP Server Started \n");
      State = WS_CONNECTED;
    }
    else
    {
      printf("> ERROR : Connection cannot be established.\n");
      State = WS_ERROR;
    }
    break;

  case WS_CONNECTED:

    WIFI_ReceiveData(Socket, resp, 1200, &respLen, WIFI_READ_TIMEOUT);

    if( respLen > 0)
    {
      if(strstr((char *)resp, "GET")) /* GET: put web page */
      {
        if(SendWebPage() != WIFI_STATUS_OK)
        {
          printf("> ERROR : Cannot send web page\n");
          State = WS_ERROR;
        }
      }
      else if(strstr((char *)resp, "POST"))/* POST: received info */
      {
            // Try to mount the filesystem
            printf("Mounting the filesystem... ");
            fflush(stdout);
            int err = fs.mount(bd);
            printf("%s\n", (err ? "Fail :(" : "OK"));
            if (err) {
                // Reformat if we can't mount the filesystem
                // this should only happen on the first boot
                printf("No filesystem found, formatting... ");
                fflush(stdout);
                err = fs.reformat(bd);
                printf("%s\n", (err ? "Fail :(" : "OK"));
                if (err) {
                    error("error: %s (%d)\n", strerror(-err), err);
                }
            }

            // Open the conf file
            printf("Opening \"/fs/conf.txt\"... ");
            fflush(stdout);
            FILE *f = fopen("/fs/conf.txt", "r+");
            printf("%s\n", (!f ? "Fail :(" : "OK"));

            std::string r = (char*)resp;

            if(SendWebPage() != WIFI_STATUS_OK)
            {
              printf("> ERROR : Cannot send web page\n");
              State = WS_ERROR;
            }

            std::string b = r.substr(r.find("ssid="), r.size());
            std::string ssid = b.substr(5, b.find("&")-5);

            b = r.substr(r.find("psw="), r.size());
            std::string psw = b.substr(4, b.find("&")-4);

            std::string id = b.substr(b.find("id=")+3, b.size());

            //WRITE ON FILE
            err = fprintf(f, "%s %s %s", ssid, psw, id);
            if (err < 0) {
                    printf("Fail :(\n");
                    error("error: %s (%d)\n", strerror(errno), -errno);
            }

            printf("Seeking file... ");
            fflush(stdout);
            err = fseek(f, 0, SEEK_SET);
            printf("%s\n", (err < 0 ? "Fail :(" : "OK"));
            if (err < 0) {
                error("error: %s (%d)\n", strerror(errno), -errno);
            }

            // Close the file which also flushes any cached writes
            printf("Closing \"/fs/conf.txt\"... ");
            fflush(stdout);
            err = fclose(f);
            printf("%s\n", (err < 0 ? "Fail :(" : "OK"));
            if (err < 0) {
                error("error: %s (%d)\n", strerror(errno), -errno);
            }

            //Unmount FS
            printf("Unmounting... ");
            fflush(stdout);
            err = fs.unmount();
            printf("%s\n", (err < 0 ? "Fail :(" : "OK"));
            if (err < 0) {
                error("error: %s (%d)\n", strerror(-err), err);
            }

            printf("Reboot now! \r\n");
      }
    }
    if(WIFI_StopServer(Socket) == WIFI_STATUS_OK)
    {
      WIFI_StartServer(Socket, WIFI_TCP_PROTOCOL, "", PORT);
    }
    else
    {
      State = WS_ERROR;
    }
    break;
  case WS_ERROR:
  default:
    break;
  }
}


//Send Configuration Page to Client (Browser)
static WIFI_Status_t SendWebPage()
{
  uint16_t SentDataLength;
  WIFI_Status_t ret;

  /* construct web page content */
  strcpy((char *)http, (char *)"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\n\r\n");
  strcat((char *)http, (char *)"<html>\r\n<body>\r\n");
  strcat((char *)http, (char *)"<title>STM32 Web Server</title>\r\n");
  strcat((char *)http, (char *)"<h2>MEmento Board Configuration</h2>\r\n");
  strcat((char *)http, (char *)"<br /><hr>\r\n");

  strcat((char *)http, (char *)"<p><form method=\"POST\"><strong>SSID (WIFI Network Name): <input type=\"text\" size=20 name=\"ssid\" value=\"" "\">");
  strcat((char *)http, (char *)"<br><p><form method=\"POST\"><strong>WIFI PASSWORD: <input type=\"text\" size=20 name=\"psw\" value=\"" "\">");
  strcat((char *)http, (char *)"<br><p><form method=\"POST\"><strong>Twitter ID: <input type=\"text\" size=20 name=\"id\" value=\"" "\">");

  strcat((char *)http, (char *)"</strong><p><input type=\"submit\"></form></span>");
  strcat((char *)http, (char *)"</body>\r\n</html>\r\n");

  ret = WIFI_SendData(0, (uint8_t *)http, strlen((char *)http), &SentDataLength, WIFI_WRITE_TIMEOUT);

  if((ret == WIFI_STATUS_OK) && (SentDataLength != strlen((char *)http)))
  {
    ret = WIFI_STATUS_ERROR;
  }

  return ret;
}



//################################# MAIN #######################################


int main(int argc, char* argv[])
{

//##################### INIT SENSORS AND FILESYSTEM ############################

    RfChip.PCD_Init();

    bool isSubscribed = false;


    //INIT PINs and LED
    DigitalIn  doorSensor(D1);
    DigitalOut led(LED2);
    led = 0;

    //Set magnetic sensor in PullUp mode
    doorSensor.mode(PullUp);
    pc.printf("Pull up mode setted\n");


    // Try to mount the filesystem
    printf("Mounting the filesystem... ");
    fflush(stdout);
    int err = fs.mount(bd);
    printf("%s\n", (err ? "Fail :(" : "OK"));
    if (err) {
        // Reformat if we can't mount the filesystem
        // this should only happen on the first boot
        // or if user pressed config button
        printf("No filesystem found, formatting... ");
        fflush(stdout);
        err = fs.reformat(bd);
        printf("%s\n", (err ? "Fail :(" : "OK"));
        if (err) {
            error("error: %s (%d)\n", strerror(-err), err);
        }
    }

    //Network variables
    NetworkInterface* network = NULL;
    MQTTNetwork* mqttNetwork = NULL;
    MQTT::Client<MQTTNetwork, Countdown>* mqttClient = NULL;

    // Enable button 1 (blue) on the board as erase-flash button
    InterruptIn btn1 = InterruptIn(MBED_CONF_APP_USER_BUTTON);
    // Setup the erase event on button press, use the event queue
    // to avoid running in interrupt context
    btn1.fall(mbed_event_queue()->event(btn1_rise_handler));

    //WAIT 3 seconds for user, if he wants to reconfigure the board he could press
    // blue button in that time window
    wait(3);

    // Open the conf file
    printf("Opening \"/fs/conf.txt\"... ");
    fflush(stdout);
    FILE *f = fopen("/fs/conf.txt", "r+");
    printf("%s\n", (!f ? "Fail :(" : "OK"));


    //########################## CONFIGURATION #################################
    if (!f) {
        // Create the conf file if it doesn't exist
        printf("No file found, creating a new file... ");
        fflush(stdout);
        f = fopen("/fs/conf.txt", "w+");
        printf("%s\n", (!f ? "Fail :(" : "OK"));
        if (!f) {
            error("error: %s (%d)\n", strerror(errno), -errno);
        }


        //START HTTP WEB SERVER ON DEFAULT WIFI ACCESS POINT
        int ret = 0;

        ret = wifi_sample_run();

        if (ret != 0) {
            return -1;
        }


        while(1) {
            WebServerProcess();
        }
    }
    //##################### END CONFIGURATION ##################################



    //##################### READ CONFIGURATION FILE ############################


        char* str = new char[128];

        fgets(str, 128, f);
        printf("LINE: %s", str);

        //Parsing configuration parameters
        char* ssid = strtok(str, " ");
        char* psw = strtok(NULL, " ");
        char* id = strtok(NULL, " ");
        id[strlen(id)-2] = '\0';

        char* ssidF = new char[strlen(ssid)];
        char* pswF = new char[strlen(psw)];
        char* idF = new char[strlen(id)];

        strcpy(ssidF, ssid);
        strcpy(pswF, psw);
        strcpy(idF, id);

        printf("SSID: %s\n", ssidF);
        printf("PSW: %s\n", pswF);
        printf("ID: %s\n", idF);

        delete[] str;

        //Close File
        printf("Closing \"/fs/conf.txt\"... ");
        fflush(stdout);
        err = fclose(f);
        printf("%s\n", (err < 0 ? "Fail :(" : "OK"));
        if (err < 0) {
            error("error: %s (%d)\n", strerror(errno), -errno);
        }

        //Unmount FS
        printf("Unmounting... ");
        fflush(stdout);
        err = fs.unmount();
        printf("%s\n", (err < 0 ? "Fail :(" : "OK"));
        if (err < 0) {
            error("error: %s (%d)\n", strerror(-err), err);
        }

//############################ INIT WIFI #######################################

        //Connect to custom Wi-fi access point
        network = NetworkInterface::get_default_instance();

        if (!network) {
            printf("Error! No network inteface found.\n");
            return 0;
        }

        WiFiInterface *wifi = network->wifiInterface();
        if (wifi) {
            printf("This is a Wi-Fi board\n");
            // call WiFi-specific methods
            nsapi_error_t ret = wifi->connect(ssidF, pswF, NSAPI_SECURITY_WPA_WPA2, 0);
            if (ret) {
                printf("Unable to connect! returned %d\n", ret);
                return -1;
            }
            printf("Connected to network\n");
        }

        delete[] ssidF;
        delete[] pswF;


    // sync the real time clock (RTC)
    NTPClient ntp(network);
    ntp.set_server("time.google.com", 123);
    time_t now = ntp.get_timestamp();
    set_time(now);
    pc.printf("Time is now %s", ctime(&now));


//##################### INIT  MQTT #############################################

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

//############################### LOGIC ########################################

    //Prepare payload with TWITTER ID
    char* payload = new char[128];
    memset(payload, 0, 128);
    strcpy(payload, "{ \"payload\": ");
    strcat(strcat(payload, idF), " }");
    delete[] idF;

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

            //START LED ALERT (A SOUND ALERT COULD BE IMPLEMENTED AS WELL)
            led = 1;

            MQTT::Message message;
            message.retained = false;
            message.dup = false;

            const size_t buf_size = 50;
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
            int rc = mqttClient->publish(MQTT_TOPIC_PUB, message);
            if(rc != MQTT::SUCCESS) {
                pc.printf("ERROR: rc from MQTT publish is %d\r\n", rc);
            }
            pc.printf("Message published.\r\n");
            delete[] buf;
            delete[] payload;


            //check nfc for shutting off the alarm
            pc.printf("Wait alert to stop\n");
            while(! RfChip.PICC_IsNewCardPresent()) {
                wait(0.5);
            }
            pc.printf("Alert stopped\n");

            //reset led to off state and stopAlarm to false
            led = 0;

            //Wait until door is closed before restarting alarm, once it's shutted off
            //the main cycle can restart checking door sensor again.
            pc.printf("Wait door to be closed again\n");
            while(doorSensor.read() == 1) {
                wait(0.5);
            }
        }
        wait(0.5);
    }

    pc.printf("The client has disconnected.\r\n");

//######################### CLEANUP OPERATIONS #################################
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
