#include "modules/sensors/sensors.h"
#include <Wire.h>
#include "config.h"
#include "modules/state/state.h"
#include "modules/utils/utils.h"
#include "modules/link/serial_link.h"
#include "modules/navigation/navigation.h"
#include "modules/storage/storage.h"
#include "modules/display/display.h"

void initMotors() {
  ledcSetup(MOTOR_LEFT_CHANNEL, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcAttachPin(MOTOR_LEFT_PIN, MOTOR_LEFT_CHANNEL);
  ledcSetup(MOTOR_RIGHT_CHANNEL, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcAttachPin(MOTOR_RIGHT_PIN, MOTOR_RIGHT_CHANNEL);

  // Pinos de sentido do L298N (ponte H). Iniciam em LOW/LOW = coast (parado),
  // estado seguro antes de qualquer comando de navegação.
  pinMode(MOTOR_LEFT_IN1_PIN,  OUTPUT);
  pinMode(MOTOR_LEFT_IN2_PIN,  OUTPUT);
  pinMode(MOTOR_RIGHT_IN3_PIN, OUTPUT);
  pinMode(MOTOR_RIGHT_IN4_PIN, OUTPUT);
  digitalWrite(MOTOR_LEFT_IN1_PIN,  LOW);
  digitalWrite(MOTOR_LEFT_IN2_PIN,  LOW);
  digitalWrite(MOTOR_RIGHT_IN3_PIN, LOW);
  digitalWrite(MOTOR_RIGHT_IN4_PIN, LOW);
}

// Define o sentido de UM motor pelos seus dois pinos IN do L298N.
//   forward=true  -> (in_a=HIGH, in_b=LOW)  = frente
//   forward=false -> (in_a=LOW,  in_b=HIGH) = ré
static void setMotorDirection(int in_a_pin, int in_b_pin, bool forward) {
  digitalWrite(in_a_pin, forward ? HIGH : LOW);
  digitalWrite(in_b_pin, forward ? LOW  : HIGH);
}

// Tipo de magnetômetro detectado no boot e endereço em que respondeu.
enum CompassChip { COMPASS_NONE, COMPASS_HMC5883L, COMPASS_QMC5883L };
static CompassChip compassChip = COMPASS_NONE;
static uint8_t compassAddr = 0;

// Calibração hard-iron: offset (centro do círculo medido) por eixo. Sem isso o
// atan2 não varre 360° — o campo fica deslocado da origem e o ângulo "dobra" no
// meio da volta. Carregado do LittleFS no boot; (0,0) = ainda não calibrado.
static float magOffX = 0.0f;
static float magOffY = 0.0f;
static bool  magCalibrated = false;

// Calibração soft-iron (escala por eixo): quando a resposta do campo é elíptica
// em vez de circular (span de X != span de Y), um ganho por eixo normaliza a
// elipse num círculo. 1.0 = sem correção. Sem isso o rumo fica correto só em
// parte da volta e "comprimido" noutra — o sintoma que resta após o hard-iron.
static float magSclX = 1.0f;
static float magSclY = 1.0f;

static const char *MAG_CAL_PATH = "/mag_cal.txt";

// Carrega offsets de calibração do LittleFS. Formato: linhas offX offY sclX sclY.
static void loadCompassCalibration() {
  magOffX = 0.0f; magOffY = 0.0f; magSclX = 1.0f; magSclY = 1.0f;
  magCalibrated = false;
  if (!isFileSystemReady()) return;
  std::vector<String> lines;
  if (!readFileLines(MAG_CAL_PATH, lines)) return;
  if (lines.size() >= 2) {
    magOffX = lines[0].toFloat();
    magOffY = lines[1].toFloat();
    // Escalas são opcionais (compat com arquivo antigo de 2 linhas): default 1.0.
    if (lines.size() >= 4) {
      float sx = lines[2].toFloat();
      float sy = lines[3].toFloat();
      if (sx > 0.01f) magSclX = sx;
      if (sy > 0.01f) magSclY = sy;
    }
    magCalibrated = true;
    Serial.printf("[MAG] calibracao carregada: offX=%.1f offY=%.1f sclX=%.3f sclY=%.3f\n",
                  magOffX, magOffY, magSclX, magSclY);
  }
}

// Persiste offsets + escalas no LittleFS (no-op seguro se o FS não montou).
static void saveCompassCalibration() {
  if (!isFileSystemReady()) {
    Serial.println("[MAG] FS indisponivel — calibracao NAO foi salva.");
    return;
  }
  std::vector<String> lines;
  lines.push_back(String(magOffX, 2));
  lines.push_back(String(magOffY, 2));
  lines.push_back(String(magSclX, 4));
  lines.push_back(String(magSclY, 4));
  if (writeFileLines(MAG_CAL_PATH, lines)) {
    Serial.println("[MAG] calibracao salva no LittleFS.");
  } else {
    Serial.println("[MAG] FALHA ao salvar calibracao.");
  }
}

// Lê rawX/rawY crus do magnetômetro detectado (sem aplicar offset). Retorna
// false em erro de barramento ou dado zerado. Usado por readCompass e pela
// rotina de calibração.
static bool readMagRaw(int16_t &rawX, int16_t &rawY) {
  if (!compassReady || compassChip == COMPASS_NONE) return false;

  if (compassChip == COMPASS_HMC5883L) {
    // HMC5883L: dados a partir de 0x03, big-endian, ordem X, Z, Y.
    Wire.beginTransmission(compassAddr);
    Wire.write(0x03);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(compassAddr, (uint8_t)6) != 6) return false;
    rawX = (int16_t)((Wire.read() << 8) | Wire.read());        // X
    int16_t rawZ = (int16_t)((Wire.read() << 8) | Wire.read()); // Z
    rawY = (int16_t)((Wire.read() << 8) | Wire.read());        // Y
    (void)rawZ;
    if (rawX == -4096 || rawY == -4096) return false;          // saturação
  } else {
    // QMC5883L (0x0D ou clone 0x2C): dados de 0x00, little-endian, X, Y, Z.
    Wire.beginTransmission(compassAddr);
    Wire.write(QMC5883L_REG_DATA);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(compassAddr, (uint8_t)6) != 6) return false;
    rawX = (int16_t)(Wire.read() | (Wire.read() << 8));
    rawY = (int16_t)(Wire.read() | (Wire.read() << 8));
    int16_t rawZ = (int16_t)(Wire.read() | (Wire.read() << 8));
    (void)rawZ;
  }

  if (rawX == 0 && rawY == 0) return false;  // dado não pronto / chip mudo
  return true;
}

// Detecta se um endereço responde (ACK) no barramento I2C.
static bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

// Init do HMC5883L genuíno (0x1E): CRA=0x70, CRB=0xA0, MODE=0x00 (contínuo).
static bool initHMC5883L(uint8_t addr) {
  Wire.beginTransmission(addr); Wire.write(0x00); Wire.write(0x70);
  if (Wire.endTransmission() != 0) return false;
  Wire.beginTransmission(addr); Wire.write(0x01); Wire.write(0xA0);
  if (Wire.endTransmission() != 0) return false;
  Wire.beginTransmission(addr); Wire.write(0x02); Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  return true;
}

// Init do QMC5883L (0x0D padrão ou clone 0x2C): reset → limpa reset →
// set/reset period → CONFIG1 (contínuo, 200Hz, ±8G, OSR512) com retry.
static bool initQMC5883L(uint8_t addr) {
  Wire.beginTransmission(addr); Wire.write(QMC5883L_REG_CONFIG2); Wire.write(0x80);
  if (Wire.endTransmission() != 0) return false;
  delay(10);
  Wire.beginTransmission(addr); Wire.write(QMC5883L_REG_CONFIG2); Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  Wire.beginTransmission(addr); Wire.write(QMC5883L_REG_SETRESET); Wire.write(0x01);
  if (Wire.endTransmission() != 0) return false;

  for (uint8_t attempt = 0; attempt < 5; attempt++) {
    Wire.beginTransmission(addr); Wire.write(QMC5883L_REG_CONFIG1);
    Wire.write(QMC5883L_CONFIG1_VAL);
    if (Wire.endTransmission() != 0) { continue; }
    delay(10);
    Wire.beginTransmission(addr); Wire.write(QMC5883L_REG_CONFIG1);
    uint8_t rb = 0;
    if (Wire.endTransmission(false) == 0 &&
        Wire.requestFrom(addr, (uint8_t)1) == 1) {
      rb = Wire.read();
    }
    if (rb == QMC5883L_CONFIG1_VAL) return true;  // modo confirmado
    delay(20);
  }
  return false;  // bit de MODE não fixou (clone incompatível)
}

bool initCompass() {
  // AUTO-DETECÇÃO: procura o magnetômetro nos três endereços conhecidos e usa o
  // protocolo certo para cada chip. Ordem de preferência: HMC genuíno (0x1E),
  // QMC padrão (0x0D), clone (0x2C).
  struct { uint8_t addr; CompassChip type; const char *name; } candidates[] = {
    { HMC5883L_ADDRESS,     COMPASS_HMC5883L, "HMC5883L(0x1E)" },
    { QMC5883L_ADDRESS_STD, COMPASS_QMC5883L, "QMC5883L(0x0D)" },
    { QMC5883L_ADDRESS,     COMPASS_QMC5883L, "QMC5883L/clone(0x2C)" },
  };

  for (auto &c : candidates) {
    if (!i2cPresent(c.addr)) continue;
    bool ok = (c.type == COMPASS_HMC5883L) ? initHMC5883L(c.addr)
                                           : initQMC5883L(c.addr);
    Serial.printf("[MAG] %s detectado em 0x%02X — init %s\n",
                  c.name, c.addr, ok ? "OK" : "FALHOU (chip nao mede)");
    if (ok) {
      compassChip = c.type;
      compassAddr = c.addr;
      compassReady = true;
      loadCompassCalibration();
      if (!magCalibrated) {
        Serial.println("[MAG] SEM calibracao — rode 'calibrar bussola' e gire 360. Rumo cru pode 'dobrar' no meio da volta.");
      }
      return true;
    }
  }

  Serial.println("[MAG] nenhum magnetometro utilizavel — rumo pelo curso do GPS.");
  compassChip = COMPASS_NONE;
  compassAddr = 0;
  compassReady = false;
  return false;
}

bool initHardwareSensors() {
  Serial.println("Inicializando sensores de hardware...");
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  // Clock I2C conservador (100kHz). Se o barramento tiver pull-up fraco, a taxa
  // padrão pode corromper escritas de config (bit de MODE do magnetômetro cai).
  Wire.setClock(100000);

#if I2C_SCAN
  // Varredura de diagnóstico do barramento I2C. Lista quem responde em
  // 0x01..0x7E. Ajuda a separar pull-up/fiação (nada responde) de módulo errado
  // (QMC5883L responde 0x0D em vez do HMC 0x1E).
  {
    Serial.printf("[I2C-SCAN] varrendo SDA=GPIO%d SCL=GPIO%d...\n",
                  I2C_SDA_PIN, I2C_SCL_PIN);
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 0x7F; addr++) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) {
        Serial.printf("[I2C-SCAN]   dispositivo em 0x%02X%s\n", addr,
                      addr == 0x1E ? "  (HMC5883L)" :
                      addr == 0x0D ? "  (QMC5883L — endereco diferente do config!)" :
                      addr == 0x3C ? "  (OLED SSD1306)" : "");
        found++;
      }
    }
    if (found == 0) {
      Serial.println("[I2C-SCAN]   NENHUM dispositivo — verificar pull-ups (4.7k p/ 3.3V), fiacao SDA/SCL e alimentacao do modulo.");
    } else {
      Serial.printf("[I2C-SCAN] %u dispositivo(s) no barramento.\n", found);
    }
  }
#endif
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

  // Dois sensores ultrassônicos de proa (esquerdo/direito). Trig = saída, Echo =
  // entrada. Alimentados a 3.3V → Echo em 3.3V, sem divisor (sem pull no Echo).
  pinMode(ULTRASONIC_L_TRIG_PIN, OUTPUT);
  pinMode(ULTRASONIC_L_ECHO_PIN, INPUT);
  digitalWrite(ULTRASONIC_L_TRIG_PIN, LOW);
  pinMode(ULTRASONIC_R_TRIG_PIN, OUTPUT);
  pinMode(ULTRASONIC_R_ECHO_PIN, INPUT);
  digitalWrite(ULTRASONIC_R_TRIG_PIN, LOW);

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
  // Coast: ambos os IN em LOW (sem frear ativamente).
  digitalWrite(MOTOR_LEFT_IN1_PIN,  LOW);
  digitalWrite(MOTOR_LEFT_IN2_PIN,  LOW);
  digitalWrite(MOTOR_RIGHT_IN3_PIN, LOW);
  digitalWrite(MOTOR_RIGHT_IN4_PIN, LOW);
}

void updateMotorOutputs() {
  // thrustL/thrustR são COM SINAL: >=0 frente, <0 ré. O sentido vai nos pinos
  // IN do L298N; o PWM (ENA/ENB) recebe apenas a magnitude (0..255).
  setMotorDirection(MOTOR_LEFT_IN1_PIN,  MOTOR_LEFT_IN2_PIN,  thrustL >= 0);
  setMotorDirection(MOTOR_RIGHT_IN3_PIN, MOTOR_RIGHT_IN4_PIN, thrustR >= 0);
  ledcWrite(MOTOR_LEFT_CHANNEL,  constrain(abs(thrustL), 0, 255));
  ledcWrite(MOTOR_RIGHT_CHANNEL, constrain(abs(thrustR), 0, 255));
}

// Dispara UM sensor ultrassônico e retorna a distância em cm, ou -1 em timeout
// (sem eco válido). pulseIn é bloqueante — timeout limita o impacto.
//
// AJ-SR04M (boas práticas de aplicação):
//  - Trigger: pulso de 10us, precedido de LOW firme (settle) — padrão do módulo.
//  - Conversão: distancia_cm = tempo_us * SOUND_CM_PER_US / 2. O /2 é ida+volta.
//    SOUND_CM_PER_US = 0.0343 cm/us (343 m/s a ~20C) — constante explícita, não
//    o "0.034" arredondado, para não acumular viés sistemático.
//  - Faixa útil: ecos abaixo da zona morta (~20cm) ou acima do alcance são
//    descartados (retorno fora de [MIN,MAX] vira amostra inválida).
//  - Robustez a ruído/eco espúrio: coleta N amostras rápidas e usa a MEDIANA
//    (não a média) — um eco falso isolado não contamina a mediana. Isto é a
//    prática recomendada para HC-SR04/AJ-SR04M em ambiente ruidoso.
static int pingOnce(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(4);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, ULTRASONIC_TIMEOUT_US);
  if (duration == 0) {
    return -1;  // timeout: sem eco
  }
  // 0.0343 cm/us (343 m/s). Divisão por 2 = ida e volta.
  int cm = (int)((duration * SOUND_CM_PER_US) / 2.0f + 0.5f);
  return cm;
}

static int pingUltrasonic(int trigPin, int echoPin) {
  // Coleta ULTRASONIC_SAMPLES amostras e retorna a MEDIANA das válidas.
  int samples[ULTRASONIC_SAMPLES];
  int n = 0;
  for (int i = 0; i < ULTRASONIC_SAMPLES; i++) {
    int cm = pingOnce(trigPin, echoPin);
    // Mantém só ecos plausíveis: dentro de [MIN_CM, MAX_CM]. Fora disso é zona
    // morta (eco cedo demais) ou fora de alcance (eco tarde/ausente) — ruído.
    if (cm >= ULTRASONIC_MIN_CM && cm <= ULTRASONIC_MAX_CM) {
      samples[n++] = cm;
    }
    delayMicroseconds(ULTRASONIC_INTER_PING_US);  // deixa o eco anterior decair
  }
  if (n == 0) {
    return -1;  // nenhuma amostra válida
  }
  // Mediana por insertion sort (n pequeno).
  for (int i = 1; i < n; i++) {
    int key = samples[i], j = i - 1;
    while (j >= 0 && samples[j] > key) { samples[j + 1] = samples[j]; j--; }
    samples[j + 1] = key;
  }
  return samples[n / 2];
}

void readUltrasonic() {
  // Sensor ESQUERDO (bombordo). Em timeout mantém o último valor e marca falha.
  int dL = pingUltrasonic(ULTRASONIC_L_TRIG_PIN, ULTRASONIC_L_ECHO_PIN);
  if (dL >= 0) {
    obsDistL = dL;
    ultrasonicLHealthy = true;
  } else {
    ultrasonicLHealthy = false;
  }

  // Sensor DIREITO (estibordo). Mesmo tratamento.
  int dR = pingUltrasonic(ULTRASONIC_R_TRIG_PIN, ULTRASONIC_R_ECHO_PIN);
  if (dR >= 0) {
    obsDistR = dR;
    ultrasonicRHealthy = true;
  } else {
    ultrasonicRHealthy = false;
  }

  // obsDist (compat): obstáculo MAIS PRÓXIMO das duas leituras de proa. Todos os
  // consumidores antigos (gate de OBSTACLE_AVOIDANCE, telemetria, log) continuam
  // lendo obsDist com semântica "distância ao obstáculo mais perto".
  obsDist = min(obsDistL, obsDistR);
  // Saúde agregada: saudável se PELO MENOS um sensor deu eco válido.
  ultrasonicHealthy = ultrasonicLHealthy || ultrasonicRHealthy;
}

bool readCompass() {
  if (!compassReady || compassChip == COMPASS_NONE) {
    return false;
  }

  int16_t rawX, rawY;
  if (!readMagRaw(rawX, rawY)) {
    return false;
  }

  // Aplica a calibração hard-iron (offset: centra o círculo na origem) e, em
  // seguida, soft-iron (escala por eixo: normaliza a elipse num círculo). Sem o
  // offset o atan2 "dobra" no meio da volta; sem a escala o rumo fica comprimido
  // num trecho da volta e esticado noutro.
  double x = ((double)rawX - magOffX) * magSclX;
  double y = ((double)rawY - magOffY) * magSclY;

  double headingDegrees = atan2(y, x) * 180.0 / PI;

  // Correção de declinação: converte norte MAGNÉTICO em norte GEOGRÁFICO, o mesmo
  // referencial do curso do GPS e das rotas LOS. Em Manaus a declinação é Leste
  // (+), então soma-se ao rumo magnético.
  headingDegrees += MAG_DECLINATION_DEG;

  // Normaliza para [0, 360) — a soma da declinação pode estourar os limites.
  while (headingDegrees < 0.0)    headingDegrees += 360.0;
  while (headingDegrees >= 360.0) headingDegrees -= 360.0;

  currentHeading = headingDegrees;
  return true;
}

// Rotina interativa de calibração hard-iron. Gire o USV lentamente >=1 volta
// completa (de preferência 2) durante ~CAL_SECONDS. Coleta min/max de cada eixo,
// calcula o centro (offset) e persiste no LittleFS. Bloqueante por design — é um
// comando de manutenção, não roda no loop de navegação.
bool calibrateCompass() {
  if (!compassReady || compassChip == COMPASS_NONE) {
    Serial.println("[MAG-CAL] bussola nao inicializada — abortado.");
    return false;
  }

  const uint32_t CAL_MS = 25000;  // 25s de coleta
  int16_t xMin = INT16_MAX, xMax = INT16_MIN;
  int16_t yMin = INT16_MAX, yMax = INT16_MIN;

  Serial.println("[MAG-CAL] ===== CALIBRACAO INICIADA =====");
  Serial.println("[MAG-CAL] GIRE o USV LENTAMENTE 1-2 voltas completas (360) nos proximos 25s...");
  displayShowCalibrating((int)(CAL_MS / 1000), 0, 0);

  uint32_t t0 = millis();
  uint32_t lastLog = 0;
  uint32_t samples = 0;
  while (millis() - t0 < CAL_MS) {
    int16_t rx, ry;
    if (readMagRaw(rx, ry)) {
      if (rx < xMin) xMin = rx;
      if (rx > xMax) xMax = rx;
      if (ry < yMin) yMin = ry;
      if (ry > yMax) yMax = ry;
      samples++;
    }
    // Progresso a cada 2s, mostrando a amplitude capturada até agora (serial + OLED).
    if (millis() - lastLog >= 2000) {
      lastLog = millis();
      uint32_t restante = (CAL_MS - (millis() - t0)) / 1000;
      Serial.printf("[MAG-CAL] %2lus restantes | X[%d..%d] Y[%d..%d] amostras=%lu\n",
                    (unsigned long)restante, xMin, xMax, yMin, yMax,
                    (unsigned long)samples);
      long sx = (xMax > xMin) ? (long)xMax - xMin : 0;
      long sy = (yMax > yMin) ? (long)yMax - yMin : 0;
      displayShowCalibrating((int)restante, sx, sy);
    }
    delay(20);  // ~50Hz de amostragem
  }

  // Validação: amplitude mínima para uma volta real ter sido feita. Um giro
  // completo num campo normal gera centenas de contagens de span em cada eixo.
  int32_t xSpan = (int32_t)xMax - xMin;
  int32_t ySpan = (int32_t)yMax - yMin;
  Serial.printf("[MAG-CAL] coleta encerrada: X span=%ld Y span=%ld amostras=%lu\n",
                (long)xSpan, (long)ySpan, (unsigned long)samples);

  if (samples < 50 || xSpan < 50 || ySpan < 50) {
    Serial.println("[MAG-CAL] amplitude INSUFICIENTE — gire mais e repita. Calibracao NAO aplicada.");
    displayShowCalResult(false, true);
    return false;
  }

  // Hard-iron offset = centro do círculo (ponto médio de min/max por eixo).
  magOffX = (xMax + xMin) / 2.0f;
  magOffY = (yMax + yMin) / 2.0f;

  // Soft-iron: normaliza os dois eixos ao MAIOR span, transformando a elipse num
  // círculo. Eixo de menor span recebe ganho > 1; o de maior fica em 1.0.
  float avgSpan = (xSpan + ySpan) / 2.0f;
  magSclX = (xSpan > 0) ? (avgSpan / (float)xSpan) : 1.0f;
  magSclY = (ySpan > 0) ? (avgSpan / (float)ySpan) : 1.0f;
  magCalibrated = true;

  // Razão de span: ~1.0 = resposta circular (bom). Muito longe de 1 (>1.3 ou
  // <0.77) indica soft-iron forte OU placa inclinada (tilt) durante o giro — se
  // for tilt, nivele melhor a placa e refaça; a escala corrige soft-iron, não tilt.
  float ratio = (ySpan > 0) ? (float)xSpan / (float)ySpan : 0.0f;
  Serial.printf("[MAG-CAL] offset: offX=%.1f offY=%.1f | escala: sclX=%.3f sclY=%.3f | razao X/Y=%.2f\n",
                magOffX, magOffY, magSclX, magSclY, ratio);
  if (ratio > 1.4f || (ratio > 0.01f && ratio < 0.71f)) {
    Serial.println("[MAG-CAL] AVISO: resposta bem eliptica. Se a placa nao estava NIVELADA no giro, nivele e refaca (tilt nao e corrigido pela escala).");
  }
  saveCompassCalibration();
  Serial.println("[MAG-CAL] ===== CALIBRACAO CONCLUIDA — rumo deve varrer 360 corretamente =====");
  displayShowCalResult(true, true);
  return true;
}

bool isCompassCalibrated() {
  return magCalibrated;
}

// ─────────────────────────────────────────────────────────────────────────────
// SELF-TEST DOS SENSORES — confirma resposta e leituras plausíveis, sem mover o
// barco e sem calibrar. Reaproveita readMagRaw(), pingUltrasonic() e os contadores
// de GPS já existentes.
// ─────────────────────────────────────────────────────────────────────────────
bool runSensorSelfTest() {
  Serial.println("[SELFTEST] ===== VERIFICACAO DE SENSORES =====");
  bool allOk = true;

  // ── Bússola: deve inicializar e entregar uma leitura crua não-nula. ──
  if (compassReady && compassChip != COMPASS_NONE) {
    int16_t rx, ry;
    bool magOk = readMagRaw(rx, ry);
    Serial.printf("[SELFTEST] Bussola  : %s (chip detectado em 0x%02X, rawX=%d rawY=%d, calibrada=%s)\n",
                  magOk ? "OK" : "FALHA leitura", compassAddr, rx, ry,
                  magCalibrated ? "sim" : "NAO");
    if (!magOk) allOk = false;
  } else {
    Serial.println("[SELFTEST] Bussola  : AUSENTE (nenhum magnetometro detectado) — rumo pelo curso do GPS");
    // Bússola ausente NÃO reprova o self-test: o firmware degrada para curso GPS.
  }

  // ── Ultrassônicos de proa: eco plausível (2..ULTRASONIC_MAX_CM). ──
  // 3 tentativas por sensor — um timeout isolado não reprova (eco perdido é comum).
  auto testUltra = [](const char *nome, int trig, int echo) -> bool {
    for (uint8_t i = 0; i < 3; i++) {
      int d = pingUltrasonic(trig, echo);
      if (d >= 2 && d <= ULTRASONIC_MAX_CM) {
        Serial.printf("[SELFTEST] Sonar %s : OK (%d cm)\n", nome, d);
        return true;
      }
      delay(60);
    }
    Serial.printf("[SELFTEST] Sonar %s : FALHA (sem eco valido em 3 tentativas)\n", nome);
    return false;
  };
  bool ulOk = testUltra("L", ULTRASONIC_L_TRIG_PIN, ULTRASONIC_L_ECHO_PIN);
  bool urOk = testUltra("R", ULTRASONIC_R_TRIG_PIN, ULTRASONIC_R_ECHO_PIN);
  if (!ulOk || !urOk) allOk = false;

  // ── GPS: confirma RECEPCAO de bytes NMEA numa janela curta (não exige fix,
  //    que depende de céu aberto). Lê a Serial1 por ~1.5s contando ASCII imprimível.
  {
    while (Serial1.available()) Serial1.read();  // limpa buffer
    unsigned long t = millis();
    unsigned long bytes = 0, nmea = 0;
    while (millis() - t < 1500) {
      if (Serial1.available()) {
        char c = (char)Serial1.read();
        bytes++;
        if ((c >= 0x20 && c <= 0x7E) || c == '\n' || c == '\r') nmea++;
      }
    }
    bool gpsOk = (nmea > 0);
    Serial.printf("[SELFTEST] GPS      : %s (bytes=%lu, nmea_like=%lu em 1.5s)\n",
                  gpsOk ? "OK (recebendo)" : "FALHA (sem dados — verificar fio/energia)",
                  bytes, nmea);
    if (!gpsOk) allOk = false;
  }

  Serial.printf("[SELFTEST] ===== RESULTADO: %s =====\n",
                allOk ? "TODOS OS SENSORES OK" : "HA FALHAS (ver linhas acima)");
  return allOk;
}

// Rotina unificada de boot e botão: verifica os sensores e calibra a bússola.
//   forceCalibration=true  -> sempre calibra (botão: pressão longa).
//   forceCalibration=false -> calibra só se NAO houver calibração salva (boot).
void runCalibrationAndCheck(bool forceCalibration) {
  stopMotors();  // segurança: motores desligados durante a rotina.
  displayShowSelfTest();
  bool stOk = runSensorSelfTest();

  bool temBussola = compassReady && compassChip != COMPASS_NONE;
  if (temBussola && (forceCalibration || !magCalibrated)) {
    if (forceCalibration) {
      Serial.println("[CAL] recalibracao FORCADA — gire o barco.");
    } else {
      Serial.println("[CAL] sem calibracao salva — calibrando a bussola no boot.");
    }
    calibrateCompass();   // já mostra progresso e resultado no OLED
  } else {
    if (temBussola) {
      Serial.println("[CAL] bussola ja calibrada (use pressao LONGA no botao para recalibrar).");
    }
    // Sem calibração nesta passada → mostra o resultado do self-test no OLED.
    displayShowCalResult(stOk, false);
  }
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
  Serial.printf("│ Ultrassom: L=%s R=%s\n",
                ultrasonicLHealthy ? "OK" : "FALHA",
                ultrasonicRHealthy ? "OK" : "FALHA");
  // 2. Localização + fix
  if (hasGpsFix) {
    Serial.printf("│ Posicao  : lat=%.6f lon=%.6f  FIX=SIM\n", currentLat, currentLon);
  } else {
    Serial.printf("│ Posicao  : --- (sem fix)          FIX=NAO\n");
  }
  // 3. Distância dos ultrassônicos de proa (esquerdo/direito) + o mais próximo.
  Serial.printf("│ Obstaculo: L=%s R=%s | min=%d cm\n",
                ultrasonicLHealthy ? String(obsDistL).c_str() : "---",
                ultrasonicRHealthy ? String(obsDistR).c_str() : "---",
                obsDist);
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
