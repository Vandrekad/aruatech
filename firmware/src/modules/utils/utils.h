#pragma once

#include <Arduino.h>
#include <vector>

double deg2rad(double deg);
double wrapAngleDeg(double angle);
double headingErrorDeg(double desired, double current);
String computeSHA256Hex(const String &input);
double nmeaToDecimal(const String &field, char hemisphere);
String computeLocalPathHash(const std::vector<String> &lines);
