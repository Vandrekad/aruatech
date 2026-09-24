#include "modules/storage/storage.h"
#include <LittleFS.h>
#include "config.h"
#include "modules/utils/utils.h"
#include "modules/state/state.h"

bool initFileSystem() {
  if (!LittleFS.begin(true)) {
    Serial.println("Falha ao iniciar LittleFS.");
    return false;
  }
  Serial.println("LittleFS montado.");
  return true;
}

bool appendLineToFile(const char *path, const String &line) {
  File file = LittleFS.open(path, FILE_APPEND);
  if (!file) {
    Serial.printf("Erro abrindo %s para append.\n", path);
    return false;
  }
  file.println(line);
  file.close();
  return true;
}

bool readFileLines(const char *path, std::vector<String> &lines) {
  File file = LittleFS.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      lines.push_back(line);
    }
  }
  file.close();
  return true;
}

bool writeFileLines(const char *path, const std::vector<String> &lines) {
  File file = LittleFS.open(path, FILE_WRITE);
  if (!file) {
    Serial.printf("Erro abrindo %s para escrita.\n", path);
    return false;
  }
  for (const String &line : lines) {
    file.println(line);
  }
  file.close();
  return true;
}

bool bufferTelemetryLocal() {
  // Monta um snapshot JSON do estado atual e grava no buffer LittleFS.
  // Mesmo formato que sendTelemetryToRpi() usa, para o daemon do RPi drenar.
  JsonDocument doc;
  doc["type"] = "telemetry";
  doc["ts"] = (uint32_t)(millis() / 1000);
  doc["lat"] = currentLat;
  doc["lon"] = currentLon;
  doc["fix"] = hasGpsFix;
  doc["hdg"] = currentHeading;
  doc["obs"] = obsDist;
  doc["bat"] = batteryMv;
  doc["thrust_l"] = thrustL;
  doc["thrust_r"] = thrustR;
  doc["state"] = navStateToString(currentState);
  doc["mission_id"] = activeMissionId;
  doc["active_leg"] = activeLeg;
  doc["progress"] = routeProgress;
  String line;
  serializeJson(doc, line);
  return appendLineToFile(telemetryBufferPath, line);
}

bool bufferPathPointOffline(double lat, double lon, unsigned long ts) {
  JsonDocument pointDoc;
  pointDoc["lat"] = lat;
  pointDoc["lon"] = lon;
  pointDoc["ts"] = ts;
  String line;
  serializeJson(pointDoc, line);
  return appendLineToFile(pathBufferPath, line);
}
