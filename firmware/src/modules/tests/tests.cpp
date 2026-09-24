#include "modules/tests/tests.h"
#include <LittleFS.h>
#include "config.h"
#include "modules/storage/storage.h"
#include "modules/sensors/sensors.h"
#include "modules/navigation/navigation.h"
#include "modules/utils/utils.h"
#include "modules/state/state.h"

static void printComponentTestResult(const char* component, bool result) {
  Serial.print("[TESTE] ");
  Serial.print(component);
  Serial.print(" => ");
  Serial.println(result ? "OK" : "FALHA");
}

void runFirmwareComponentTests() {
  Serial.println("=== INÍCIO DO APLICATIVO DE TESTES DE COMPONENTES ===");
  printComponentTestResult("LittleFS", testLittleFS());
  printComponentTestResult("GPS parsing / fix", testGPSParsing());
  printComponentTestResult("Bússola HMC5883L", testCompassSensor());
  printComponentTestResult("Ultrassom HC-SR04", testUltrasonicSensor());
  printComponentTestResult("Saída de motores PWM", testMotorOutput());
  printComponentTestResult("Geração de rota básica", testRouteGeneration());
  printComponentTestResult("Buffer offline simples", testOfflineBuffering());
  Serial.println("=== FIM DO APLICATIVO DE TESTES DE COMPONENTES ===");
}

bool testLittleFS() {
  Serial.println("Testando armazenamento LittleFS...");
  const char* testPath = "/test_lfs.txt";

  File file = LittleFS.open(testPath, FILE_WRITE);
  if (!file) {
    Serial.println("Falha ao abrir arquivo de teste no LittleFS (WRITE).");
    return false;
  }
  file.println("firmware-test");
  file.close();

  file = LittleFS.open(testPath, FILE_READ);
  if (!file) {
    Serial.println("Falha ao ler arquivo de teste no LittleFS (READ).");
    return false;
  }
  String content = file.readStringUntil('\n');
  file.close();

  String trimmed = content;
  trimmed.trim();

  LittleFS.remove(testPath);

  bool ok = trimmed == "firmware-test";
  if (!ok) {
    Serial.print("Conteúdo inesperado LittleFS: '");
    Serial.print(trimmed);
    Serial.println("'");
  }
  return ok;
}

bool testGPSParsing() {
  Serial.println("Testando parser GPS com NMEA de exemplo...");
  // Backup state
  bool backupHasGps = hasGpsFix;
  double backupLat = gpsLat;
  double backupLon = gpsLon;
  double backupCourse = gpsCourse;

  // Simular parsing manual (readGPS() lê de Serial2)
  String line = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
  int index = 0;
  int fieldStart = 0;
  String fields[13];
  for (int i = 0; i < (int)line.length() && index < 13; i++) {
    if (line[i] == ',') {
      fields[index++] = line.substring(fieldStart, i);
      fieldStart = i + 1;
    }
  }
  if (index < 13) fields[index++] = line.substring(fieldStart);

  bool ok = false;
  if (index >= 9 && fields[2] == "A") {
    gpsLat = nmeaToDecimal(fields[3], fields[4].charAt(0));
    gpsLon = nmeaToDecimal(fields[5], fields[6].charAt(0));
    gpsCourse = fields[8].toDouble();
    hasGpsFix = true;

    ok = hasGpsFix && fabs(gpsLat - 48.1173) < 0.01 && fabs(gpsLon - 11.5167) < 0.01;
    Serial.printf("GPS parsed: lat=%.6f lon=%.6f course=%.2f\n", gpsLat, gpsLon, gpsCourse);
  }

  // Restore
  gpsLat = backupLat;
  gpsLon = backupLon;
  gpsCourse = backupCourse;
  hasGpsFix = backupHasGps;
  return ok;
}

bool testCompassSensor() {
  Serial.println("Testando leitura da bússola HMC5883L...");
  if (!compassReady) {
    Serial.println("Bússola não está pronta.");
    return false;
  }
  for (int i = 0; i < 3; i++) {
    if (readCompass()) {
      Serial.printf("Heading lido: %.2f\n", currentHeading);
      return true;
    }
    delay(200);
  }
  Serial.println("Falha ao ler bússola após 3 tentativas.");
  return false;
}

bool testUltrasonicSensor() {
  Serial.println("Testando sensor ultrassônico...");
  readUltrasonic();
  Serial.printf("Distância medida: %d cm\n", obsDist);
  return obsDist > 0 && obsDist <= ULTRASONIC_MAX_CM;
}

bool testMotorOutput() {
  Serial.println("Testando saída PWM dos motores...");
  thrustL = 100;
  thrustR = 100;
  updateMotorOutputs();
  delay(200);
  stopMotors();
  Serial.println("Saída PWM aplicada e desligada.");
  return true;
}

bool testRouteGeneration() {
  Serial.println("Testando geração de rota básica...");
  double startLat = currentLat;
  double startLon = currentLon;
  double targetLat = startLat + 0.001;
  double targetLon = startLon + 0.001;
  double dist = computeDistanceMeters(startLat, startLon, targetLat, targetLon);
  Serial.printf("Distância calculada: %.2f m\n", dist);
  return dist > 100.0;
}

bool testOfflineBuffering() {
  Serial.println("Testando armazenamento offline simples...");
  const char* testTelem = "/test_offline_buffer.ndjson";
  const char* testPath = "/test_offline_path.ndjson";

  if (LittleFS.exists(testTelem)) LittleFS.remove(testTelem);
  if (LittleFS.exists(testPath)) LittleFS.remove(testPath);

  bool ok1 = appendLineToFile(testTelem, "{\"test\":\"offline\"}");
  bool ok2 = appendLineToFile(testPath, "{\"lat\":0.0,\"lon\":0.0,\"ts\":0}");

  std::vector<String> lines;
  bool ok3 = readFileLines(testTelem, lines) && lines.size() == 1;
  lines.clear();
  bool ok4 = readFileLines(testPath, lines) && lines.size() == 1;

  if (LittleFS.exists(testTelem)) LittleFS.remove(testTelem);
  if (LittleFS.exists(testPath)) LittleFS.remove(testPath);

  return ok1 && ok2 && ok3 && ok4;
}
