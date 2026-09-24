#pragma once

#include <Arduino.h>

void runFirmwareComponentTests();
bool testLittleFS();
bool testGPSParsing();
bool testCompassSensor();
bool testUltrasonicSensor();
bool testMotorOutput();
bool testRouteGeneration();
bool testOfflineBuffering();
