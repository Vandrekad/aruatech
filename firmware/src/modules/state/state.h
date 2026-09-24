#pragma once

#include <Arduino.h>
#include "config.h"
#include "modules/navigation/navigation.h"

// ─────────────────────────────────────────────────────────────────────────────
// Identificação e configuração (constantes vindas de config.h)
// ─────────────────────────────────────────────────────────────────────────────
extern const String droneId;
extern const char* telemetryBufferPath;
extern const char* pathBufferPath;

// ─────────────────────────────────────────────────────────────────────────────
// Estado de navegação
// ─────────────────────────────────────────────────────────────────────────────
extern NavState currentState;
extern double currentLat;
extern double currentLon;
extern double currentHeading;
extern int batteryMv;
extern int obsDist;
extern int thrustL;
extern int thrustR;
extern String activeMissionId;
extern String lastCommandId;

// ─────────────────────────────────────────────────────────────────────────────
// Sensores
// ─────────────────────────────────────────────────────────────────────────────
extern bool hasGpsFix;
extern double gpsLat;
extern double gpsLon;
extern double gpsCourse;
extern bool compassReady;

// Saúde/diagnóstico dos sensores (atualizados na leitura; lidos pelo log).
extern unsigned long gpsBytesWindow;    // bytes brutos recebidos na última janela
extern unsigned long gpsNmeaWindow;     // bytes ASCII-NMEA na última janela
extern bool compassLastReadOk;          // última leitura da bússola teve sucesso
extern bool ultrasonicHealthy;          // último eco do HC-SR04 foi válido

// ─────────────────────────────────────────────────────────────────────────────
// Missão e rota
// ─────────────────────────────────────────────────────────────────────────────
extern double goalLat;
extern double goalLon;
extern double homeLat;
extern double homeLon;
extern double routeDistanceMeters;
extern double remainingDistanceMeters;
extern int activeLeg;
extern double routeProgress;

// ─────────────────────────────────────────────────────────────────────────────
// Âncora de station-keeping (IDLE_HOLDING_POSITION)
// ─────────────────────────────────────────────────────────────────────────────
extern double holdLat;
extern double holdLon;
extern bool holdAnchored;
