#pragma once
/**
 * serial_link — Camada de comunicação UART com o Raspberry Pi 4.
 *
 * Protocolo: JSON-lines (uma mensagem JSON por linha, terminada em '\n') a 115200 baud.
 * O ESP32 é a fonte de verdade do controle real-time; o RPi é aditivo (Firebase, missão).
 *
 * Invariante de autonomia: se o link com o RPi cair (nenhuma mensagem por
 * RPI_LINK_TIMEOUT_MS), isRpiPresent() passa a retornar false e o firmware
 * retoma a publicação direta no Firebase + buffering offline.
 *
 * Mensagens RPi -> ESP32 (comandos):
 *   {"cmd":"set_destination","command_id":"cmd_1","mission_id":"m_1","lat":-3.105,"lon":-60.03}
 *   {"cmd":"emergency_stop","command_id":"cmd_2","mission_id":"m_1"}
 *   {"cmd":"request_telemetry"}
 *   {"cmd":"ping"}
 *
 * Mensagens ESP32 -> RPi:
 *   {"type":"telemetry","ts":..,"lat":..,"lon":..,"hdg":..,"obs":..,"bat":..,
 *    "thrust_l":..,"thrust_r":..,"state":"..","mission_id":"..","progress":..}
 *   {"type":"ack","command_id":"cmd_1","ok":true}
 *   {"type":"event","event":"obstacle_detected","dist":45}
 *   {"type":"pong"}
 */

#include <Arduino.h>

// Inicializa a UART do link (Serial2 nos pinos definidos em config.h).
void initSerialLink();

// Lê e processa mensagens pendentes do RPi (não-bloqueante). Deve ser chamada
// no loop principal. Comandos válidos são despachados para o módulo commands.
void processSerialLink();

// true se o RPi enviou alguma mensagem dentro de RPI_LINK_TIMEOUT_MS.
bool isRpiPresent();

// Envia o snapshot de telemetria atual ao RPi (JSON-line).
void sendTelemetryToRpi();

// Envia um evento operacional ao RPi (ex.: "obstacle_detected").
void sendEventToRpi(const char *event, double value);
