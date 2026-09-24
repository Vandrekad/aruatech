#include "modules/sensors/sensors.h"
#include <Wire.h>
#include "config.h"
#include "modules/state/state.h"
#include "modules/utils/utils.h"
#include "modules/link/serial_link.h"
#include "modules/navigation/navigation.h"

void initMotors() {
  ledcSetup(MOTOR_LEFT_CHANNEL, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcAttachPin(MOTOR_LEFT_PIN, MOTOR_LEFT_CHANNEL);
  ledcSetup(MOTOR_RIGHT_CHANNEL, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcAttachPin(MOTOR_RIGHT_PIN, MOTOR_RIGHT_CHANNEL);
}

bool initCompass() {
  // Registrador A: 8 amostras avg, 15Hz, modo normal
  Wire.beginTransmission(HMC5883L_ADDRESS);
  Wire.write(0x00);
  Wire.write(0x70);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  // Registrador B: ganho ±1.3 Ga
  Wire.beginTransmission(HMC5883L_ADDRESS);
  Wire.write(0x01);
  Wire.write(0xA0);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  // Registrador Mode: medição contínua
  Wire.beginTransmission(HMC5883L_ADDRESS);
  Wire.write(0x02);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  compassReady = true;
  return true;
}

bool initHardwareSensors() {
  Serial.println("Inicializando sensores de hardware...");
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  // GPS agora em Serial1 (Serial2 foi dedicada ao link com o RPi na F1).
  // Pull-up no RX mantém nível idle-alto quando o fio do GPS oscila/solta,
  // evitando que ruído seja lido como bytes (bytes_rx alto, nmea_like=0).
  pinMode(GPS_RX_PIN, INPUT_PULLUP);
  Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

#if GPS_ECHO_RAW
  // Sonda de baud: testa taxas comuns e conta bytes recebidos em ~1.5s cada.
  // Se UMA delas conta > 0 e as outras 0, essa é a taxa real do modulo.
  {
    const uint32_t bauds[] = {9600, 38400, 115200};
    Serial.printf("[GPS-PROBE] sondando baud no GPIO%d (TX do GPS)...\n", GPS_RX_PIN);
    for (uint8_t b = 0; b < 3; b++) {
      Serial1.begin(bauds[b], SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
      delay(50);
      while (Serial1.available()) Serial1.read();   // descarta lixo de troca
      unsigned long t = millis();
      unsigned long n = 0;
      while (millis() - t < 1500) {
        if (Serial1.available()) { Serial1.read(); n++; }
      }
      Serial.printf("[GPS-PROBE] baud=%lu -> %lu bytes\n", (unsigned long)bauds[b], n);
    }
    // Restaura o baud de operacao configurado.
    Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  }
#endif

  pinMode(ULTRASONIC_TRIG_PIN, OUTPUT);
  pinMode(ULTRASONIC_ECHO_PIN, INPUT);
  digitalWrite(ULTRASONIC_TRIG_PIN, LOW);

  initMotors();
  bool compassOk = initCompass();
  Serial.print("Bússola inicializada: ");
  Serial.println(compassOk ? "OK" : "FALHA");
  Serial.println("Sensores de hardware inicializados.");
  return true;
}

void stopMotors() {
  thrustL = 0;
  thrustR = 0;
  ledcWrite(MOTOR_LEFT_CHANNEL, 0);
  ledcWrite(MOTOR_RIGHT_CHANNEL, 0);
}

void updateMotorOutputs() {
  ledcWrite(MOTOR_LEFT_CHANNEL, constrain(thrustL, 0, 255));
  ledcWrite(MOTOR_RIGHT_CHANNEL, constrain(thrustR, 0, 255));
}

void readUltrasonic() {
  digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(ULTRASONIC_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(ULTRASONIC_TRIG_PIN, LOW);

  // pulseIn é bloqueante — timeout de 25ms limita o impacto
  unsigned long duration = pulseIn(ULTRASONIC_ECHO_PIN, HIGH, ULTRASONIC_TIMEOUT_US);
  if (duration > 0) {
    int measured = (int)(duration * 0.034 / 2.0);
    obsDist = min(ULTRASONIC_MAX_CM, measured);
    ultrasonicHealthy = true;
  } else {
    // duration == 0 (timeout): sem eco válido. Mantém último valor, marca falha.
    ultrasonicHealthy = false;
  }
}

bool readCompass() {
  if (!compassReady) {
    return false;
  }

  // Usar endTransmission(false) para repeated start — necessário para alguns barramentos I2C
  Wire.beginTransmission(HMC5883L_ADDRESS);
  Wire.write(0x03);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  Wire.requestFrom((uint8_t)HMC5883L_ADDRESS, (uint8_t)6);
  if (Wire.available() < 6) {
    return false;
  }

  // HMC5883L retorna: X_MSB, X_LSB, Z_MSB, Z_LSB, Y_MSB, Y_LSB
  int16_t rawX = (Wire.read() << 8) | Wire.read();
  int16_t rawZ = (Wire.read() << 8) | Wire.read();
  int16_t rawY = (Wire.read() << 8) | Wire.read();

  // Verificar saturação do sensor (valor -4096 indica overflow)
  if (rawX == -4096 || rawY == -4096 || rawZ == -4096) {
    return false;
  }

  double headingRadians = atan2((double)rawY, (double)rawX);
  double headingDegrees = headingRadians * 180.0 / PI;
  if (headingDegrees < 0) {
    headingDegrees += 360.0;
  }

  currentHeading = headingDegrees;
  return true;
}

void readGPS() {
  // Buffer estático com limite de tamanho para evitar memory leak
  static char lineBuffer[GPS_LINE_MAX_LENGTH + 1];
  static uint8_t linePos = 0;

  while (Serial1.available()) {
    char c = (char)Serial1.read();
    gpsBytesWindow++;
    // Conta bytes que PODEM ser NMEA (ASCII imprimível ou CR/LF). Se este contador
    // fica em 0 enquanto gpsBytesWindow sobe, o que chega é RUÍDO elétrico (pino
    // flutuante), não sinal de GPS. Os contadores são zerados pelo log periódico.
    bool printableAscii = (c >= 0x20 && c <= 0x7E) || c == '\n' || c == '\r';
    if (printableAscii) gpsNmeaWindow++;
#if GPS_ECHO_RAW
    // Dump bruto de NMEA — só atrás do flag de debug. Ecoa só ASCII imprimível.
    if (printableAscii) Serial.write(c);
#endif

    if (c == '\n' || c == '\r') {
      if (linePos > 0) {
        lineBuffer[linePos] = '\0';
        String line(lineBuffer);

        if (line.startsWith("$GPRMC") || line.startsWith("$GNRMC")) {
          int index = 0;
          int fieldStart = 0;
          String fields[13];
          for (int i = 0; i < (int)line.length() && index < 13; i++) {
            if (line[i] == ',') {
              fields[index++] = line.substring(fieldStart, i);
              fieldStart = i + 1;
            }
          }
          if (index < 13) {
            fields[index++] = line.substring(fieldStart);
          }

          // Campos: 0=$GPRMC, 1=time, 2=status, 3=lat, 4=N/S, 5=lon, 6=E/W, 7=speed, 8=course
          if (index >= 9 &&
              fields[2].length() > 0 &&
              fields[3].length() > 0 &&
              fields[4].length() > 0 &&
              fields[5].length() > 0 &&
              fields[6].length() > 0) {

            char status = fields[2].charAt(0);
            if (status == 'A') {
              double lat = nmeaToDecimal(fields[3], fields[4].charAt(0));
              double lon = nmeaToDecimal(fields[5], fields[6].charAt(0));
              if (lat != 0.0 || lon != 0.0) {
                gpsLat = lat;
                gpsLon = lon;
                if (fields[8].length() > 0) {
                  gpsCourse = fields[8].toDouble();
                }
                hasGpsFix = true;
              }
            } else {
              hasGpsFix = false;
            }
          }
        }
        linePos = 0;
      }
    } else if (c != '\0') {
      // Proteger contra overflow do buffer
      if (linePos < GPS_LINE_MAX_LENGTH) {
        lineBuffer[linePos++] = c;
      } else {
        // Sentença corrompida/muito longa — descartar
        linePos = 0;
      }
    }
  }
}

void updateSensorValues() {
  readGPS();
  // Promove a leitura do GPS para a posição corrente. currentLat/currentLon são
  // o que a telemetria (position), o status (last_position), o link com o RPi e
  // toda a navegação (LOS, hold-position) consomem — sem esta cópia, a posição
  // publicada no Firebase nunca sai dos valores default de boot.
  if (hasGpsFix) {
    currentLat = gpsLat;
    currentLon = gpsLon;
  }
  // Bússola: usa curso do GPS como fallback quando a leitura falha.
  compassLastReadOk = readCompass();
  if (!compassLastReadOk && hasGpsFix) {
    currentHeading = gpsCourse;
  }
  readUltrasonic();
}

// ─────────────────────────────────────────────────────────────────────────────
// Log estruturado periódico — substitui os [GPS-DIAG]/dump bruto dispersos.
// Reaproveita o estado já lido; não toca no hardware.
// ─────────────────────────────────────────────────────────────────────────────
void printStatusBlock() {
  // Saúde do GPS: precisa de bytes chegando E fix para estar "OK".
  const char *gpsHealth =
      (gpsBytesWindow == 0)        ? "SEM SINAL (0 bytes — verificar fio/energia)" :
      (gpsNmeaWindow == 0)         ? "RUIDO (bytes sem NMEA — pino/baud)" :
      hasGpsFix                    ? "OK (fix)" : "SEM FIX (recebendo, aguardando satelites)";

  Serial.println(F("┌──────────────── USV STATUS ────────────────"));
  // 1. Sensores OK/FALHA
  Serial.printf("│ GPS      : %s\n", gpsHealth);
  Serial.printf("│ Bussola  : %s\n", compassReady ? (compassLastReadOk ? "OK" : "FALHA leitura")
                                                   : "FALHA (nao inicializou)");
  Serial.printf("│ Ultrassom: %s\n", ultrasonicHealthy ? "OK" : "FALHA (sem eco)");
  // 2. Localização + fix
  if (hasGpsFix) {
    Serial.printf("│ Posicao  : lat=%.6f lon=%.6f  FIX=SIM\n", currentLat, currentLon);
  } else {
    Serial.printf("│ Posicao  : --- (sem fix)          FIX=NAO\n");
  }
  // 3. Distância do ultrassônico
  if (ultrasonicHealthy) {
    Serial.printf("│ Obstaculo: %d cm\n", obsDist);
  } else {
    Serial.printf("│ Obstaculo: --- (sem leitura valida)\n");
  }
  // 4. Link com o Raspberry Pi
  Serial.printf("│ Link RPi : %s\n", isRpiPresent() ? "CONECTADO" : "AUSENTE (buffer local)");
  // 5. Rumo (desvio em relação ao Norte)
  if (compassLastReadOk) {
    Serial.printf("│ Rumo     : %.1f graus (bussola)\n", currentHeading);
  } else if (hasGpsFix) {
    Serial.printf("│ Rumo     : %.1f graus (curso GPS, bussola em falha)\n", currentHeading);
  } else {
    Serial.printf("│ Rumo     : --- (sem bussola nem fix)\n");
  }
  // Extra: estado de navegação e diagnóstico de bytes do GPS na janela.
  Serial.printf("│ Nav      : %s | gps_rx=%lu nmea=%lu\n",
                navStateToString(currentState), gpsBytesWindow, gpsNmeaWindow);
  Serial.println(F("└─────────────────────────────────────────────"));

  // Zera os contadores de janela do GPS para a próxima amostragem.
  gpsBytesWindow = 0;
  gpsNmeaWindow = 0;
}
