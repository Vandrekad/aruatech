/**
 * Teste isolado: módulo sensors
 * Exercita: initHardwareSensors, readGPS, readCompass, readUltrasonic, updateMotorOutputs
 *
 * Resultado esperado no Serial (115200):
 *   - Inicialização de sensores OK/FALHA
 *   - Leitura de bússola com heading
 *   - Leitura de ultrassônico com distância em cm
 *   - Ativação breve de motores PWM
 *   - Leitura contínua de GPS (aguardando fix)
 */

#include <Arduino.h>
#include "modules/sensors/sensors.h"
#include "modules/state/state.h"

static int testsPassed = 0;
static int testsFailed = 0;

void reportTest(const char* name, bool result) {
  Serial.printf("[TEST] %-30s => %s\n", name, result ? "PASS" : "FAIL");
  if (result) testsPassed++;
  else testsFailed++;
}

void testInitSensors() {
  bool ok = initHardwareSensors();
  reportTest("initHardwareSensors", ok);
}

void testCompass() {
  if (!compassReady) {
    reportTest("readCompass (sensor não pronto)", false);
    return;
  }
  bool ok = readCompass();
  if (ok) {
    Serial.printf("  -> Heading: %.2f graus\n", currentHeading);
  }
  reportTest("readCompass", ok && currentHeading >= 0.0 && currentHeading < 360.0);
}

void testUltrasonic() {
  readUltrasonic();
  bool ok = (obsDist > 0 && obsDist <= 400);
  Serial.printf("  -> Distância: %d cm\n", obsDist);
  reportTest("readUltrasonic", ok);
}

void testMotors() {
  thrustL = 80;
  thrustR = 80;
  updateMotorOutputs();
  delay(300);
  thrustL = 0;
  thrustR = 0;
  updateMotorOutputs();
  reportTest("updateMotorOutputs (breve)", true);
}

void testGPSRead() {
  Serial.println("  -> Lendo GPS por 5 segundos...");
  unsigned long start = millis();
  while (millis() - start < 5000) {
    readGPS();
    delay(100);
  }
  Serial.printf("  -> Fix: %s | Lat: %.6f | Lon: %.6f\n",
                hasGpsFix ? "SIM" : "NÃO", gpsLat, gpsLon);
  // GPS pode não ter fix indoor — reporta resultado mas não falha
  reportTest("readGPS (executou sem crash)", true);
}

// ── Ponte GPS -> posição corrente ──────────────────────────────────────────
// Regressão do bug em que readGPS() só gravava gpsLat/gpsLon e a posição
// publicada (currentLat/currentLon) nunca saía do default de boot. Não depende
// de hardware: injeta o resultado de um fix e verifica a promoção feita por
// updateSensorValues(). Determinístico, roda em bancada sem GPS conectado.
void testGpsToCurrentBridge() {
  // 1) COM fix: currentLat/currentLon devem passar a refletir gpsLat/gpsLon.
  currentLat = -3.1019;   // valores default de boot
  currentLon = -60.0250;
  gpsLat = -3.1099;       // "novo fix" bem distinto do default
  gpsLon = -60.0333;
  hasGpsFix = true;
  updateSensorValues();   // readGPS() sem dados não altera gps*; a promoção roda
  bool promoted = fabs(currentLat - gpsLat) < 1e-9 &&
                  fabs(currentLon - gpsLon) < 1e-9;
  Serial.printf("  -> com fix: current=(%.4f,%.4f) gps=(%.4f,%.4f)\n",
                currentLat, currentLon, gpsLat, gpsLon);
  reportTest("bridge: fix promove gps->current", promoted);

  // 2) SEM fix: a posição corrente NÃO deve ser sobrescrita (mantém a última boa).
  currentLat = -3.2000;
  currentLon = -60.4000;
  gpsLat = 0.0;           // valor que jamais deve vazar para current sem fix
  gpsLon = 0.0;
  hasGpsFix = false;
  updateSensorValues();
  bool held = fabs(currentLat - (-3.2000)) < 1e-9 &&
              fabs(currentLon - (-60.4000)) < 1e-9;
  Serial.printf("  -> sem fix: current=(%.4f,%.4f) preservado\n",
                currentLat, currentLon);
  reportTest("bridge: sem fix preserva current", held);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("========================================");
  Serial.println(" TESTE ISOLADO: módulo SENSORS");
  Serial.println("========================================");

  testInitSensors();
  testCompass();
  testUltrasonic();
  testMotors();
  testGPSRead();
  testGpsToCurrentBridge();

  Serial.println("========================================");
  Serial.printf(" RESULTADO: %d PASS / %d FAIL\n", testsPassed, testsFailed);
  Serial.println("========================================");
}

void loop() {
  // Leitura contínua para debug se necessário
  delay(2000);
  updateSensorValues();
  Serial.printf("[LIVE] heading=%.1f obs=%dcm gps_fix=%s lat=%.6f lon=%.6f\n",
                currentHeading, obsDist, hasGpsFix ? "Y" : "N", gpsLat, gpsLon);
}
