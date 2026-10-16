#pragma once
/*
 * DoorActuator.h
 * Porta acionada por servo POSICIONAL de 180 graus (MG946R/MG995/MG996R).
 *
 * As posicoes sao em GRAUS: o mecanismo usa 0 (fechada) e 90 (aberta). A
 * conversao para microssegundos usa SERVO_US_AT_0DEG / SERVO_US_AT_180DEG.
 *
 * Mudanca de fundo em relacao a teste_esp/files: o original tratava o servo
 * como de rotacao continua, girando ate a chave de fim de curso acionar. Com
 * servo posicional o pulso E a posicao, entao "abrir" e escrever o angulo de
 * aberto e esperar o curso terminar. As chaves passam a ser opcionais
 * (USE_LIMIT_SWITCHES), servindo de confirmacao e nao de sensor primario.
 *
 * Tambem: nenhum delay(). O original bloqueava 50 ms dentro de update().
 */
#include <Arduino.h>
#include <ESP32Servo.h>
#include "config.h"

enum class DoorState : uint8_t {
  CLOSED,
  OPENING,
  OPEN,
  CLOSING,
  FAULT,
  CALIBRATING,  // bancada: servo energizado e livre para posicionar
};

const char *doorStateName(DoorState s);

// Conversao grau <-> microssegundo, usando os extremos definidos em config.h.
int doorDegToUs(int deg);
int doorUsToDeg(int us);

class DoorActuator {
 public:
  void begin(int closedDeg, int openDeg);
  void update();  // nao bloqueante

  void requestOpen();
  void requestClose();

  // Rearma depois de uma falha: refaz o percurso ate fechado.
  // O original nao tinha como sair de FAULT (REVISAO.md, item 4).
  void reset();

  // --- calibracao de bancada ---
  void enterCalibration();
  void exitCalibration();          // volta para a posicao de fechada
  void jogToDeg(int deg);          // so tem efeito em CALIBRATING
  bool isCalibrating() const { return state_ == DoorState::CALIBRATING; }

  void setPositions(int closedDeg, int openDeg);
  int closedDeg() const { return closedDeg_; }
  int openDeg()   const { return openDeg_; }

  DoorState state() const { return state_; }
  bool isOpen()   const { return state_ == DoorState::OPEN; }
  bool isClosed() const { return state_ == DoorState::CLOSED; }
  bool isMoving() const { return state_ == DoorState::OPENING || state_ == DoorState::CLOSING; }
  bool isFault()  const { return state_ == DoorState::FAULT; }

  // Fins de curso como o protocolo v2 espera. Sem as chaves montadas
  // (USE_LIMIT_SWITCHES 0) sao deduzidos do estado da porta.
  bool fcClosed() const;
  bool fcOpen() const;

  int currentPulseUs() const { return currentUs_; }
  int currentDeg()     const { return doorUsToDeg(currentUs_); }

 private:
  Servo servo_;
  DoorState state_ = DoorState::CLOSED;

  int closedDeg_ = DEFAULT_DOOR_CLOSED_DEG;
  int openDeg_   = DEFAULT_DOOR_OPEN_DEG;

  int currentUs_ = 0;
  int targetUs_  = 0;

  unsigned long moveStartMs_ = 0;
  unsigned long lastStepMs_  = 0;
  unsigned long settleStartMs_ = 0;
  bool settling_ = false;

  bool limitOpenActive() const;
  bool limitClosedActive() const;

  void startMovement(DoorState movingState, int targetUs);
  void energize();
  void deenergize();
  void finishMovement(DoorState finalState);
  bool stepTowardTarget();  // true quando chegou no alvo
};
