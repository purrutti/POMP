#include <Ethernet.h>
#include <WebSockets.h>
#include <WebSocketsClient.h>
#include <WebSocketsServer.h>
#include <ModbusRtu.h>
#include <TimeLib.h>
#include <EEPROMex.h>
#include <ArduinoJson.h>
#include <RTC.h>
#include <PID_v1.h>


#include "Meso.h"


const byte PLCID = 1;

/***** PIN ASSIGNMENTS *****/
const byte PIN_DEBITMETRE[12] = { 54,55,56,57,58,59,19,60,61,62,63,64 };//Chaud, froid, ambiant

const byte PIN_HR[12] = { 37,36,39,38,4,40,42,41,44,43,8,45 };//Chaud, froid, ambiant
const byte PIN_PRESSION = 65;
const byte PIN_V2V = 5;


// Enter a MAC address for your controller below.
// Newer Ethernet shields have a MAC address printed on a sticker on the shield
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, PLCID };

// Set the static IP address to use if the DHCP fails to assign
IPAddress ip(172, 16, 253, 200 + PLCID);

const char* SERVER_IP = "172.16.253.82";

WebSocketsClient webSocket;
ModbusRtu master(0, 3, 46); // this is master and RS-232 or USB-FTDI


ModbusSensor mbSensor(1);


tempo tempoSensorRead;
tempo tempoSendValues;
tempo tempoMBSensorRead;

double minPression = 0.15;

double ambiantpH = 0;
double ambiantTemperature = 0;

Meso meso[12];



enum {
    REQ_PARAMS = 0,
    REQ_DATA = 1,
    SEND_PARAMS = 2,
    SEND_DATA = 3,
    CALIBRATE_SENSOR = 4,
    REQ_MASTER_DATA = 5,
    SEND_MASTER_DATA = 6
};


void webSocketEvent(WStype_t type, uint8_t* payload, size_t lenght) {
    Serial.println(" WEBSOCKET EVENT:");
    Serial.println(type);
    switch (type) {
    case WStype_DISCONNECTED:
        Serial.println(" Disconnected!");
        break;
    case WStype_CONNECTED:
        Serial.println(" Connected!");

        // send message to client
        webSocket.sendTXT("Connected");
        break;
    case WStype_TEXT:

        Serial.print(" Payload:"); Serial.println((char*)payload);
        readJSON((char*)payload);

        break;
    case WStype_ERROR:
        Serial.println(" ERROR!");
        break;
    }
}

unsigned long dateToTimestamp(int year, int month, int day, int hour, int minute) {

    tmElements_t te;  //Time elements structure
    time_t unixTime; // a time stamp
    te.Day = day;
    te.Hour = hour;
    te.Minute = minute;
    te.Month = month;
    te.Second = 0;
    te.Year = year - 1970;
    unixTime = makeTime(te);
    return unixTime;
}


// the setup function runs once when you press reset or power the board
void setup() {
    Serial.begin(115200);


    master.begin(9600); // baud-rate at 19200
    master.setTimeOut(1000); // if there is no answer in 5000 ms, roll over

    Serial.println("START SETUP");


    int  startAddress = 10;
    for (int i = 0; i < 12; i++) {
        meso[i] = Meso(i + (PLCID*12-11), PIN_DEBITMETRE[i], PIN_HR[i]);
        meso[i].startAddress = startAddress;
        startAddress = meso[i].load();

        meso[i].regulTemp.pid = PID((double*)&meso[i].temperature, (double*)&meso[i].regulTemp.sortiePID, (double*)&meso[i].regulTemp.consigne, meso[i].regulTemp.Kp, meso[i].regulTemp.Ki, meso[i].regulTemp.Kd, DIRECT);

        meso[i].regulTemp.pid.SetSampleTime(100);
        
        meso[i].regulTemp.pid.SetOutputLimits(0, 100);
        meso[i].regulTemp.pid.SetMode(AUTOMATIC);
        meso[i].regulTemp.pid.SetTunings(5.0, 1.0, 0.0);
        meso[i].regulTemp.pid.SetControllerDirection(DIRECT);
        pinMode(PIN_HR[i], OUTPUT);
        pinMode(PIN_DEBITMETRE[i], INPUT);
        digitalWrite(PIN_HR[i], LOW);
    }

    
    Ethernet.begin(mac,ip);
    Serial.println("ETHER");
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {

        Serial.println("NO ETHERNET");
        while (true) {
            delay(1000); // do nothing, no point running without Ethernet hardware
            if (Ethernet.hardwareStatus() != EthernetNoHardware) break;
        }
        
    }



    Serial.println("Ethernet connected");

    Serial.print("localIP"); Serial.println(Ethernet.localIP());

    webSocket.begin(SERVER_IP, 8189);
    webSocket.onEvent(webSocketEvent);
    

    RTC.read();
    //setPIDparams();

    Serial.println("START");
    tempoSensorRead.interval = 1000;
    tempoSendValues.interval = 5000;
    tempoMBSensorRead.interval = 100;

    for (int i = 0; i < 12; i++) {
        meso[i].regulTemp.consigne = 0.5;
    }

}

void loop() {

    webSocket.loop();


    readMBSensors();
   // readFlow();
   regulTemp();

    if (elapsed(&tempoSendValues)) {
       sendData();

        for (int i = 0; i < 12; i++)
        {
                Serial.print(F("*********\nSensor ID:")); Serial.println(meso[i].id);
            Serial.print(F("oxy %:")); Serial.println(meso[i].O2);
            Serial.print(F("temp:")); Serial.println(meso[i].temperature);
            Serial.print(F("consigne:")); Serial.println(meso[i].regulTemp.consigne);
            Serial.print(F("PID%:")); Serial.println(meso[i].regulTemp.sortiePID_pc);
        }

    }


}

bool regulTemp() {
    for (int i = 0; i < 12; i++) {
       meso[i].regulTemperature();

    }
}


int flowReadIndex = 0;
void readFlow() {
    if(meso[flowReadIndex].readFlow()) flowReadIndex++;
    if (flowReadIndex == 12) flowReadIndex = 0;
}





int state = 0;

typedef struct Calibration {
    int sensorID;
    int calibParam;
    float value;
    bool calibEnCours;
    bool calibRequested;
}Calibration;

Calibration calib;

int sensorIndex = 0;
bool pHSensor = true;


void readMBSensors() {
    if (elapsed(&tempoMBSensorRead)) {

        if (state == 0 && calib.calibRequested) {
            calib.calibRequested = false;
            calib.calibEnCours = true;
        }
        if (calib.calibEnCours) {
            //calibrateSensor();
        }
        else {
            mbSensor.query.u8id = 10 + sensorIndex;
            if (state == 0) {
                if (mbSensor.requestValues(&master)) {
                    state = 1;
                }
            }
            else if (mbSensor.readValues(&master, &meso[sensorIndex].O2, &meso[sensorIndex].temperature)) {

                /*Serial.print(F("Sensor ID:")); Serial.println(meso[sensorIndex].id);
                Serial.print(F("oxy %:")); Serial.println(meso[sensorIndex].O2);
                Serial.print(F("temp:")); Serial.println(meso[sensorIndex].temperature);*/
                state = 0;
                sensorIndex++;
                if (sensorIndex == 12) sensorIndex = 0;
            }
        }        
    }
}

void sendData() {
        Serial.println("SEND DATA");
        for(int i=0;i<12;i++)
        {
            meso[i].serializeData(RTC.getTime(), PLCID, buffer);
            Serial.println(buffer);
            webSocket.sendTXT(buffer);
        }

}



void sendParams() {
    StaticJsonDocument<jsonDocSize> doc;
    Serial.println("SEND PARAMS");
    for (int i = 0; i < 12; i++) {
        meso[i].serializeParams(RTC.getTime(), PLCID, buffer);
        Serial.println(buffer);
        webSocket.sendTXT(buffer);
    }

}

void reqParams() {
    Serial.println("REQ PARAMS");
    webSocket.sendTXT("{\"cmd\":0,\"AquaID\":0}");

}


void readJSON(char* json) {
    StaticJsonDocument<jsonDocSize> doc;
    char buffer[bufferSize];
    Serial.print("payload received:"); Serial.println(json);
    //deserializeJson(doc, json);

    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }

    uint8_t command = doc["cmd"];
    uint8_t destID = doc["PLCID"];
    uint8_t senderID = doc["AquaID"];

    if (command == 0) {
        Serial.println("COmmand 0"); return;
    }
    else Serial.println("Command" + command);

    uint32_t time = doc["time"];
    if (time > 0) RTC.setTime(time);
    if (destID == PLCID) {
        switch (command) {
        case REQ_PARAMS:
            sendParams();
            //condition.serializeParams(buffer, RTC.getTime(),CONDID);
            //webSocket.sendTXT(buffer);
            break;
        case REQ_DATA:
            sendData();
            break;
        case SEND_PARAMS:
            meso[senderID].deserializeParams(doc);
            break;
        default:
            //webSocket.sendTXT(F("wrong request"));
            break;
        }
    }
}