#pragma once
/**
 * log.h — Logging de debug que convive com o link USB do RPi.
 *
 * CONTEXTO: o link de dados com o Raspberry Pi usa a Serial USB nativa (UART0 =
 * `Serial`). Como os logs de debug saíam nessa MESMA porta, eles sujariam o
 * stream JSON-lines que o RPi parseia. Portanto:
 *
 *   - DEBUG_USB_CONSOLE == 0 (PRODUÇÃO, ESP32 ligado ao RPi):
 *       Os macros DBG_* viram no-op. Só o protocolo JSON trafega na Serial.
 *   - DEBUG_USB_CONSOLE == 1 (BANCADA, cabo no PC):
 *       Os macros DBG_* escrevem no `Serial` normalmente. NÃO conecte o RPi
 *       nesse modo — os logs quebrariam o parser dele.
 *
 * O protocolo JSON (serial_link.cpp) escreve DIRETO no `Serial`, nunca por DBG_*,
 * então a telemetria/acks sempre saem, em qualquer modo.
 *
 * Uso: troque `Serial.println("...")` por `DBG_PRINTLN("...")`, `Serial.printf`
 * por `DBG_PRINTF`, `Serial.print` por `DBG_PRINT`.
 */

#include <Arduino.h>
#include "config.h"

#if DEBUG_USB_CONSOLE
  #define DBG_BEGIN(baud)   Serial.begin(baud)
  #define DBG_PRINT(...)    Serial.print(__VA_ARGS__)
  #define DBG_PRINTLN(...)  Serial.println(__VA_ARGS__)
  #define DBG_PRINTF(...)   Serial.printf(__VA_ARGS__)
#else
  // No-ops em produção. O `do{}while(0)` mantém a sintaxe de statement idêntica
  // (ponto-e-vírgula no fim) sem gerar código.
  #define DBG_BEGIN(baud)   do {} while (0)
  #define DBG_PRINT(...)    do {} while (0)
  #define DBG_PRINTLN(...)  do {} while (0)
  #define DBG_PRINTF(...)   do {} while (0)
#endif
