#include "modules/state/state.h"

// Identificação
const String droneId = DRONE_ID;
const char* telemetryBufferPath = TELEMETRY_BUFFER_PATH;
const char* pathBufferPath = PATH_BUFFER_PATH;

// Estado de navegação
NavState currentState = IDLE_HOLDING_POSITION;
// Posição corrente. NÃO há coordenada fabricada de boot: começa em 0/0 e só é
// válida depois do 1º fix real (hasEverHadFix). Quem consome (telemetria, HUD,
// navegação) deve checar hasGpsFix/hasEverHadFix antes de usar — nunca publicar
// 0/0 nem um ponto fictício como se fosse posição real.
double currentLat = 0.0;
double currentLon = 0.0;
double currentHeading = 0.0;
int batteryMv = 8000;
int obsDist = 200;
int obsDistL = 200;
int obsDistR = 200;
int thrustL = 0;
int thrustR = 0;
String activeMissionId = "";
String lastCommandId = "";

// Sensores
bool hasGpsFix = false;
bool hasEverHadFix = false;   // true após o 1º fix válido; distingue "nunca tive posição" de "tive e perdi"
double gpsLat = 0.0;
double gpsLon = 0.0;
double gpsCourse = 0.0;
bool compassReady = false;

// Saúde/diagnóstico dos sensores
unsigned long gpsBytesWindow = 0;
unsigned long gpsNmeaWindow = 0;
bool compassLastReadOk = false;
bool ultrasonicHealthy = false;
bool ultrasonicLHealthy = false;
bool ultrasonicRHealthy = false;

// Missão e rota. SEM ponto fabricado: só são válidos quando há target ativo
// (hasActiveTarget), definido por um set_destination real vindo do RPi.
double goalLat = 0.0;
double goalLon = 0.0;
bool hasActiveTarget = false;   // nenhum target até um set_destination real
double homeLat = 0.0;
double homeLon = 0.0;
double routeDistanceMeters = 0.0;
double remainingDistanceMeters = 0.0;
int activeLeg = 0;
double routeProgress = 0.0;

// Âncora de station-keeping (IDLE_HOLDING_POSITION)
double holdLat = 0.0;
double holdLon = 0.0;
bool holdAnchored = false;
