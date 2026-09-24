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

// I2C (Bússola HMC5883L)
#define I2C_SDA_PIN       21
#define I2C_SCL_PIN       22
#define HMC5883L_ADDRESS  0x1E

// Ultrassônico HC-SR04 — pinos imunes ao WiFi:
//   Trig = GPIO5 (I/O livre, boot-safe) — saída de disparo.
//   Echo = GPIO35 (ADC1, input-only) — entrada; USAR DIVISOR 5V→3.3V.
#define ULTRASONIC_TRIG_PIN  5
#define ULTRASONIC_ECHO_PIN  35

// Motores (PWM)
#define MOTOR_LEFT_PIN       32
#define MOTOR_RIGHT_PIN      33
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
// PARÂMETROS DE NAVEGAÇÃO (LOS)
// ─────────────────────────────────────────────────────────────────────────────
#define LOS_LOOKAHEAD_METERS       8.0
#define LOS_HEADING_GAIN           1.5
#define NAV_BASE_THRUST            120
#define ARRIVAL_RADIUS_METERS      4.0   // dentro deste raio do alvo, missão concluída
#define OBSTACLE_THRESHOLD_CM      60
#define OBSTACLE_CLEAR_CM          120
#define OBSTACLE_AVOIDANCE_TIMEOUT_MS 8000

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
