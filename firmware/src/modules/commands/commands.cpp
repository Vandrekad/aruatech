#include "modules/commands/commands.h"
#include "modules/state/state.h"
#include "modules/navigation/navigation.h"
#include "modules/link/serial_link.h"

// Arquitetura dual: os comandos NÃO vêm mais do Firebase. O RPi é dono da nuvem
// e repassa set_destination/emergency_stop ao ESP32 por UART (ver serial_link.cpp,
// que chama handleCommand()). fetchCommand()/processCommand() (polling RTDB) foram
// removidos junto com a dependência de WiFi/Firebase.

void setNavState(NavState newState) {
  if (newState == currentState) {
    return;
  }
  currentState = newState;
  // Station-keeping: ao entrar em IDLE, fixa a posição atual como âncora;
  // ao sair, libera a âncora para não interferir na navegação.
  if (newState == IDLE_HOLDING_POSITION) {
    setHoldAnchor(currentLat, currentLon);
  } else {
    clearHoldAnchor();
  }
  Serial.print("Nav state alterado para: ");
  Serial.println(navStateToString(currentState));
  // Notifica o RPi da mudança de estado por UART (ele publica no Firebase).
  sendEventToRpi("nav_state_changed", (double)newState);
}

void handleCommand(const DroneCommand &command) {
  Serial.print("Processando comando: ");
  Serial.println(command.type);

  if (command.type == "set_destination") {
    // Gate de prontidão: não inicia navegação sem GPS fix válido. O comando é
    // reconhecido (lastCommandId atualizado no fim), mas a missão não começa —
    // o drone permanece em IDLE com motores desligados até haver fix.
    if (!isSystemReady()) {
      Serial.println("set_destination RECUSADO: aguardando GPS fix.");
      sendEventToRpi("command_rejected_no_fix", 0.0);
      lastCommandId = command.commandId;
      return;
    }
    activeMissionId = command.missionId;
    homeLat = currentLat;
    homeLon = currentLon;
    goalLat = command.targetLat;
    goalLon = command.targetLon;
    routeDistanceMeters = computeDistanceMeters(currentLat, currentLon, goalLat, goalLon);
    remainingDistanceMeters = routeDistanceMeters;
    activeLeg = 0;
    routeProgress = 0.0;
    setNavState(NAVIGATING_TO_GOAL);
    Serial.printf("Destino definido: %.6f, %.6f (dist=%.1fm)\n",
                  command.targetLat, command.targetLon, routeDistanceMeters);

  } else if (command.type == "emergency_stop") {
    goalLat = homeLat;
    goalLon = homeLon;
    routeDistanceMeters = computeDistanceMeters(currentLat, currentLon, homeLat, homeLon);
    remainingDistanceMeters = routeDistanceMeters;
    activeLeg = 0;
    routeProgress = 0.0;
    setNavState(RETURNING_TO_HOME);
    Serial.println("Emergência: retornando para a origem.");

  } else {
    Serial.print("Comando desconhecido: ");
    Serial.println(command.type);
  }

  lastCommandId = command.commandId;
}
