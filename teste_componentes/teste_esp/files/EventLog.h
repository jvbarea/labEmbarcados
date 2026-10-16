#pragma once
/*
 * EventLog.h
 * Log circular em RAM dos eventos do sistema (RF11).
 * Cada evento guarda tipo, peso medido (g) e o instante (millis() desde o boot).
 * Exposto ao aplicativo via HttpApi (GET /api/events).
 * Observação: por ora o "horário" é relativo ao boot (millis). Se o projeto
 * adotar sincronização de hora real (NTP) futuramente, basta trocar o campo
 * timestampMs por um epoch absoluto sem alterar a interface deste módulo.
 */
#include <Arduino.h>
#include "config.h"

enum class EventType : uint8_t {
  ACCESS_GRANTED = 0,   // peso dentro da janela, porta liberada
  ACCESS_BLOCKED = 1,   // peso fora da janela, porta mantida fechada
  DOOR_OPENED_MANUAL = 2,
  DOOR_CLOSED_MANUAL = 3,
  CALIBRATION_DONE = 4,
  FAULT_SAFE_STATE = 5, // RNF03: falha detectada, porta mantida na posição atual
};

struct FeederEvent {
  EventType type;
  float weightG;
  unsigned long timestampMs;
};

class EventLog {
 public:
  void add(EventType type, float weightG);

  // Serializa os últimos eventos (mais recente por último) em JSON.
  String toJson(size_t maxItems = EVENT_LOG_CAPACITY) const;

  void clear();

 private:
  FeederEvent buffer_[EVENT_LOG_CAPACITY];
  size_t count_ = 0;
  size_t head_  = 0; // próxima posição de escrita
};
