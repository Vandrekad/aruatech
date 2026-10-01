#pragma once

#include <Arduino.h>

enum NavState {
  IDLE_HOLDING_POSITION,
  NAVIGATING_TO_GOAL,
  OBSTACLE_AVOIDANCE,
  RETURNING_TO_HOME,
  OFFLINE_NAVIGATION
};

const char* navStateToString(NavState state);
double computeDistanceMeters(double lat1, double lon1, double lat2, double lon2);
double computeLOSHeading(double fromLat, double fromLon, double toLat, double toLon);
void updateLOSControl();
// Retoma a navegação se houver um target ativo pendente e o GPS acabou de obter
// fix (ex.: target recuperado após reset do ESP32, recebido antes do 1º fix).
// No-op se não há target ativo, se não há fix, ou se já está navegando. Chamada
// periodicamente no loop, após a leitura dos sensores.
void maybeResumeNavigation();
void advanceTowards(double destLat, double destLon, double stepMeters);

// Station-keeping: fixa a posição atual como âncora de hold e a mantém no IDLE.
void setHoldAnchor(double lat, double lon);
void clearHoldAnchor();

// Gate de prontidão: true só quando os sensores essenciais para navegar estão
// prontos. No MVP, o único requisito bloqueante é GPS com FIX válido — sem ele
// não há posição confiável, então motores ficam desligados e comandos de
// navegação são recusados. Bússola/ultrassom são desejáveis mas NÃO bloqueiam
// (a bússola tem fallback via curso do GPS; o ultrassom só habilita desvio).
bool isSystemReady();
