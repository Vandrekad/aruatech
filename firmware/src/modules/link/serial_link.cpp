#include "modules/link/serial_link.h"
#include <ArduinoJson.h>
#include "config.h"
#include "modules/state/state.h"
#include "modules/navigation/navigation.h"
#include "modules/commands/commands.h"

// Serial2 do ESP32 é dedicada ao link com o RPi (GPS foi movido para os pinos
// GPS_RX_PIN/GPS_TX_PIN e usa Serial1 — ver sensors.cpp).
static HardwareSerial &RpiSerial = Serial2;

// Buffer de linha com limite fixo (evita crescimento sem limite / memory leak).
#define RPI_LINE_MAX 256
static char lineBuffer[RPI_LINE_MAX + 1];
static uint16_t linePos = 0;

static unsigned long lastRpiMessageMs = 0;
static bool rpiEverSeen = false;

void initSerialLink() {
  RpiSerial.begin(RPI_LINK_BAUD, SERIAL_8N1, RPI_LINK_RX_PIN, RPI_LINK_TX_PIN);
  linePos = 0;
  lastRpiMessageMs = 0;
  rpiEverSeen = false;
  Serial.println("[LINK] Serial link com RPi inicializado (Serial2 @115200).");
}

bool isRpiPresent() {
  if (!rpiEverSeen) {
    return false;
  }
  return (millis() - lastRpiMessageMs) < RPI_LINK_TIMEOUT_MS;
}

static void sendJsonLine(JsonDocument &doc) {
  serializeJson(doc, RpiSerial);
  RpiSerial.print('\n');
}

static void sendAck(const String &commandId, bool ok) {
  JsonDocument doc;
  doc["type"] = "ack";
  doc["command_id"] = commandId;
  doc["ok"] = ok;
  sendJsonLine(doc);
}

void sendTelemetryToRpi() {
  JsonDocument doc;
  doc["type"] = "telemetry";
  doc["ts"] = (uint32_t)(millis() / 1000);
  doc["lat"] = currentLat;
  doc["lon"] = currentLon;
  doc["hdg"] = currentHeading;
  doc["obs"] = obsDist;
  doc["bat"] = batteryMv;
  doc["thrust_l"] = thrustL;
  doc["thrust_r"] = thrustR;
  doc["state"] = navStateToString(currentState);
  doc["mission_id"] = activeMissionId;
  doc["active_leg"] = activeLeg;
  doc["progress"] = routeProgress;
  sendJsonLine(doc);
}

void sendEventToRpi(const char *event, double value) {
  JsonDocument doc;
  doc["type"] = "event";
  doc["event"] = event;
  doc["value"] = value;
  doc["ts"] = (uint32_t)(millis() / 1000);
  sendJsonLine(doc);
}

// Converte um comando JSON recebido do RPi numa DroneCommand e despacha.
static void handleRpiLine(const char *line) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    Serial.printf("[LINK] JSON inválido do RPi: %s\n", err.c_str());
    return;
  }

  const char *cmd = doc["cmd"] | "";

  // Mensagens de keep-alive / consulta não são comandos de navegação.
  if (strcmp(cmd, "ping") == 0) {
    JsonDocument pong;
    pong["type"] = "pong";
    sendJsonLine(pong);
    return;
  }
  if (strcmp(cmd, "request_telemetry") == 0) {
    sendTelemetryToRpi();
    return;
  }

  // Comandos de navegação → reusa a lógica existente de commands.cpp.
  DroneCommand command;
  command.commandId = String((const char *)(doc["command_id"] | ""));
  command.type = String(cmd);
  command.missionId = String((const char *)(doc["mission_id"] | ""));
  command.targetLat = doc["lat"] | 0.0;
  command.targetLon = doc["lon"] | 0.0;
  command.issuedAt = doc["issued_at"] | 0UL;

  if (command.commandId.length() == 0) {
    Serial.println("[LINK] Comando sem command_id — ignorado.");
    sendAck("", false);
    return;
  }

  // Deduplicação: mesmo command_id não é reprocessado (igual ao path RTDB).
  if (command.commandId == lastCommandId) {
    sendAck(command.commandId, true);  // já aplicado; ack idempotente
    return;
  }

  if (command.type == "set_destination" || command.type == "emergency_stop") {
    handleCommand(command);  // handleCommand já atualiza lastCommandId
    sendAck(command.commandId, true);
  } else {
    Serial.printf("[LINK] Comando desconhecido do RPi: %s\n", cmd);
    sendAck(command.commandId, false);
  }
}

void processSerialLink() {
  while (RpiSerial.available()) {
    char c = (char)RpiSerial.read();

    if (c == '\n' || c == '\r') {
      if (linePos > 0) {
        lineBuffer[linePos] = '\0';
        lastRpiMessageMs = millis();
        rpiEverSeen = true;
        handleRpiLine(lineBuffer);
        linePos = 0;
      }
    } else if (c != '\0') {
      if (linePos < RPI_LINE_MAX) {
        lineBuffer[linePos++] = c;
      } else {
        // Linha longa demais / corrompida — descarta e ressincroniza.
        Serial.println("[LINK] Linha excedeu o buffer — descartada.");
        linePos = 0;
      }
    }
  }
}
