#pragma once
/*
 * DoorActuator.h
 * Porta acionada por servo POSICIONAL de 180 graus (MG946R/MG995/MG996R).
 *
 * As posicoes sao em GRAUS: o mecanismo usa 0 (fechada) e 90 (aberta). A
 * conversao para microssegundos e linear entre dois pontos MEDIDOS no servo
 * real: o pulso em que o eixo esta fisicamente em 0 graus (us0) e em 90 (us90).
 * Ver setServoCalibration() e os comandos set0 / set90 do menu de bancada.
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

class DoorActuator {
 public:
  // us0 / us90: pulsos em que o eixo esta fisicamente em 0 e 90 graus.
  void begin(int closedDeg, int openDeg, int us0, int us90);
  void update();  // nao bloqueante

  void requestOpen();
  void requestClose();

  // Rearma depois de uma falha: refaz o percurso ate fechado.
  // O original nao tinha como sair de FAULT (REVISAO.md, item 4).
  void reset();

  // --- calibracao de bancada ---
  void enterCalibration();
  void exitCalibration();          // volta para a posicao de fechada
  // So tem efeito em CALIBRATING. Devolve true quando a saida do repouso na
  // posicao fechada ganhou partida suave (ver DOOR_KICK_DEG em config.h).
  bool jogToDeg(int deg);
  void jogToUs(int us);            // idem; limitado a SERVO_US_MIN..SERVO_US_MAX
  void nudge(int deltaUs);         // anda a partir do ALVO atual, para os toques se acumularem
  bool isCalibrating() const { return state_ == DoorState::CALIBRATING; }

  // Em calibracao, true quando o eixo ja chegou ao pulso pedido. Gravar set0 ou
  // set90 no meio da rampa guardaria um pulso em que o eixo ainda nao esta.
  bool atTarget() const { return !kicking_ && currentUs_ == targetUs_; }
  int  targetPulseUs() const { return targetUs_; }

  void setPositions(int closedDeg, int openDeg);
  int closedDeg() const { return closedDeg_; }
  int openDeg()   const { return openDeg_; }

  // Calibracao do servo. Recusa (devolve false) pulsos fora da faixa absoluta ou
  // a menos de SERVO_CAL_MIN_SPAN_US um do outro, e mantem a calibracao anterior.
  // Nao move o eixo: so muda como os proximos graus viram pulso.
  bool setServoCalibration(int us0, int us90);
  int  servoUs0()  const { return us0_; }
  int  servoUs90() const { return us90_; }

  // Conversao grau <-> pulso pela calibracao em vigor.
  int degToUs(int deg) const;
  int usToDeg(int us) const;

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
  int currentDeg()     const { return usToDeg(currentUs_); }

 private:
  // PWM direto em vez de Servo: a Servo corta o pulso em 2500 us. Ver config.h.
  ESP32PWM pwm_;
  DoorState state_ = DoorState::CLOSED;

  int closedDeg_ = DEFAULT_DOOR_CLOSED_DEG;
  int openDeg_   = DEFAULT_DOOR_OPEN_DEG;

  int us0_  = DEFAULT_SERVO_US_AT_0DEG;
  int us90_ = DEFAULT_SERVO_US_AT_90DEG;

  int currentUs_ = 0;
  int targetUs_  = 0;

  unsigned long moveStartMs_ = 0;
  unsigned long lastStepMs_  = 0;
  unsigned long settleStartMs_ = 0;
  bool settling_ = false;

  // Partida suave: enquanto kicking_, o alvo e o fim do primeiro trecho e o
  // destino verdadeiro espera em finalTargetUs_.
  bool kicking_ = false;
  int  finalTargetUs_ = 0;
  unsigned long kickHoldStartMs_ = 0;

  bool limitOpenActive() const;
  bool limitClosedActive() const;

  void startMovement(DoorState movingState, int targetUs);
  void writeUs(int us);  // escreve o pulso, ja limitado a SERVO_US_MIN..SERVO_US_MAX
  void energize();
  void deenergize();
  void finishMovement(DoorState finalState);
  bool stepTowardTarget();  // true quando chegou no alvo

  bool planejaPartida(int alvoUs);  // true se dividiu o movimento em partida + resto
  bool andaPartida();               // true enquanto a partida ocupa este ciclo
};
