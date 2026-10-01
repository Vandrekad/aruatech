#pragma once
/**
 * config.h — Configuração centralizada do firmware USV-AM
 *
 * Todos os parâmetros de hardware, rede e tuning em um só lugar.
 * Altere aqui para adaptar a diferentes hardwares ou ambientes.
 */

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// REDE / NUVEM
// ─────────────────────────────────────────────────────────────────────────────
// Arquitetura dual: o ESP32 NÃO fala WiFi nem Firebase. A camada de nuvem
// (WiFi, Firebase RTDB, autenticação) é responsabilidade EXCLUSIVA do Raspberry
// Pi 4, que se comunica com o ESP32 por UART. Nenhuma credencial de rede vive
// mais no firmware.

// ─────────────────────────────────────────────────────────────────────────────
// IDENTIFICAÇÃO DO DRONE
// ─────────────────────────────────────────────────────────────────────────────
#define DRONE_ID "drone_01"

// ─────────────────────────────────────────────────────────────────────────────
// DIAGNÓSTICO DE GPS (temporário)
// ─────────────────────────────────────────────────────────────────────────────
// Com 1: despeja no Serial USB tudo que chega bruto na Serial1 do GPS.
//   - aparece "$GPRMC..."  -> GPS OK, baud certo (problema era só o fix/parsing)
//   - aparece lixo/caracteres -> baud errado (testar 38400/115200 em GPS_BAUD)
//   - nada, com fio e LED do GPS piscando -> pino físico / GND / RX-TX invertido
// Também sonda automaticamente 9600/38400/115200 no boot. Voltar a 0 em produção.
#define GPS_ECHO_RAW  0

// ─────────────────────────────────────────────────────────────────────────────
// PINOS DE HARDWARE
// ─────────────────────────────────────────────────────────────────────────────
// GPS (Serial1):
//   RX = GPIO4  — I/O pleno COM pull-up interno. Escolhido no lugar do GPIO34:
//        o 34 é input-only e SEM pull-up, então quando o fio TX do GPS não faz
//        contato firme o pino flutua e capta ruído (bytes_rx sobe, nmea_like=0).
//        O pull-up do GPIO4 mantém o nível idle-alto e elimina esse ruído.
//        (ADC2 não importa mais: o WiFi foi removido do ESP32.)
//   TX = GPIO13 — I/O livre, boot-safe; saída ao RX do GPS (raramente usado).
#define GPS_RX_PIN        4
#define GPS_TX_PIN        13
#define GPS_BAUD          9600

// ─────────────────────────────────────────────────────────────────────────────
// LINK COM RASPBERRY PI 4 (UART — Serial2 nos GPIO 16/17)
// ─────────────────────────────────────────────────────────────────────────────
// ESP32 Serial2: RX = GPIO16 (recebe do TX do RPi), TX = GPIO17 (envia ao RX do RPi)
// 3.3V direto, sem level shifter. Protocolo: JSON-lines a 115200 baud.
#define RPI_LINK_RX_PIN   16
#define RPI_LINK_TX_PIN   17
#define RPI_LINK_BAUD     115200
// Timeout: se nenhuma mensagem do RPi chegar nesse período, o ESP32 assume que
// está sozinho (modo autônomo) e passa a bufferizar a telemetria em LittleFS
// local; a navegação continua normalmente (autonomia independe do RPi).
#define RPI_LINK_TIMEOUT_MS  30000

// I2C (Bússola magnetômetro)
#define I2C_SDA_PIN       21
#define I2C_SCL_PIN       22
// O módulo detectado pelo I2C-SCAN responde em 0x2C — é um QMC5883L (clone),
// NÃO um HMC5883L (que seria 0x1E). Registradores e sequência de init são
// diferentes do HMC. Mantido o endereço 0x1E como referência histórica.
#define HMC5883L_ADDRESS  0x1E            // HMC5883L genuíno
#define QMC5883L_ADDRESS  0x2C            // módulo real presente no barramento
#define QMC5883L_ADDRESS_STD 0x0D         // QMC5883L genuíno (endereço padrão)
// Mapa de registradores do QMC5883L:
#define QMC5883L_REG_DATA     0x00        // X_LSB,X_MSB,Y_LSB,Y_MSB,Z_LSB,Z_MSB
#define QMC5883L_REG_STATUS   0x06        // bit0 = DRDY (dado pronto)
#define QMC5883L_REG_CONFIG1  0x09        // OSR/RNG/ODR/MODE
#define QMC5883L_REG_CONFIG2  0x0A        // soft reset / rol_pnt
#define QMC5883L_REG_SETRESET 0x0B        // período set/reset (recomendado 0x01)
// CONFIG1: OSR=512(0b00<<6) | RNG=8G(0b01<<4) | ODR=200Hz(0b11<<2) | MODE=cont(0b01)
#define QMC5883L_CONFIG1_VAL  0x1D
// Declinação magnética de MANAUS/AM (lat -3.11, lon -59.93), modelo WMM 2025:
// ~ +6.8° LESTE. Rumo verdadeiro = rumo magnético + declinação (Leste é positivo).
// Converte o norte magnético (que a bússola mede) para o norte GEOGRÁFICO, que é
// o referencial do curso do GPS e das rotas LOS. Atualize se mudar de região.
#define MAG_DECLINATION_DEG   6.8f
// Diagnóstico: com 1, varre o barramento I2C no boot e lista os endereços que
// respondem. Voltar a 0 em produção.
#define I2C_SCAN          1

// OLED SSD1306 (mesmo barramento I2C, endereço 0x3C — visto no I2C-SCAN).
#define OLED_ADDRESS      0x3C
#define OLED_WIDTH        128
// ALTURA do painel — TROQUE conforme o seu módulo físico:
//   64 → OLED 0.96" (128x64).   32 → OLED 0.91" (128x32).
// Se o HUD aparece cortado/espremido, quase sempre é este valor errado: o
// layout se adapta (6 linhas em 64, 4 compactas em 32).
#define OLED_HEIGHT       64
#define OLED_UPDATE_MS    500     // atualiza o HUD a cada 500ms

// Ultrassônicos AJ-SR04M de proa — DOIS sensores (esquerdo/direito) para
// desvio DIRECIONAL. Alimentados a 3.3V → Echo em 3.3V, SEM divisor de tensão.
//   Sensor ESQUERDO (bombordo): Trig=GPIO5, Echo=GPIO35 (o sensor que já existia).
//     GPIO35 é input-only (ADC1) — ok, o Echo só é lido.
//   Sensor DIREITO (estibordo): Trig=GPIO19, Echo=GPIO23 — I/O plenos, boot-safe.
//     NÃO usar 16/17 aqui: esses pinos ainda são do Serial2 (link RPi, ver
//     RPI_LINK_RX/TX_PIN). A migração do link para USB foi decidida no diagrama
//     mas ainda NÃO foi aplicada no firmware, então 16/17 continuam ocupados —
//     colocar o sensor lá causava conflito de barramento.
// Compat: ULTRASONIC_TRIG_PIN/ECHO_PIN continuam apontando o sensor esquerdo,
// para não quebrar referências antigas.
#define ULTRASONIC_L_TRIG_PIN  5
#define ULTRASONIC_L_ECHO_PIN  35
#define ULTRASONIC_R_TRIG_PIN  19
#define ULTRASONIC_R_ECHO_PIN  23
// Aliases legados (sensor esquerdo = o sensor único original).
#define ULTRASONIC_TRIG_PIN  ULTRASONIC_L_TRIG_PIN
#define ULTRASONIC_ECHO_PIN  ULTRASONIC_L_ECHO_PIN

// Motores (L298N — ponte H dupla, motores DC 3-6V bidirecionais)
//   ENA/ENB = velocidade (PWM); IN1..IN4 = sentido de rotação de cada motor.
//   Pino ENA/ENB (PWM):
#define MOTOR_LEFT_PIN       32   // ENA — PWM velocidade motor esquerdo
#define MOTOR_RIGHT_PIN      33   // ENB — PWM velocidade motor direito
//   Pinos de sentido (digitais). Combinação por motor: (HIGH,LOW)=frente,
//   (LOW,HIGH)=ré, (LOW,LOW)=coast/freio. GPIO14 é strapping (MTMS) mas seguro
//   como SAÍDA após o boot; troque por GPIO19/23 se quiser zero strapping.
#define MOTOR_LEFT_IN1_PIN   25   // IN1 — sentido motor esquerdo (A)
#define MOTOR_LEFT_IN2_PIN   26   // IN2 — sentido motor esquerdo (B)
#define MOTOR_RIGHT_IN3_PIN  27   // IN3 — sentido motor direito (A)
#define MOTOR_RIGHT_IN4_PIN  14   // IN4 — sentido motor direito (B)
#define MOTOR_LEFT_CHANNEL   0
#define MOTOR_RIGHT_CHANNEL  1
#define MOTOR_PWM_FREQ       5000
#define MOTOR_PWM_RES        8

// ─────────────────────────────────────────────────────────────────────────────
// INTERVALOS DE TEMPO (ms)
// ─────────────────────────────────────────────────────────────────────────────
#define TELEMETRY_INTERVAL_MS   2000
#define STATUS_LOG_INTERVAL_MS  3000

// ─────────────────────────────────────────────────────────────────────────────
// BOTÃO FÍSICO DE CALIBRAÇÃO / SELF-TEST
// ─────────────────────────────────────────────────────────────────────────────
// Botão momentâneo entre o GPIO e o GND (sem resistor externo — usa o pull-up
// interno). GPIO18: I/O pleno, boot-safe, livre (não colide com GPS 4/13, RPi
// 16/17, I2C 21/22, ultrassom 5/35/19/23 nem motores 32/33/25/26/27/14).
// Pressionar aterra o pino -> leitura LOW = acionado. Segurar não redispara: é
// preciso soltar e apertar de novo (detecção por borda + debounce).
#define CAL_BUTTON_PIN         18
#define CAL_BUTTON_DEBOUNCE_MS 50
// Pressão LONGA (>= este tempo) força recalibração completa da bússola; pressão
// CURTA roda só a verificação (self-test) dos sensores, sem girar o barco.
#define CAL_BUTTON_LONGPRESS_MS 1500

// ─────────────────────────────────────────────────────────────────────────────
// PARÂMETROS DE NAVEGAÇÃO (LOS)
// ─────────────────────────────────────────────────────────────────────────────
#define LOS_LOOKAHEAD_METERS       8.0
#define LOS_HEADING_GAIN           1.5
#define NAV_BASE_THRUST            120
#define ARRIVAL_RADIUS_METERS      4.0   // dentro deste raio do alvo, missão concluída
#define OBSTACLE_THRESHOLD_CM      60
#define OBSTACLE_CLEAR_CM          120
#define OBSTACLE_AVOIDANCE_TIMEOUT_MS 8000
// ── Desvio DIRECIONAL (dois sensores de proa) ──
// O motor EXTERNO (lado com mais espaço livre) segue avante em AVOID_OUTER_THRUST.
// O motor INTERNO entra em RÉ com magnitude proporcional a quão perto está o
// obstáculo mais próximo: perto → ré forte (pivô fechado), longe → ré suave.
#define AVOID_OUTER_THRUST         180   // empuxo do motor externo (avante)
#define AVOID_REVERSE_MIN          40    // magnitude mínima de ré do motor interno
#define AVOID_REVERSE_MAX          120   // magnitude máxima de ré (obstáculo colado)
// Banda morta de diferença entre os dois sensores: |L-R| abaixo disso é tratado
// como "obstáculo centrado" — sem lado preferencial, usa o desvio-padrão (vira
// para bombordo). Evita pivô trêmulo por ruído do AJ-SR04M.
#define AVOID_DIFF_DEADBAND_CM     8

// ─────────────────────────────────────────────────────────────────────────────
// STATION-KEEPING (IDLE_HOLDING_POSITION) — manter posição contra correnteza
// ─────────────────────────────────────────────────────────────────────────────
// Ao entrar em IDLE, o firmware ancora a posição atual (holdLat/holdLon) e a
// mantém ativamente: dentro do raio de banda morta os motores ficam em 0; fora
// dele o barco aponta de volta para a âncora com propulsão diferencial
// proporcional ao desvio. Só atua com GPS fix — sem fix, motores em 0 (seguro).
#define HOLD_DEADBAND_METERS       3.0   // dentro deste raio, considera-se "no lugar"
#define HOLD_HEADING_GAIN          1.5   // ganho angular (reaproveita a lógica LOS)
#define HOLD_BASE_THRUST           90    // empuxo de correção quando fora do raio
#define HOLD_MAX_THRUST            160   // teto de empuxo em correção de deriva

// ─────────────────────────────────────────────────────────────────────────────
// ARMAZENAMENTO OFFLINE
// ─────────────────────────────────────────────────────────────────────────────
#define TELEMETRY_BUFFER_PATH  "/telemetry_buffer.ndjson"
#define PATH_BUFFER_PATH       "/path_buffer.ndjson"

// ─────────────────────────────────────────────────────────────────────────────
// LIMITES DE SEGURANÇA
// ─────────────────────────────────────────────────────────────────────────────
#define GPS_LINE_MAX_LENGTH    120   // Máximo de caracteres por sentença NMEA
#define ULTRASONIC_MAX_CM      400
#define ULTRASONIC_TIMEOUT_US  25000
#define FLUSH_BATCH_SIZE       10    // Linhas por lote no flush offline
