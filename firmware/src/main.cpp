#include <Arduino.h>
#include "config.h"
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

  // Motivo do último reset — confirma brownout (queda de tensão) vs poweron normal.
  esp_reset_reason_t rr = esp_reset_reason();
  const char *rrStr =
      (rr == ESP_RST_BROWNOUT) ? "BROWNOUT (queda de tensao — energia instavel!)" :
      (rr == ESP_RST_POWERON)  ? "POWERON (ligar normal)" :
      (rr == ESP_RST_PANIC)    ? "PANIC (crash de software)" :
      (rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT) ? "WATCHDOG" :
      (rr == ESP_RST_SW)       ? "SOFTWARE" : "OUTRO";
  Serial.printf("[RESET] motivo=%d (%s)\n", (int)rr, rrStr);

  // 1. Filesystem primeiro (necessário para buffering offline local)
  if (!initFileSystem()) {
    Serial.println("ERRO CRÍTICO: falha ao montar LittleFS.");
  }

  // 2. Sensores de hardware
  initHardwareSensors();

  // 3. Link serial com o Raspberry Pi 4.
  //    Arquitetura dual: o RPi é dono da camada de nuvem (WiFi/Firebase). O ESP32
  //    NÃO fala WiFi/Firebase — publica tudo por UART e navega de forma autônoma
  //    mesmo sem o RPi. Comandos (set_destination/emergency_stop) chegam só por aqui.
  initSerialLink();

  // 4. Testes (se habilitados)
  if (enableComponentTestApp) {
    runFirmwareComponentTests();
  }

  Serial.println("Firmware inicializado (modo dual: RPi = nuvem, ESP32 = controle).");
}

void loop() {
  // ── Link com o RPi (processa comandos recebidos, não-bloqueante) ──
  processSerialLink();

  // ── Timers do loop principal ──
  static unsigned long sensorPrevMs = 0;
  static unsigned long telemetryPrevMs = 0;
  static unsigned long statusLogPrevMs = 0;

  unsigned long now = millis();

  // ── Atualização de sensores e controle (alta frequência: ~100ms) ──
  // FIX #11: Separar leitura de sensores + controle da publicação de telemetria
  if (now - sensorPrevMs >= 100 || sensorPrevMs == 0) {
    sensorPrevMs = now;
    updateSensorValues();

    // Log de transição de prontidão: avisa quando o sistema fica pronto para
    // navegar (GPS fix obtido) — enquanto não, motores ficam desligados.
    static bool wasReady = false;
    bool ready = isSystemReady();
    if (ready != wasReady) {
      Serial.printf("[READY] Sistema %s para navegar (gps_fix=%s).\n",
                    ready ? "PRONTO" : "NAO pronto — motores desligados",
                    ready ? "SIM" : "NAO");
      wasReady = ready;
    }

    updateLOSControl();
    updateMotorOutputs();
  }

  // ── Publicação de telemetria (a cada TELEMETRY_INTERVAL_MS) ──
  // Sempre via UART para o RPi. Se o RPi não estiver presente, o ESP32 continua
  // navegando de forma autônoma e bufferiza a telemetria em LittleFS local; ao
  // reconectar o RPi, o daemon faz o flush do buffer.
  if (now - telemetryPrevMs >= TELEMETRY_INTERVAL_MS || telemetryPrevMs == 0) {
    telemetryPrevMs = now;
    if (isRpiPresent()) {
      sendTelemetryToRpi();
    } else {
      bufferTelemetryLocal();
    }
  }

  // ── Log de status estruturado (mesma cadência do antigo [GPS-DIAG]: ~3s) ──
  if (now - statusLogPrevMs >= STATUS_LOG_INTERVAL_MS || statusLogPrevMs == 0) {
    statusLogPrevMs = now;
    printStatusBlock();
  }

  delay(10);
}
