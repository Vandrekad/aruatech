#include "modules/sensors/sensors.h"
#include <Wire.h>
#include "config.h"
#include "modules/state/state.h"
#include "modules/utils/utils.h"

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
  // GPS agora em Serial1 (Serial2 foi dedicada ao link com o RPi na F1)
  Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

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
  }
  // Se duration == 0 (timeout), mantém último valor válido
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
  if (!readCompass() && hasGpsFix) {
    currentHeading = gpsCourse;
  }
  readUltrasonic();
}
