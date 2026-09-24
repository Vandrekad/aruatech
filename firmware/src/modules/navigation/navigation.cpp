#include "modules/navigation/navigation.h"
#include "modules/navigation/navigation.h"
#include "modules/utils/utils.h"
#include "modules/state/state.h"
#include "modules/sensors/sensors.h"
#include "modules/commands/commands.h"
#include "modules/link/serial_link.h"
#include "config.h"

static unsigned long obstacleAvoidanceStartMs = 0;
static NavState previousNavState = IDLE_HOLDING_POSITION;

// Forward declaration (definida mais abaixo, usada por updateLOSControl).
void updateHoldPosition();

const char* navStateToString(NavState state) {
  switch (state) {
    case IDLE_HOLDING_POSITION:
      return "IDLE_HOLDING_POSITION";
    case NAVIGATING_TO_GOAL:
      return "NAVIGATING_TO_GOAL";
    case OBSTACLE_AVOIDANCE:
      return "OBSTACLE_AVOIDANCE";
    case RETURNING_TO_HOME:
      return "RETURNING_TO_HOME";
    case OFFLINE_NAVIGATION:
      return "OFFLINE_NAVIGATION";
    default:
      return "UNKNOWN";
  }
}

double computeDistanceMeters(double lat1, double lon1, double lat2, double lon2) {
  double dLat = deg2rad(lat2 - lat1);
  double dLon = deg2rad(lon2 - lon1);
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(deg2rad(lat1)) * cos(deg2rad(lat2)) *
             sin(dLon / 2) * sin(dLon / 2);
  double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
  return 6371000.0 * c;
}

static double computeCrossTrackError(double fromLat, double fromLon,
                                      double toLat, double toLon,
                                      double pointLat, double pointLon) {
  double dxLeg = (toLon - fromLon) * cos(deg2rad(fromLat)) * 111320.0;
  double dyLeg = (toLat - fromLat) * 110540.0;
  double dxPoint = (pointLon - fromLon) * cos(deg2rad(fromLat)) * 111320.0;
  double dyPoint = (pointLat - fromLat) * 110540.0;
  double legNorm = sqrt(dxLeg * dxLeg + dyLeg * dyLeg);
  if (legNorm < 1e-6) {
    return 0.0;
  }
  return (dxPoint * dyLeg - dyPoint * dxLeg) / legNorm;
}

static void enterObstacleAvoidance() {
  if (currentState != OBSTACLE_AVOIDANCE) {
    previousNavState = currentState;
    currentState = OBSTACLE_AVOIDANCE;
    obstacleAvoidanceStartMs = millis();
    Serial.println("OBSTACLE_AVOIDANCE ativado.");
    sendEventToRpi("obstacle_detected", (double)obsDist);
  }
}

double computeLOSHeading(double fromLat, double fromLon, double toLat, double toLon) {
  double dxLeg = (toLon - fromLon) * cos(deg2rad(fromLat)) * 111320.0;
  double dyLeg = (toLat - fromLat) * 110540.0;
  double chiP = atan2(dyLeg, dxLeg) * 180.0 / PI;
  if (chiP < 0) {
    chiP += 360.0;
  }

  double eCt = computeCrossTrackError(fromLat, fromLon, toLat, toLon, currentLat, currentLon);
  double chiD = chiP - atan2(eCt, LOS_LOOKAHEAD_METERS) * 180.0 / PI;
  chiD = fmod(chiD + 360.0, 360.0);

  // Compensação de correnteza via diferença GPS COG vs bússola
  if (hasGpsFix && compassReady) {
    double betaHat = wrapAngleDeg(gpsCourse - currentHeading);
    chiD = wrapAngleDeg(chiD - betaHat);
    if (chiD < 0) {
      chiD += 360.0;
    }
  }

  return chiD;
}

void updateLOSControl() {
  // GATE DE PRONTIDÃO: sem GPS fix não há posição confiável — motores DESLIGADOS
  // e nenhuma navegação. Isso protege contra sair navegando com a posição default
  // de boot antes do primeiro fix. A missão só progride quando isSystemReady().
  if (!isSystemReady()) {
    thrustL = 0;
    thrustR = 0;
    return;
  }

  if (currentState == NAVIGATING_TO_GOAL || currentState == RETURNING_TO_HOME) {
    // Verificar obstáculo
    if (obsDist <= OBSTACLE_THRESHOLD_CM) {
      enterObstacleAvoidance();
      return;
    }

    double targetLat = (currentState == NAVIGATING_TO_GOAL) ? goalLat : homeLat;
    double targetLon = (currentState == NAVIGATING_TO_GOAL) ? goalLon : homeLon;

    // Distância ao alvo e progresso da rota (atualizados a cada ciclo de controle).
    double distToTarget = computeDistanceMeters(currentLat, currentLon, targetLat, targetLon);
    remainingDistanceMeters = distToTarget;
    if (routeDistanceMeters > 1e-6) {
      routeProgress = constrain(1.0 - (distToTarget / routeDistanceMeters), 0.0, 1.0);
    }

    // ── Chegada ── dentro do raio de chegada: fecha a missão e mantém posição.
    // Só declara chegada com GPS fix — sem fix, a posição pode estar defasada e
    // gerar chegada falsa; nesse caso segue navegando com a melhor estimativa.
    if (hasGpsFix && distToTarget <= ARRIVAL_RADIUS_METERS) {
      thrustL = 0;
      thrustR = 0;
      remainingDistanceMeters = 0.0;
      routeProgress = 1.0;
      bool wasReturn = (currentState == RETURNING_TO_HOME);
      Serial.printf("[NAV] Alvo alcançado (%.1fm). Missão concluída.\n", distToTarget);
      sendEventToRpi(wasReturn ? "return_completed" : "mission_completed", distToTarget);
      // Muda para hold ativo — setNavState ancora a posição atual (o alvo).
      setNavState(IDLE_HOLDING_POSITION);
      return;
    }

    // Usar posição atual como ponto de partida para LOS
    // (idealmente seria WP_i da perna atual, mas no MVP com rota ponto-a-ponto é equivalente)
    double desiredHeading = computeLOSHeading(currentLat, currentLon, targetLat, targetLon);
    double error = headingErrorDeg(desiredHeading, currentHeading);
    int correction = (int)(error * LOS_HEADING_GAIN);

    thrustL = constrain(NAV_BASE_THRUST + correction, 0, 255);
    thrustR = constrain(NAV_BASE_THRUST - correction, 0, 255);

    // Banda morta: se erro angular é mínimo, navegação reta
    if (fabs(error) < 5.0) {
      thrustL = NAV_BASE_THRUST;
      thrustR = NAV_BASE_THRUST;
    }

  } else if (currentState == OBSTACLE_AVOIDANCE) {
    // Desvio simples: virar para um lado
    thrustL = 180;
    thrustR = 60;

    unsigned long elapsed = millis() - obstacleAvoidanceStartMs;
    if ((obsDist > OBSTACLE_CLEAR_CM && elapsed > 2000) ||
        elapsed > OBSTACLE_AVOIDANCE_TIMEOUT_MS) {
      currentState = previousNavState;
      Serial.println("Saindo de OBSTACLE_AVOIDANCE.");
    }

  } else {
    // IDLE_HOLDING_POSITION (e qualquer estado remanescente): manter posição.
    updateHoldPosition();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Station-keeping: âncora de posição para IDLE_HOLDING_POSITION
// ─────────────────────────────────────────────────────────────────────────────
void setHoldAnchor(double lat, double lon) {
  holdLat = lat;
  holdLon = lon;
  holdAnchored = true;
  Serial.printf("[HOLD] Âncora fixada em %.6f, %.6f\n", lat, lon);
}

void clearHoldAnchor() {
  holdAnchored = false;
}

// Prontidão para navegar. No MVP o único requisito bloqueante é GPS com fix.
bool isSystemReady() {
  return hasGpsFix;
}

// Mantém a embarcação sobre a âncora contra a correnteza. Só atua com GPS fix:
// sem posição confiável, PARAR os motores é o comportamento seguro (não sair
// correndo atrás de uma âncora com base numa posição estimada/desatualizada).
void updateHoldPosition() {
  // Sem fix → seguro: motores parados (comportamento antigo do IDLE).
  if (!hasGpsFix) {
    thrustL = 0;
    thrustR = 0;
    return;
  }

  // Auto-ancoragem: em IDLE com fix mas ainda sem âncora (boot, ou fix recém
  // recuperado), fixa a posição atual. Cobre o estado inicial, que não passa
  // por setNavState().
  if (!holdAnchored && currentState == IDLE_HOLDING_POSITION) {
    setHoldAnchor(currentLat, currentLon);
  }

  if (!holdAnchored) {
    thrustL = 0;
    thrustR = 0;
    return;
  }

  double drift = computeDistanceMeters(currentLat, currentLon, holdLat, holdLon);

  // Dentro da banda morta: considera-se "no lugar", não gasta bateria corrigindo.
  if (drift <= HOLD_DEADBAND_METERS) {
    thrustL = 0;
    thrustR = 0;
    return;
  }

  // Fora do raio: apontar de volta para a âncora e empurrar proporcionalmente.
  double desiredHeading = computeLOSHeading(currentLat, currentLon, holdLat, holdLon);
  double error = headingErrorDeg(desiredHeading, currentHeading);
  int correction = (int)(error * HOLD_HEADING_GAIN);

  // Empuxo base cresce com o desvio, saturado no teto de segurança.
  int base = HOLD_BASE_THRUST + (int)((drift - HOLD_DEADBAND_METERS) * 6.0);
  base = constrain(base, HOLD_BASE_THRUST, HOLD_MAX_THRUST);

  thrustL = constrain(base + correction, 0, HOLD_MAX_THRUST);
  thrustR = constrain(base - correction, 0, HOLD_MAX_THRUST);
}

void advanceTowards(double destLat, double destLon, double stepMeters) {
  double dx = (destLon - currentLon) * cos(deg2rad(currentLat)) * 111320.0;
  double dy = (destLat - currentLat) * 110540.0;
  double dist = sqrt(dx * dx + dy * dy);

  if (dist <= stepMeters || dist < 1e-6) {
    currentLat = destLat;
    currentLon = destLon;
    remainingDistanceMeters = 0.0;
    return;
  }

  double ratio = stepMeters / dist;
  currentLat += (destLat - currentLat) * ratio;
  currentLon += (destLon - currentLon) * ratio;
  remainingDistanceMeters = computeDistanceMeters(currentLat, currentLon, destLat, destLon);
}
