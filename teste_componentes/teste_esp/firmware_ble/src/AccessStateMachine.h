#pragma once
/*
 * AccessStateMachine.h
 * Decide, a partir do peso, se libera o acesso a racao.
 *
 * Nao toca em hardware: recebe IWeightSource e DoorActuator por ponteiro, o
 * que permite rodar a mesma logica com a balanca real ou com o mock.
 *
 * Conceito de VISITA (protocolo v2, plano/01-app.md secao 7): comeca quando o
 * peso passa de PRESENCE_THRESHOLD_G e termina quando volta a zero. O evento
 * so e gravado NO FINAL, com a duracao e o resultado da porta.
 *
 * Correcoes em relacao a teste_esp/files (ver REVISAO.md):
 *  - usa isHealthy() e nao "tem amostra agora" para decidir falha (item 1);
 *  - manualClose respeita a janela de confirmacao (item 6);
 *  - reage a porta em FAULT (item 8).
 */
#include <Arduino.h>
#include "IWeightSource.h"
#include "DoorActuator.h"
#include "Storage.h"
#include "EventLog.h"

enum class AccessState : uint8_t {
  IDLE,
  STABILIZING,
  EVALUATING,
  ACCESS_OPEN,
  ACCESS_BLOCKED,
  VACATE_CONFIRM,
  SAFE_FAULT,
};

const char *accessStateName(AccessState s);

class AccessStateMachine {
 public:
  void begin(IWeightSource *sensor, DoorActuator *door, EventLog *log, const AppConfig &cfg);
  void update();

  void applyConfig(const AppConfig &cfg);
  const AppConfig &config() const { return cfg_; }

  void manualOpen();                      // reabastecer
  void manualClose();
  void startRefill(uint32_t timeoutS);    // reabastecer com fechamento automatico
  void reset();

  AccessState state() const { return state_; }
  bool manualOverride() const { return manualOverride_; }

  // Gato reconhecido pelo peso atual: 0 quando nao bate com nenhum cadastro.
  uint8_t currentCat() const;

 private:
  IWeightSource *sensor_ = nullptr;
  DoorActuator  *door_   = nullptr;
  EventLog      *log_    = nullptr;

  AppConfig cfg_;

  AccessState state_ = AccessState::IDLE;
  unsigned long stateEnteredMs_ = 0;

  bool manualOverride_ = false;
  bool manualCloseRequested_ = false;
  unsigned long manualCloseSinceMs_ = 0;

  // Reabastecer
  unsigned long refillStartMs_ = 0;
  uint32_t refillTimeoutS_ = 0;
  bool refillActive_ = false;

  // Visita em andamento
  bool visitActive_ = false;
  unsigned long visitStartMs_ = 0;
  int32_t visitPeakG_ = 0;
  uint8_t visitCat_ = 0;
  bool visitOpened_ = false;
  EvWhy visitWhy_ = EvWhy::UNKNOWN;

  bool doorFaultReported_ = false;
  bool sensorFaultReported_ = false;

  bool platformOccupied() const;
  bool platformEmpty() const;
  bool weightAllowed(float w, uint8_t &catOut, EvWhy &whyOut) const;

  void enterState(AccessState newState);
  void beginVisit();
  void endVisit();
};
