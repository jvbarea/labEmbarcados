#pragma once
/*
 * AccessStateMachine.h
 * Máquina de estados que decide, a partir do peso filtrado e estável,
 * se o acesso à ração deve ser liberado, e comanda a abertura/fechamento
 * da porta em conjunto com o DoorActuator.
 *
 * Atende RF02, RF03, RF04, RNF02, RNF03, RNF04.
 *
 * Esta classe não lê o HX711 nem aciona o PWM diretamente: ela recebe o
 * WeightSensor e o DoorActuator já inicializados, o que permite testar a
 * lógica isoladamente (RNF07).
 */
#include <Arduino.h>
#include "WeightSensor.h"
#include "DoorActuator.h"
#include "Storage.h"
#include "EventLog.h"

enum class AccessState : uint8_t {
  IDLE,             // plataforma vazia, porta fechada
  STABILIZING,      // peso detectado, aguardando estabilização
  EVALUATING,       // peso estável, decidindo liberar ou bloquear
  ACCESS_OPEN,      // dentro da janela: porta aberta, animal se alimentando
  ACCESS_BLOCKED,   // fora da janela: porta permanece fechada, aguardando desocupação
  VACATE_CONFIRM,   // plataforma parece vazia, confirmando antes de fechar
  SAFE_FAULT,       // RNF03: falha detectada, mantém posição atual e não decide
};

class AccessStateMachine {
 public:
  void begin(WeightSensor *sensor, DoorActuator *door, EventLog *log, const AppConfig &cfg);

  // Deve ser chamada a cada iteração do loop(), após WeightSensor::update()
  // e antes/depois de DoorActuator::update() (ordem não é crítica).
  void update();

  // Permite ao HttpApi/BLE atualizar a janela de peso e os temporizadores
  // em tempo de execução (RF05).
  void applyConfig(const AppConfig &cfg);

  // RF06: abertura/fechamento manual via aplicativo. Sai do modo automático
  // pelo tempo da operação manual; volta ao ciclo automático assim que a
  // plataforma for esvaziada e confirmada.
  void manualOpen();
  void manualClose();

  AccessState state() const { return state_; }
  bool manualOverride() const { return manualOverride_; }

 private:
  WeightSensor *sensor_ = nullptr;
  DoorActuator *door_   = nullptr;
  EventLog     *log_    = nullptr;

  AppConfig cfg_;

  AccessState state_ = AccessState::IDLE;
  unsigned long stateEnteredMs_ = 0;
  bool manualOverride_ = false;
  bool manualCloseRequested_ = false; // fechar assim que a plataforma for confirmadamente esvaziada

  bool platformOccupied() const;
  bool platformEmpty() const;
  bool weightWithinWindow(float w) const;

  void enterState(AccessState newState);
};
