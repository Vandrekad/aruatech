#pragma once

#include <Arduino.h>

bool initHardwareSensors();
void initMotors();
bool initCompass();
void stopMotors();
void updateMotorOutputs();
void readUltrasonic();
bool readCompass();
void readGPS();
void updateSensorValues();

// Imprime um bloco de status estruturado e legível (sensores, GPS, sonar, link
// RPi, rumo). Chamado periodicamente pelo loop principal. Reaproveita os dados
// já lidos — não faz novas leituras de hardware.
void printStatusBlock();
