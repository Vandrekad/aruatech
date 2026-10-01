#include "modules/display/display.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "modules/state/state.h"
#include "modules/navigation/navigation.h"
#include "modules/link/serial_link.h"

// -1 = compartilha o Wire já iniciado (não faz reset por pino dedicado).
static Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
static bool displayReady = false;

bool isDisplayReady() {
  return displayReady;
}

bool initDisplay() {
  // SSD1306_SWITCHCAPVCC: gera a tensão do painel a partir do 3.3V.
  // O segundo arg é o endereço I2C; reset=false pois compartilhamos o barramento.
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, false, false)) {
    Serial.println("[OLED] SSD1306 nao respondeu — HUD desligado.");
    displayReady = false;
    return false;
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("USV-AM v1.0"));
  display.println(F("Init..."));
  display.display();
  displayReady = true;
  Serial.printf("[OLED] SSD1306 inicializado (%dx%d, HUD ativo).\n",
                OLED_WIDTH, OLED_HEIGHT);
  return true;
}

// Rótulo CURTO do estado de navegação para caber na faixa amarela do topo.
// (navStateToString devolve nomes longos tipo "IDLE_HOLDING_POSITION".)
static const char* navShort(NavState s) {
  switch (s) {
    case IDLE_HOLDING_POSITION: return "HOLD";
    case NAVIGATING_TO_GOAL:    return "NAV>";
    case OBSTACLE_AVOIDANCE:    return "!OBST";
    case RETURNING_TO_HOME:     return "RTH";
    case OFFLINE_NAVIGATION:    return "OFFL";
    default:                    return "?";
  }
}

void updateDisplayHUD() {
  if (!displayReady) {
    return;  // OLED ausente — no-op.
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ── Faixa amarela (topo ~16px): título grande e curto ──
  // Painéis 0.96" two-color têm as ~16 linhas de cima amarelas. Usamos texto
  // GRANDE (size 2 = 16px) só com o rótulo curto do estado, que preenche a
  // faixa sem transbordar para a área azul.
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(navShort(currentState));
  // Indicador de FIX no canto direito da faixa (size 1, alinhado ao topo).
  display.setTextSize(1);
  display.setCursor(104, 0);
  display.print(hasGpsFix ? "FIX" : "---");

  // ── Área azul (y>=18): dados de sensor, fonte pequena, passo 9px ──
  display.setTextSize(1);
  int y = 18;

  // Rumo + fonte (C=bússola, G=curso GPS).
  display.setCursor(0, y); y += 9;
  if (hasGpsFix || compassLastReadOk) {
    display.printf("Rumo %3.0f%c%c", currentHeading, (char)247,   // 247 = '°'
                   compassLastReadOk ? 'C' : 'G');
  } else {
    display.print("Rumo ---");
  }

  // Obstáculos L | R (setas indicam o lado).
  display.setCursor(0, y); y += 9;
  display.print((char)0x11);  // '◄'
  display.print(ultrasonicLHealthy ? String(obsDistL) : String("--"));
  display.print(" ");
  display.print((char)0x10);  // '►'
  display.print(ultrasonicRHealthy ? String(obsDistR) : String("--"));
  display.print("cm");

  // Thrust L/R com sinal (ré negativo).
  display.setCursor(0, y); y += 9;
  display.printf("Thr %d/%d", thrustL, thrustR);

  // Posição + link RPi na última linha.
  display.setCursor(0, y);
  if (hasGpsFix) {
    display.printf("%.4f,%.4f", currentLat, currentLon);
  } else {
    display.print("sem posicao");
  }
  // Símbolo de link no canto direito: '*' conectado, 'x' ausente.
  display.setCursor(120, y);
  display.print(isRpiPresent() ? "*" : "x");

  display.display();
}

// ─────────────────────────────────────────────────────────────────────────────
// FEEDBACK DE CALIBRACAO / SELF-TEST — telas dedicadas mostradas durante a rotina
// de boot/botão. Todas são no-op se o OLED estiver ausente.
// ─────────────────────────────────────────────────────────────────────────────
void displayShowSelfTest() {
  if (!displayReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print("CHECANDO");
  display.setTextSize(1);
  display.setCursor(0, 22);
  display.println("Verificando sensores");
  display.println("(bussola, sonar, GPS)");
  display.println("aguarde...");
  display.display();
}

void displayShowCalibrating(int secondsLeft, long xSpan, long ySpan) {
  if (!displayReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  // Título grande na faixa amarela.
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print("CALIBrar");
  // Instrução + progresso na área azul.
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println("GIRE o barco 360");
  display.setCursor(0, 32);
  display.printf("Faltam: %2ds", secondsLeft);
  display.setCursor(0, 44);
  display.printf("Amplitude X:%ld", xSpan);
  display.setCursor(0, 54);
  display.printf("          Y:%ld", ySpan);
  display.display();
}

void displayShowCalResult(bool ok, bool isCal) {
  if (!displayReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(ok ? "OK!" : "FALHA");
  display.setTextSize(1);
  display.setCursor(0, 24);
  display.println(isCal ? "Calibracao da bussola" : "Self-test sensores");
  display.setCursor(0, 40);
  if (ok) {
    display.println(isCal ? "Rumo deve varrer 360." : "Todos sensores OK.");
  } else {
    display.println(isCal ? "Amplitude baixa:" : "Ha falhas - ver");
    display.println(isCal ? "gire mais e repita." : "o log serial.");
  }
  display.display();
}
