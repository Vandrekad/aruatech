#include <Arduino.h>
#include "config.h"
#include "modules/net/wifi_manager.h"
#include "modules/net/firebase_manager.h"
#include "modules/storage/storage.h"
#include "modules/sensors/sensors.h"
#include "modules/commands/commands.h"
#include "modules/navigation/navigation.h"
#include "modules/state/state.h"
#include "modules/link/serial_link.h"
#include "modules/tests/tests.h"

// Flag para ativar o app de testes de componentes (desliga missão normal)
static const bool enableComponentTestApp = false;

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("=== USV-AM Firmware v1.0 ===");
  Serial.println("Inicializando...");

  // 1. Filesystem primeiro (necessário para buffering offline)
  if (!initFileSystem()) {
    Serial.println("ERRO CRÍTICO: falha ao montar LittleFS.");
  }

  // 2. Sensores de hardware
  initHardwareSensors();

  // 2b. Link serial com o Raspberry Pi 4 (aditivo — ESP32 opera sem ele)
  initSerialLink();

  // 3. WiFi (não-bloqueante após timeout)
  setupWiFi();
  unsigned long startMs = millis();
  Serial.println("Aguardando conexão Wi-Fi...");
  while (!isWiFiConnected() && millis() - startMs < WIFI_CONNECT_TIMEOUT_MS) {
    manageWiFi();
    delay(200);
  }

  // 4. Firebase (se WiFi disponível)
  if (isWiFiConnected()) {
    Serial.println("Wi-Fi conectado. Inicializando Firebase...");
    setupFirebase();
  } else {
    Serial.println("WiFi não disponível. Operando em modo offline.");
  }

  // 5. Testes (se habilitados)
  if (enableComponentTestApp) {
    runFirmwareComponentTests();
  }

  Serial.println("Firmware inicializado. Entrando em loop principal.");
}

void loop() {
  // ── Link com o RPi (processa comandos recebidos, não-bloqueante) ──
  processSerialLink();

  // ── Gerenciamento de conectividade ──
  manageWiFi();

  if (isWiFiConnected() && !firebaseInitialized) {
    Serial.println("Wi-Fi reconectado. Inicializando Firebase...");
    setupFirebase();
  }

  // ── Timers do loop principal ──
  static unsigned long sensorPrevMs = 0;
  static unsigned long telemetryPrevMs = 0;
  static unsigned long statusPrevMs = 0;
  static unsigned long commandPrevMs = 0;

  unsigned long now = millis();

  // ── Atualização de sensores e controle (alta frequência: ~100ms) ──
  // FIX #11: Separar leitura de sensores + controle da publicação de telemetria
  if (now - sensorPrevMs >= 100 || sensorPrevMs == 0) {
    sensorPrevMs = now;
    updateSensorValues();
    updateLOSControl();
    updateMotorOutputs();
  }

  // ── Publicação de telemetria (a cada TELEMETRY_INTERVAL_MS) ──
  if (now - telemetryPrevMs >= TELEMETRY_INTERVAL_MS || telemetryPrevMs == 0) {
    telemetryPrevMs = now;

    // Sempre atualiza o snapshot local (sensores + estado) e envia ao RPi se presente.
    // Quando o RPi está presente, ELE publica no Firebase — o ESP32 não escreve
    // direto para evitar escrita duplicada (flag RPI_PRESENT lógica na F2).
    if (isRpiPresent()) {
      sendTelemetryToRpi();
    } else {
      // Autonomia: sem RPi, o ESP32 publica direto (ou bufferiza offline).
      if (!publishTelemetry()) {
        Serial.println("Aviso: publicação de telemetria falhou.");
      }
    }
  }

  // ── Operações Firebase diretas: SÓ quando o RPi NÃO está presente ──
  // Com o RPi presente, ele é dono da camada Firebase (comandos chegam via UART,
  // status/telemetria são publicados por ele). Isso evita escrita duplicada.
  if (!isRpiPresent() && isWiFiConnected() && Firebase.ready()) {
    // Flush de buffers offline na reconexão
    if (needFlushBuffers) {
      if (flushOfflineBuffers()) {
        needFlushBuffers = false;
        Serial.println("Buffers offline enviados com sucesso.");
      }
      // Se falhou, tentará novamente no próximo ciclo
    }

    // Polling de comandos
    if (now - commandPrevMs >= COMMAND_INTERVAL_MS || commandPrevMs == 0) {
      commandPrevMs = now;
      processCommand();
    }

    // Atualização de status
    if (now - statusPrevMs >= STATUS_INTERVAL_MS || statusPrevMs == 0) {
      statusPrevMs = now;
      updateStatus();
    }
  } else if (!isWiFiConnected()) {
    needFlushBuffers = true;
  }

  delay(10);
}
