#pragma once

#include <Arduino.h>

bool initHardwareSensors();
void initMotors();
bool initCompass();
void stopMotors();
void updateMotorOutputs();
void readUltrasonic();
bool readCompass();
// Rotina de calibração hard-iron: gire o USV 1-2 voltas completas em ~25s. Mede
// o offset de cada eixo e persiste no LittleFS. Bloqueante — comando manual.
bool calibrateCompass();
// true se há calibração de bússola persistida (carregada no boot). Usado para
// decidir se o boot deve rodar a calibração automaticamente (só se faltar).
bool isCompassCalibrated();
// Verificação ("self-test") dos sensores: confirma que bússola, ultrassônicos de
// proa e GPS respondem com leituras plausíveis. NÃO calibra e NÃO move o barco.
// Retorna true se TODOS os sensores essenciais passarem. Loga um relatório.
bool runSensorSelfTest();
// Rotina completa de boot/botão: self-test + (opcional) calibração da bússola.
// forceCalibration=true sempre calibra; false calibra só se não houver calibração
// salva. Reaproveita runSensorSelfTest() e calibrateCompass().
void runCalibrationAndCheck(bool forceCalibration);
void readGPS();
void updateSensorValues();

// Imprime um bloco de status estruturado e legível (sensores, GPS, sonar, link
// RPi, rumo). Chamado periodicamente pelo loop principal. Reaproveita os dados
// já lidos — não faz novas leituras de hardware.
void printStatusBlock();
