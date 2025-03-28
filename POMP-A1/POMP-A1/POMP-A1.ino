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
//const byte PIN_DEBITMETRE[12] = { 54,55,56,57,58,59,19,60,61,62,63,64 };//Chaud, froid, ambiant
const byte PIN_DEBITMETRE[12] = { 55,54,57,56,59,58,60,19,62,61,64,63 };//Chaud, froid, ambiant

const byte PIN_HR[12] = { 37,36,39,38,4,40,42,41,44,43,8,45 };//Chaud, froid, ambiant
const byte PIN_PRESSION = 65;
const byte PIN_V2V = 5;

double pression;

Regul regulPression;

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

    pinMode(PIN_PRESSION, INPUT);

    int  startAddress = 10;
    for (int i = 0; i < 12; i++) {
        meso[i] = Meso(i + (PLCID*12-11), PIN_DEBITMETRE[i], PIN_HR[i]);
        meso[i].startAddress = startAddress;
        startAddress = meso[i].load();

        meso[i].regulTemp.pid = PID((double*)&meso[i].temperature, (double*)&meso[i].regulTemp.sortiePID, (double*)&meso[i].regulTemp.consigne, meso[i].regulTemp.Kp, meso[i].regulTemp.Ki, meso[i].regulTemp.Kd, DIRECT);

        meso[i].regulTemp.pid.SetSampleTime(100);
        
        meso[i].regulTemp.pid.SetOutputLimits(0, 100);
        meso[i].regulTemp.pid.SetMode(AUTOMATIC);
        //meso[i].regulTemp.pid.SetTunings(5.0, 1.0, 0.0);
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

    /*for (int i = 0; i < 12; i++) {
        meso[i].regulTemp.consigne = 0.5;
        meso[i].regulTemp.offset = 0.0;
    }*/


    regulPression = Regul();

    regulPression.load(350);
   // regulPression.Kp = 100;
    //regulPression.Ki = 10;
    //regulPression.Kd = 20;
    regulPression.pid = PID((double*)&pression, (double*)&regulPression.sortiePID, (double*)&regulPression.consigne, regulPression.Kp, regulPression.Ki, regulPression.Kd, DIRECT);
    regulPression.pid.SetOutputLimits(50, 255);
    regulPression.pid.SetMode(AUTOMATIC);
    //regulPression.consigne = 1.0;


    //regulPression.save(350);

}

void loop() {

    webSocket.loop();
    if (PLCID == 1) {

        pression = readPressure(1);
        regulationPression(pression);
    }


   readMBSensors();
   readFlow();
   regulTemp();

    if (elapsed(&tempoSendValues)) {
       sendData();

        for (int i = 0; i < 12; i++)
        {
                Serial.print(F("*********\nSensor ID:")); Serial.println(meso[i].id);
            Serial.print(F("oxy %:")); Serial.println(meso[i].O2);
            Serial.print(F("temp:")); Serial.println(meso[i].temperature);
            Serial.print(F("consigne:")); Serial.println(meso[i].regulTemp.consigne);
            Serial.print(F("offset:")); Serial.println(meso[i].regulTemp.offset);
            Serial.print(F("PID%:")); Serial.println(meso[i].regulTemp.sortiePID_pc);
        }

        if (PLCID == 1) {
            Serial.print(F("PRESSION:")); Serial.println(pression);
            Serial.print(F("V2V:")); Serial.println(regulPression.sortiePID_pc);
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
            calibrateSensor();
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


void calibrateSensor() {   

        Serial.println("CALIBRATE PODOC");
        mbSensor.query.u8id = calib.sensorID;
        if (calib.calibParam == 99) {
            if (state == 0) {
                if (mbSensor.factoryReset(&master)) state = 1;
            }
            else {
                calib.calibEnCours = false;
                state = 0;
            }
        }
        else {
            int offset;
            if (state == 0) {
                //PODOC oxy
                if (calib.calibParam == 0) {
                    offset = 516;
                }
                else {
                    offset = 522;
                }
                if (mbSensor.calibrateCoeff(calib.value, offset, &master)) state = 1;
            }
            else {
                offset = 654;
                if (mbSensor.validateCalibration(offset, &master)) {
                    state = 0;
                    calib.calibEnCours = false;

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
        if (PLCID == 1) {
            webSocket.sendTXT("{\"cmd\":5,\"pression\":"+String(pression) + ",\"sPIDpression\":"+String(regulPression.sortiePID_pc) + "}");
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
        if (command == 4) {
            Serial.println("CALIB REQ received");
            calib.sensorID = doc[F("sensorID")];
            calib.calibParam = doc[F("calibParam")];
            calib.value = doc[F("value")];

            Serial.print(F("calib.sensorID:")); Serial.println(calib.sensorID);
            Serial.print(F("calib.calibParam:")); Serial.println(calib.calibParam);
            Serial.print(F("calib.value:")); Serial.println(calib.value);

            calib.calibRequested = true;
        }
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
            Serial.println("SEND PARAMS");
            meso[senderID - (PLCID * 12 - 11)].deserializeParams(doc);

            Serial.println("SEND PARAMS");
            break; 
        case 6:
                Serial.println("SEND Master PARAMS");
                JsonObject regulP = doc["rPression"];

                regulPression.consigne = regulP[scons]; // 24.2
                regulPression.Kp = regulP[sKp]; // 2.1
                regulPression.Ki = regulP[sKi]; // 2.1
                regulPression.Kd = regulP[sKd]; // 2.1

                regulPression.save(350);

                break;
        default:
            //webSocket.sendTXT(F("wrong request"));
            break;
        }
    }
}





float readPressure(int lissage) {
    int ana = analogRead(PIN_PRESSION); // 0-1023 value corresponding to 0-10 V corresponding to 0-20 mA
    //if using 330 ohm resistor so 20mA = 6.6V
    //int ana2 = ana * 10 / 6.6;
    int mA = map(ana, 0, 1023, 0, 2000); //map to milli amps with 2 extra digits
    int mbars = map(mA, 400, 2000, 0, 4000); //map to milli amps with 2 extra digits
    double anciennePression = pression;
    pression = ((double)mbars) / 1000.0; // pressure in bars
    //pression = (lissage * pression + (100.0 - lissage) * anciennePression) / 100.0;
    return pression;
}

double regulationPression(double mesure) {

    regulPression.pid.Compute();
    regulPression.sortiePID_pc = (int)(regulPression.sortiePID * 100 / 255);

    analogWrite(PIN_V2V, (int)regulPression.sortiePID);

    return regulPression.sortiePID;
}