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
    // Guarda o target SEMPRE (mesmo sem fix): ele é a missão ativa a cumprir.
    // Antes, sem fix o comando era rejeitado e ESQUECIDO (e o commandId gravado),
    // o que fazia um target recuperado no boot — quando o GPS ainda não tem fix —
    // se perder para sempre, deixando o barco em HOLD. Agora retemos o target e
    // a navegação é ativada automaticamente quando o fix chega (ver maybeResumeNavigation).
    activeMissionId = command.missionId;
    goalLat = command.targetLat;
    goalLon = command.targetLon;
    hasActiveTarget = true;

    // Gate de prontidão: só INICIA a navegação com GPS fix. Sem fix, o target
    // fica pendente e o barco permanece em IDLE/HOLD (motores desligados) até o
    // fix chegar — então maybeResumeNavigation() dispara a navegação.
    if (!isSystemReady()) {
      Serial.println("set_destination RETIDO: aguardando GPS fix para iniciar.");
      sendEventToRpi("command_pending_no_fix", 0.0);
      lastCommandId = command.commandId;
      return;
    }
    homeLat = currentLat;
    homeLon = currentLon;
    routeDistanceMeters = computeDistanceMeters(currentLat, currentLon, goalLat, goalLon);
    remainingDistanceMeters = routeDistanceMeters;
    activeLeg = 0;
    routeProgress = 0.0;
    setNavState(NAVIGATING_TO_GOAL);
    Serial.printf("Destino definido: %.6f, %.6f (dist=%.1fm)\n",
                  command.targetLat, command.targetLon, routeDistanceMeters);

  } else if (command.type == "emergency_stop") {
    hasActiveTarget = false;   // missão encerrada: nada a retomar quando houver fix
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
