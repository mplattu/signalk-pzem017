/*
  ModbusRTU ESP8266/ESP32
  Read multiple coils from slave device example

  (c)2019 Alexander Emelianov (a.m.emelianov@gmail.com)
  https://github.com/emelianov/modbus-esp8266

  modified 13 May 2020
  by brainelectronics

  This code is licensed under the BSD New License. See LICENSE.txt for more info.
*/

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ModbusRTU.h>         // https://github.com/emelianov/modbus-esp8266
#include <SoftwareSerial.h>
#include <EspSigK.h>

// ESP8266 pins
#define LED LED_BUILTIN
#define D6 12
#define D7 13
#define D8 15

// PZEM-017 Shunt Range
#define PZEM_SHUNT_100A 0x0000
#define PZEM_SHUNT_50A  0x0001
#define PZEM_SHUNT_200A 0x0002
#define PZEM_SHUNT_300A 0x0003

// My settings
#define SERIAL_RX D8
#define SERIAL_TX D7
#define SERIAL_CONTROL D6

#include "../lib/settings.cpp"

WiFiClient wiFiClient;
EspSigK sigK(wifiHostname, wifiSsid, wifiPass, &wiFiClient);

EspSoftwareSerial::UART swSerial;

ModbusRTU modbus;
int modbusLastStatusCode = -1;

void ledOn() {
  digitalWrite(LED, LOW);
}

void ledOff() {
  digitalWrite(LED, HIGH);
}

void ledError() {
  for (uint n=0; n < 5; n++) {
    ledOn();
    sigK.safeDelay(100);
    ledOff();
    sigK.safeDelay(100);
  }
}

bool otaActive() {
  return (strcmp(otaPass, (const char *)"") != 0);
}

void otaSetup(const char * hostname, const char * otaPassword) {
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.setPassword(otaPassword);
  Serial.print("OTA password: ");
  Serial.print(otaPassword);
  Serial.print(" ");

  ArduinoOTA.onStart([]() {
    Serial.println("Staring OTA update...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA update done");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
}

bool modbusStatusCallback(Modbus::ResultCode event, uint16_t transactionId, void* data) {
  // Serial.printf_P("Request result: 0x%02X, Mem: %d\n", event, ESP.getFreeHeap());

  modbusLastStatusCode = uint(event);

  return true;
}

void setup() {
  Serial.begin(115200);

  pinMode(LED, OUTPUT);
  ledOn();

  swSerial.begin(9600, SWSERIAL_8N1, SERIAL_RX, SERIAL_TX, false);
  
  modbus.begin(&swSerial, SERIAL_CONTROL);
  modbus.client();

  Serial.print("Setting shut value...");

  // The write resulted always 0xE4 (timeout), so no callback here
  modbus.writeHreg(PZEM_MODBUS_ID, 0x0003, PZEM_SHUNT);
  modbus.task();

  Serial.println("OK");

  Serial.print("Starting signalK client...");
  sigK.setServerHost(signalKHostIP);
  sigK.setServerPort(signalKHostPort);
  sigK.setPrintDebugSerial(true);
  sigK.setWiFiConnectTimeout(20000);
  sigK.begin();
  Serial.println("OK");

  if (otaActive()) {
    Serial.print("Initialising OTA...");
    otaSetup(wifiHostname.c_str(), otaPass);
    Serial.println("OK");
  }

  Serial.println("Setup finished");
  ledOff();
}

uint16_t registers[2];
float voltage;
float current;
String myIP;

void sigKhandler() {
  if (otaActive()) {
    ArduinoOTA.handle();
  }
}

void loop() {
  if (!modbus.slave()) {
    modbus.readIreg(PZEM_MODBUS_ID, 0, registers, 2, modbusStatusCallback);
    if (modbusLastStatusCode == 0) {
        voltage = registers[0] / 100.0;
        current = registers[1] / 100.0;
        myIP = WiFi.localIP().toString();

        Serial.print("Voltage: ");
        Serial.print(voltage);
        Serial.print("V, Current: ");
        Serial.print(current);
        Serial.print("A, IP: ");
        Serial.println(myIP);

        sigK.addDeltaValue(signalKPathVoltage, voltage);
        sigK.addDeltaValue(signalKPathCurrent, current);
        sigK.addDeltaValue(signalKPathIp, myIP);
        sigK.sendDelta();
    }
    else {
        Serial.printf_P("Modbus error code: 0x%02X\n", modbusLastStatusCode);
        ledError();
    }
  }
  modbus.task();
  sigK.handle();

  ledOn();
  sigK.safeDelay(signalKBetweenDeltas / 2, &sigKhandler);
  ledOff();
  sigK.safeDelay(signalKBetweenDeltas / 2, &sigKhandler);
}
