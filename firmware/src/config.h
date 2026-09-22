#pragma once
/**
 * config.h — Configuração centralizada do firmware USV-AM
 *
 * Todos os parâmetros de hardware, rede e tuning em um só lugar.
 * Altere aqui para adaptar a diferentes hardwares ou ambientes.
 */

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// REDE WiFi
// ─────────────────────────────────────────────────────────────────────────────
#ifndef WIFI_SSID
#define WIFI_SSID "orlando_redmi"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "Or@91861882"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// FIREBASE
// ─────────────────────────────────────────────────────────────────────────────
#ifndef FIREBASE_DATABASE_URL
#define FIREBASE_DATABASE_URL "https://usvs-drone-fluvial-autonomo-default-rtdb.firebaseio.com/"
#endif

#ifndef FIREBASE_API_KEY
#define FIREBASE_API_KEY "AIzaSyCxdGSogOdPjuckQLZsW2RzpKrltlbBBmw"
#endif

#ifndef FIREBASE_USER_EMAIL
#define FIREBASE_USER_EMAIL "operador@usv-am.local"
#endif

#ifndef FIREBASE_USER_PASSWORD
#define FIREBASE_USER_PASSWORD "DevTest123!"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// IDENTIFICAÇÃO DO DRONE
// ─────────────────────────────────────────────────────────────────────────────
#define DRONE_ID "drone_01"

// ─────────────────────────────────────────────────────────────────────────────
// PINOS DE HARDWARE
// ─────────────────────────────────────────────────────────────────────────────
// GPS (UART2) — movido para GPIO 25/26 na F1 (16/17 liberados para o link RPi)
#define GPS_RX_PIN        25
#define GPS_TX_PIN        26
#define GPS_BAUD          9600

// ─────────────────────────────────────────────────────────────────────────────
// LINK COM RASPBERRY PI 4 (UART — Serial1 nos GPIO 16/17)
// ─────────────────────────────────────────────────────────────────────────────
// ESP32 Serial1: RX = GPIO16 (recebe do TX do RPi), TX = GPIO17 (envia ao RX do RPi)
// 3.3V direto, sem level shifter. Protocolo: JSON-lines a 115200 baud.
#define RPI_LINK_RX_PIN   16
#define RPI_LINK_TX_PIN   17
#define RPI_LINK_BAUD     115200
// Timeout: se nenhuma mensagem do RPi chegar nesse período, o ESP32 assume que
// está sozinho (modo autônomo) e volta a publicar direto no Firebase.
#define RPI_LINK_TIMEOUT_MS  30000

// I2C (Bússola HMC5883L)
#define I2C_SDA_PIN       21
#define I2C_SCL_PIN       22
#define HMC5883L_ADDRESS  0x1E

// Ultrassônico HC-SR04
#define ULTRASONIC_TRIG_PIN  26
#define ULTRASONIC_ECHO_PIN  27

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
#define STATUS_INTERVAL_MS      2000
#define COMMAND_INTERVAL_MS     1000
#define WIFI_CONNECT_TIMEOUT_MS 10000

// ─────────────────────────────────────────────────────────────────────────────
// PARÂMETROS DE NAVEGAÇÃO (LOS)
// ─────────────────────────────────────────────────────────────────────────────
#define LOS_LOOKAHEAD_METERS       8.0
#define LOS_HEADING_GAIN           1.5
#define NAV_BASE_THRUST            120
#define OBSTACLE_THRESHOLD_CM      60
#define OBSTACLE_CLEAR_CM          120
#define OBSTACLE_AVOIDANCE_TIMEOUT_MS 8000

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
