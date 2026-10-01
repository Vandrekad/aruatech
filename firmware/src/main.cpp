#include <Arduino.h>
#include "config.h"
#include "modules/log/log.h"
#include "modules/storage/storage.h"
#include "modules/sensors/sensors.h"
#include "modules/commands/commands.h"
#include "modules/navigation/navigation.h"
#include "modules/state/state.h"
#include "modules/link/serial_link.h"
#include "modules/tests/tests.h"
#include "modules/display/display.h"

// Flag para ativar o app de testes de componentes (desliga missão normal)
static const bool enableComponentTestApp = false;

void setup() {
  // UART0/USB: agora é o TRANSPORTE do link com o RPi (dados JSON) E, só na
  // bancada (DEBUG_USB_CONSOLE=1), o console de debug. Em produção os logs ficam
  // silenciados (ver log.h) para não sujar o JSON. O begin fica aqui, cedo,
  // porque a porta é usada tanto pelos logs de boot quanto pelo link.
  Serial.begin(115200);
  delay(100);

  DBG_PRINTLN("=== USV-AM Firmware v1.0 ===");
  DBG_PRINTLN("Inicializando...");

  // Motivo do último reset — confirma brownout (queda de tensão) vs poweron normal.
  esp_reset_reason_t rr = esp_reset_reason();
  const char *rrStr =
      (rr == ESP_RST_BROWNOUT) ? "BROWNOUT (queda de tensao — energia instavel!)" :
      (rr == ESP_RST_POWERON)  ? "POWERON (ligar normal)" :
      (rr == ESP_RST_PANIC)    ? "PANIC (crash de software)" :
      (rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT) ? "WATCHDOG" :
      (rr == ESP_RST_SW)       ? "SOFTWARE" : "OUTRO";
  DBG_PRINTF("[RESET] motivo=%d (%s)\n", (int)rr, rrStr);

  // 1. Filesystem primeiro (necessário para buffering offline local)
  if (!initFileSystem()) {
    DBG_PRINTLN("ERRO CRÍTICO: falha ao montar LittleFS.");
  }

  // 2. Sensores de hardware
  initHardwareSensors();

  // 2b. OLED SSD1306 (HUD de status). Usa o barramento I2C já iniciado nos
  //     sensores. Opcional: se ausente, o firmware segue normal sem display.
  initDisplay();

  // 3. Link serial com o Raspberry Pi 4.
  //    Arquitetura dual: o RPi é dono da camada de nuvem (WiFi/Firebase). O ESP32
  //    NÃO fala WiFi/Firebase — publica tudo por UART e navega de forma autônoma
  //    mesmo sem o RPi. Comandos (set_destination/emergency_stop) chegam só por aqui.
  initSerialLink();

  // 3b. Verificação e calibração dos sensores no BOOT. O self-test sempre roda; a
  //     calibração da bússola só roda se NAO houver calibração salva (senão o boot
  //     travaria 25s girando). Em bancada, use os comandos serial 'cal'/'test'.
  runCalibrationAndCheck(false);

  // 4. Testes (se habilitados)
  if (enableComponentTestApp) {
    runFirmwareComponentTests();
  }

  DBG_PRINTLN("Firmware inicializado (modo dual: RPi = nuvem, ESP32 = controle).");
}

void loop() {
  // ── Comandos de manutenção pelo monitor serial (SÓ na bancada) ──
  // A UART0/USB agora é o barramento de DADOS do RPi. Ler comandos de texto
  // ('cal'/'test') daqui roubaria bytes do stream JSON e os corromperia. Por
  // isso o leitor só existe quando DEBUG_USB_CONSOLE=1 (cabo no PC, RPi
  // desconectado). Em produção, use o BOTÃO FÍSICO (GPIO18) para cal/self-test.
#if DEBUG_USB_CONSOLE
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.equalsIgnoreCase("cal") || cmd.equalsIgnoreCase("calibrar")) {
      runCalibrationAndCheck(true);   // força recalibração
    } else if (cmd.equalsIgnoreCase("test") || cmd.equalsIgnoreCase("selftest")) {
      displayShowSelfTest();          // mostra "VERIFICANDO..." no OLED
      bool ok = runSensorSelfTest();
      displayShowCalResult(ok, false);
    }
  }
#endif

  // ── Link com o RPi (processa comandos recebidos, não-bloqueante) ──
  processSerialLink();

  // ── Timers do loop principal ──
  static unsigned long sensorPrevMs = 0;
  static unsigned long telemetryPrevMs = 0;
#if DEBUG_USB_CONSOLE
  static unsigned long statusLogPrevMs = 0;
#endif

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
      DBG_PRINTF("[READY] Sistema %s para navegar (gps_fix=%s).\n",
                    ready ? "PRONTO" : "NAO pronto — motores desligados",
                    ready ? "SIM" : "NAO");
      wasReady = ready;
    }

    updateLOSControl();
    updateMotorOutputs();
  }

  // ── Publicação de telemetria (a cada TELEMETRY_INTERVAL_MS) ──
  // O ESP32 é PROATIVO: emite telemetria SEMPRE pela USB, mesmo antes de ter
  // ouvido o RPi. Isso resolve o impasse em que o ESP32 só falava depois de
  // isRpiPresent()==true, mas isRpiPresent() só vira true depois do ESP32 falar
  // (ou de um comando do RPi) — se o 1º comando se perdia no auto-reset do boot,
  // os dois ficavam mudos esperando um ao outro. Agora o RPi detecta o ESP32
  // assim que o cabo USB sobe, pela própria telemetria periódica.
  //
  // Persistência: enquanto o RPi está AUSENTE, bufferizamos também em LittleFS,
  // para o daemon drenar o histórico ao reconectar. O envio pela serial é barato
  // e inofensivo mesmo sem ninguém ouvindo (bytes caem no vazio).
  if (now - telemetryPrevMs >= TELEMETRY_INTERVAL_MS || telemetryPrevMs == 0) {
    telemetryPrevMs = now;
    sendTelemetryToRpi();              // sempre: heartbeat que torna o ESP32 visível
    if (!isRpiPresent()) {
      bufferTelemetryLocal();          // além do envio, persiste local até reconectar
    }
  }

  // ── Log de status estruturado (mesma cadência do antigo [GPS-DIAG]: ~3s) ──
  // SÓ na bancada: esse bloco escreve dezenas de linhas por chamada e, em
  // produção, inundaria o stream JSON do RPi na UART0/USB. O OLED (HUD) é o
  // status visível em operação; este bloco serve ao debug pelo monitor do PC.
#if DEBUG_USB_CONSOLE
  if (now - statusLogPrevMs >= STATUS_LOG_INTERVAL_MS || statusLogPrevMs == 0) {
    statusLogPrevMs = now;
    printStatusBlock();
  }
#endif

  // ── HUD no OLED (cadência própria, mais rápida que o log serial) ──
  static unsigned long displayPrevMs = 0;
  if (now - displayPrevMs >= OLED_UPDATE_MS || displayPrevMs == 0) {
    displayPrevMs = now;
    updateDisplayHUD();
  }

  delay(10);
}
