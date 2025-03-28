#pragma once
#include <EEPROMex.h>

#include "C:\Users\pierr\Dropbox\Pierre\CNRS\repos\POMP\ModbusSensor.h"
#include <PID_v1.h>

float tempAmbiante = 18;
float tempChaud = 24;
float tempFroid = 5;
float pHAmbiant = 8;


const char* scmd = "cmd";
const char* sID = "AquaID";
const char* sPLCID = "PLCID";
const char* stime = "time";

const char* soxy = "oxy";
const char* stemp = "temp";
const char* sdata = "data";
const char* rTemp = "rTemp";
const char* rpH = "rpH";

const char* scons = "cons";
const char* sPID_pc = "sPID_pc";
const char* sdebit = "debit";

const char* sKp = "Kp";
const char* sKi = "Ki";
const char* sKd = "Kd";
const char* saForcage = "aForcage";
const char* sconsForcage = "consForcage";

char buffer[500];
const size_t jsonDocSize = 512;
const int bufferSize = 500;


typedef struct tempo {
    unsigned long debut;
    unsigned long interval;
}tempo;


unsigned long flowIntegrationDuration = 1000; // Derni�re mise � jour du d�bit
unsigned long lastTime = 0; // Derni�re mise � jour du d�bit
int pulseCount;

bool elapsed(tempo* t) {
    if (t->debut == 0) {
        t->debut = millis();
    }
    else {
        if ((unsigned long)(millis() - t->debut) >= t->interval) {
            t->debut = 0;
            return true;
        }
    }
    return false;
}

class Regul {
public:

    double sortiePID;
    double consigne;
    double Kp;
    double Ki;
    double Kd;
    double sortiePID_pc;
    bool autorisationForcage;
    int consigneForcage;
    double offset;
    PID pid;
    int startAddress;

    bool useOffset;

    double meanPIDOutput = 255;
    Regul() {

    }
    int save(int startAddress) {
        int add = startAddress;
        EEPROM.updateDouble(add, consigne); add += sizeof(double);
        EEPROM.updateDouble(add, Kp); add += sizeof(double);
        EEPROM.updateDouble(add, Ki); add += sizeof(double);
        EEPROM.updateDouble(add, Kd); add += sizeof(double);
        EEPROM.updateDouble(add, offset); add += sizeof(double);

        EEPROM.updateInt(add, autorisationForcage); add += sizeof(int);
        EEPROM.updateInt(add, consigneForcage); add += sizeof(int);

        EEPROM.updateInt(add, useOffset); add += sizeof(int);
        return add;
    }

    int load(int startAddress) {
        int add = startAddress;
        consigne = EEPROM.readDouble(add); add += sizeof(double);
        Kp = EEPROM.readDouble(add); add += sizeof(double);
        Ki = EEPROM.readDouble(add); add += sizeof(double);
        Kd = EEPROM.readDouble(add); add += sizeof(double);
        offset = EEPROM.readDouble(add); add += sizeof(double);

        autorisationForcage = EEPROM.readInt(add); add += sizeof(int);
        consigneForcage = EEPROM.readInt(add); add += sizeof(int);

        useOffset = EEPROM.readInt(add); add += sizeof(int);
        return add;
    }
};

class Meso {
public:
    byte id;
    byte pinDebitmetre;
    byte pinCmdHR;

    double debit;
    double temperature;
    double O2;

    Regul regulTemp;
    tempo tempoHRON, tempoHROFF;
    bool toggleHR;


    int startAddress;

    int lastState;

    Meso() {
    };
    Meso(byte _id, byte _pinDebitmetre, byte _pinCmdHR) {
        regulTemp = Regul();
        id = _id;
        pinDebitmetre = _pinDebitmetre;
        pinCmdHR = _pinCmdHR;

        debit = 0;
        lastState = LOW;
    };

    int load() {

        Serial.println("LOAD Start Address " + String(id) + ":" + String(startAddress));
        int add = startAddress;
        add = regulTemp.load(add);


        return add;

    };
    int save() {
        Serial.println("SAVE Start Address " + String(id) + ":" + String(startAddress));
        int add = startAddress;
        add = regulTemp.save(add);

        return add;

    };

    bool readFlow() {
        int currentState = digitalRead(pinDebitmetre); // Lire l'�tat actuel

        // D�tecter un changement d'�tat (montant)
        if (currentState == HIGH && lastState == LOW) {
            pulseCount++; // Incrementer le compteur d'impulsions
        }
        lastState = currentState; // Mettre � jour l'�tat

        // Calculer le d�bit toutes les secondes
        if (millis() - lastTime >= flowIntegrationDuration) {
            // Calculer le d�bit en litres par minute
            debit = (pulseCount / 10200.0) * (60000 / flowIntegrationDuration); // Pulses per litre * seconds per minute
            // Afficher le d�bit
            Serial.print("D�bit : ");
            Serial.print(debit);
            Serial.println(" L/min");
            // R�initialiser le compteur
            pulseCount = 0;
            lastTime = millis();
            return true;
        }
        return false;
        
    }


    int regulTemperature() {
        int dutyCycle = 0;
        regulTemp.pid.Compute();

        regulTemp.sortiePID_pc = (int)regulTemp.sortiePID;

        dutyCycle = regulTemp.sortiePID;
        //dutyCycle = 50;
        unsigned long cycleDuration = 10000;
        tempoHRON.interval = dutyCycle * cycleDuration / 100;
        tempoHROFF.interval = cycleDuration - tempoHRON.interval;;
        if (tempoHRON.interval == 0) toggleHR = false;
        else if (tempoHROFF.interval == 0) toggleHR = true;
        else if (toggleHR) {
            if (elapsed(&tempoHRON)) {
                tempoHROFF.debut = millis();
                toggleHR = false;
            }
        }
        else {
            if (elapsed(&tempoHROFF)) {
                tempoHRON.debut = millis();
                toggleHR = true;
            }
        }
        digitalWrite(pinCmdHR, toggleHR);
        return dutyCycle;
    }

    bool serializeData(uint32_t timeString, uint8_t sender, char* buffer) {
        //Serial.println("SENDDATA");
        //DynamicJsonDocument doc(512);

        StaticJsonDocument<512> doc;

        doc[scmd] = 3;
        doc[sPLCID] = String(sender);
        doc[sID] = String(id);
        doc[stemp] = serialized(String((int)(temperature * 100 + 0.5) / 100.0, 2));
        doc[soxy] = serialized(String((int)(O2 * 100 + 0.5) / 100.0));
        doc[sdebit] = serialized(String((int)(debit * 100 + 0.5) / 100.0));


        //Serial.print(F("CONDID:")); Serial.println(condID);
        //Serial.print(F("socketID:")); Serial.println(socketID);
       // doc[stime] = timeString;

        JsonObject regulT = doc.createNestedObject(rTemp);
        regulT[scons] = regulTemp.consigne;
        regulT["offset"] = regulTemp.offset;
        regulT[sPID_pc] = regulTemp.sortiePID_pc;

        regulT[sKp] = regulTemp.Kp;
        regulT[sKi] = regulTemp.Ki;
        regulT[sKd] = regulTemp.Kd;

        serializeJson(doc, buffer, bufferSize);
        return true;
    }

    bool serializeParams(uint32_t timeString, uint8_t sender, char* buffer) {
        StaticJsonDocument<300> doc;
        //Serial.println(F("SEND PARAMS"));

        doc[scmd] = 2;
        doc[sPLCID] = String(sender);
        doc[sID] = String(id);
        doc[stime] = timeString;

        JsonObject regulT = doc.createNestedObject(F("rTemp"));
        regulT[scons] = regulTemp.consigne;
        regulT["offset"] = regulTemp.offset;
        regulT[sKp] = regulTemp.Kp;
        regulT[sKi] = regulTemp.Ki;
        regulT[sKd] = regulTemp.Kd;
        if (this->regulTemp.autorisationForcage) regulT[saForcage] = "true";
        else regulT[saForcage] = "false";
        regulT[sconsForcage] = regulTemp.consigneForcage;

        serializeJson(doc, buffer, bufferSize);
    }

    void deserializeParams(StaticJsonDocument<jsonDocSize> doc) {
        Serial.println("DESERIALIAZE PARAMS");
        JsonObject regulT = doc[rTemp];

        regulTemp.consigne = regulT[scons]; // 24.2
        regulTemp.Kp = regulT[sKp]; // 2.1
        regulTemp.Ki = regulT[sKi]; // 2.1
        regulTemp.Kd = regulT[sKd]; // 2.1
        const char* regulTemp_autorisationForcage = regulT[saForcage];
        if (strcmp(regulTemp_autorisationForcage, "true") == 0 || strcmp(regulTemp_autorisationForcage, "True") == 0) regulTemp.autorisationForcage = true;
        else regulTemp.autorisationForcage = false;
        regulTemp.consigneForcage = regulT[sconsForcage]; // 2.1

        regulTemp.useOffset = regulT[F("useOffset")];
        regulTemp.offset = regulT[F("offset")];

        Serial.println("OFFSET:" + String(regulTemp.offset));

        Serial.println("consigne:" + String(regulTemp.consigne));
        save();


    }

};


