#pragma once

#include <Arduino.h>

// Driver do OLED SSD1306 (128x64, I2C 0x3C) — HUD de status do USV.
// Reaproveita o barramento Wire já iniciado em initHardwareSensors().

// Inicializa o display. Retorna true se o SSD1306 respondeu no endereço I2C.
// Se falhar, o resto do firmware segue normal (display é opcional).
bool initDisplay();

// True somente se o OLED inicializou. Enquanto false, updateDisplayHUD() é no-op.
bool isDisplayReady();

// Redesenha o HUD com o estado atual (rumo, fix, obstáculos L/R, nav, link).
// Chamar periodicamente do loop (cadência OLED_UPDATE_MS).
void updateDisplayHUD();

// ── Feedback de calibração / self-test no OLED ──
// Tela "VERIFICANDO SENSORES" durante o self-test.
void displayShowSelfTest();
// Tela de calibração em andamento: instrui a girar e mostra o tempo restante
// e a amplitude capturada. Chamada repetidamente durante o giro.
void displayShowCalibrating(int secondsLeft, long xSpan, long ySpan);
// Tela de RESULTADO: ok=true -> "OK", false -> "FALHA". isCal distingue
// "CALIBRACAO" de "SELF-TEST" no título. Mostrada por alguns segundos.
void displayShowCalResult(bool ok, bool isCal);
