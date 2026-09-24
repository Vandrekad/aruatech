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
