#pragma once
/*
 * AppProtocol.h
 * Traduz o protocolo Feed Cat v2 (plano/01-app.md secao 7) para os modulos do
 * firmware, e vice-versa. E a unica parte que conhece os nomes das mensagens.
 *
 * Escopo: Etapa 2 do plano ("firmware minimo de teste... o peso aparece ao
 * vivo no app e o ack volta para cada comando").
 *
 *   implementado de verdade : hello, cfg, cfgGet/cfgDump, cmd (close, refill,
 *                             tare, calibrate), st, ev, sync/syncEnd
 *   aceita, guarda e confirma: cats, mode, profile
 *   ainda nao               : grant, revoke -> ack com err "not_supported"
 *
 * Extensoes fora do v2, para bancada sem celula de carga (o app nunca manda):
 *   {"t":"cmd","id":N,"c":"mock","wG":4200}    define o peso simulado
 *   {"t":"cmd","id":N,"c":"sensor","ok":false} simula sensor mudo
 * Elas usam a chave "c"; o v2 usa "op". Por isso convivem sem ambiguidade.
 */
#include <Arduino.h>
#include <ArduinoJson.h>
#include "BleControl.h"
#include "IWeightSource.h"
#include "MockWeightSource.h"
#include "Hx711WeightSource.h"
#include "DoorActuator.h"
#include "AccessStateMachine.h"
#include "EventLog.h"
#include "Storage.h"

class AppProtocol {
 public:
  /*
   * scale: a fonte de peso em uso, qualquer que seja.
   * mock / hx: so um dos dois e diferente de nullptr, conforme USE_HX711.
   *            Servem para os comandos que SO fazem sentido numa delas -
   *            injetar peso simulado, ou tarar e calibrar de verdade.
   */
  void begin(BleControl *ble, IWeightSource *scale, MockWeightSource *mock,
             Hx711WeightSource *hx, DoorActuator *door,
             AccessStateMachine *fsm, EventLog *log, Storage *storage);

  void handleJson(const char *raw);

  // Chamar no loop(): manda o status periodico e empurra eventos novos.
  void tick();

  String statusJson() const;
  void sendStatus();

 private:
  BleControl        *ble_ = nullptr;
  IWeightSource     *scale_ = nullptr;
  MockWeightSource  *mock_ = nullptr;   // nullptr quando a balanca e real
  Hx711WeightSource *hx_ = nullptr;     // nullptr quando a balanca e simulada
  DoorActuator      *door_ = nullptr;
  AccessStateMachine *fsm_ = nullptr;
  EventLog          *log_ = nullptr;
  Storage           *storage_ = nullptr;

  uint32_t lastPushedSeq_ = 0;
  unsigned long lastStatusMs_ = 0;

  void sendAck(int id, bool ok, const char *err = nullptr);
  void sendCfgDump();
  void sendEvent(const FeederEvent &e);
  void pushNewEvents();

  void persisteCalibracao();

  void handleCmd(int id, JsonDocument &doc);
  void handleCfg(int id, JsonDocument &doc);
  void handleCats(int id, JsonDocument &doc);
  void handleSync(int id, JsonDocument &doc);
};
