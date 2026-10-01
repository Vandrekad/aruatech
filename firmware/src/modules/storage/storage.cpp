#include "modules/storage/storage.h"
#include <LittleFS.h>
#include "config.h"
#include "modules/utils/utils.h"
#include "modules/state/state.h"

// Flag de saúde do filesystem. Só vira true se o LittleFS montar de fato. Todas
// as funções de escrita/leitura consultam este flag antes de tocar no LittleFS,
// para nunca cair no lfs_alloc com block_count inválido (que gera panic
// IntegerDivideByZero e boot loop quando o FS está corrompido).
static bool fsReady = false;

bool isFileSystemReady() {
  return fsReady;
}

bool initFileSystem() {
  // 1ª tentativa: montar formatando no erro (comportamento padrão). Em FS
  // corrompido, porém, o begin(true) pode montar um superbloco inválido em vez
  // de reformatar — por isso a checagem de sanidade + reformatação explícita
  // abaixo.
  if (LittleFS.begin(true)) {
    // Sanidade: um FS válido reporta totalBytes > 0. Se vier 0, o superbloco
    // está inconsistente (block_count zerado) — força format + remonta.
    if (LittleFS.totalBytes() > 0) {
      Serial.printf("LittleFS montado (%u bytes).\n", (unsigned)LittleFS.totalBytes());
      fsReady = true;
      return true;
    }
    Serial.println("LittleFS montou com totalBytes=0 (superbloco invalido) — reformatando...");
    LittleFS.end();
  } else {
    Serial.println("Falha ao montar LittleFS — tentando reformatar...");
  }

  // Reformatação explícita e remontagem.
  if (LittleFS.format() && LittleFS.begin(false) && LittleFS.totalBytes() > 0) {
    Serial.printf("LittleFS reformatado e montado (%u bytes).\n", (unsigned)LittleFS.totalBytes());
    fsReady = true;
    return true;
  }

  Serial.println("ERRO: LittleFS indisponivel — buffering local DESLIGADO (navegacao segue normal).");
  fsReady = false;
  return false;
}

bool appendLineToFile(const char *path, const String &line) {
  if (!fsReady) {
    return false;  // FS indisponível — no-op seguro (evita panic no lfs_alloc).
  }
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
  if (!fsReady) {
    return false;
  }
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
  if (!fsReady) {
    return false;
  }
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
