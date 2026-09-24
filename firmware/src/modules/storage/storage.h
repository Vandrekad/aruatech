#pragma once

#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>

// Armazenamento local (LittleFS). Arquitetura dual: o ESP32 NÃO fala Firebase.
// Quando o RPi está ausente, a telemetria é bufferizada localmente aqui; o RPi,
// ao reconectar, drena o buffer via UART e o publica no Firebase.

bool initFileSystem();
bool appendLineToFile(const char *path, const String &line);
bool readFileLines(const char *path, std::vector<String> &lines);
bool writeFileLines(const char *path, const std::vector<String> &lines);

// Bufferiza um snapshot da telemetria atual (estado + sensores) em LittleFS,
// como uma linha JSON. Usada quando o RPi não está presente.
bool bufferTelemetryLocal();

// Bufferiza um ponto de trajetória (lat/lon/ts) em LittleFS.
bool bufferPathPointOffline(double lat, double lon, unsigned long ts);
