#include "modules/link/serial_link.h"
#include <ArduinoJson.h>
#include "config.h"
#include "modules/log/log.h"
#include "modules/state/state.h"
#include "modules/navigation/navigation.h"
#include "modules/commands/commands.h"

// O link com o RPi usa a Serial USB NATIVA (UART0 = `Serial`): um único cabo USB
// liga o ESP32 ao RPi, alimentando-o (5V do RPi) e trafegando os dados. No RPi
// esse ESP32 aparece como /dev/ttyUSB1. Os GPIO16/17 (antigo Serial2) ficam
// livres. Em produção os logs de debug ficam silenciados (ver log.h) para não
// sujar o stream JSON — só a telemetria/acks saem nesta porta.
static HardwareSerial &RpiSerial = Serial;

// Buffer de linha com limite fixo (evita crescimento sem limite / memory leak).
#define RPI_LINE_MAX 256
static char lineBuffer[RPI_LINE_MAX + 1];
static uint16_t linePos = 0;

static unsigned long lastRpiMessageMs = 0;
static bool rpiEverSeen = false;

void initSerialLink() {
  // UART0/USB já foi iniciado em setup() (Serial.begin). Não re-inicializamos a
  // porta aqui para não derrubar o console; apenas garantimos o baud do link.
  // (Se algum dia o link mudar de porta, trocar RpiSerial e o begin aqui.)
  RpiSerial.begin(RPI_LINK_BAUD);
  linePos = 0;
  lastRpiMessageMs = 0;
  rpiEverSeen = false;
  DBG_PRINTLN("[LINK] Link com RPi inicializado (USB/UART0 @115200).");
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
    DBG_PRINTF("[LINK] JSON invalido do RPi: %s\n", err.c_str());
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
    DBG_PRINTLN("[LINK] Comando sem command_id — ignorado.");
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
    DBG_PRINTF("[LINK] Comando desconhecido do RPi: %s\n", cmd);
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
        DBG_PRINTLN("[LINK] Linha excedeu o buffer — descartada.");
        linePos = 0;
      }
    }
  }
}
