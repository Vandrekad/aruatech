#pragma once

#include <Arduino.h>
#include "modules/navigation/navigation.h"

struct DroneCommand {
  String commandId;
  String type;
  double targetLat = 0.0;   // FIX: inicializar para evitar valor lixo
  double targetLon = 0.0;   // FIX: inicializar para evitar valor lixo
  String missionId;
  unsigned long issuedAt = 0;
};

void setNavState(NavState newState);
void handleCommand(const DroneCommand &command);
