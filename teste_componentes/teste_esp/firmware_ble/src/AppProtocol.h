#pragma once
/*
 * AppProtocol.h
 * Traduz o protocolo Feed Cat v2 (plano/01-app.md secao 7) para os modulos do
 * firmware, e vice-versa. E a unica parte que conhece os nomes das mensagens.
 *
 * Todas as mensagens do v2 estao implementadas: hello, profile, cfgGet/cfgDump,
 * cats, grant, revoke, cmd (refill, close, tare, calibrate), cfg, mode, sync; e
 * do lado da ESP st, ack, ev e syncEnd. O comportamento segue o simulador do app
 * (app/src/device/MockLink.ts), que serve de especificacao.
 *
 * Detalhes que o app nao ve mas que importam:
 *  - cats, cfg, profile e mode com o MESMO conteudo que ja esta gravado sao
 *    confirmados sem escrever na flash e sem mudar o cfgV (docs/08 secao 4.4:
 *    o app reenvia tudo a cada conexao).
 *  - o sync manda TODOS os eventos pendentes, um por vez, sem travar o loop.
 *  - o status sai a cada STATUS_PERIOD_MS e na hora em que porta, gato, modo,
 *    liberacao ou configuracao mudam.
 *  - eventos so sao empurrados ao vivo depois do primeiro hello do boot, quando
 *    ja tem hora certa.
 *
 * Extensoes fora do v2, para bancada (o app nunca manda). Usam a chave "c"; o
 * v2 usa "op", entao convivem sem ambiguidade:
 *   {"t":"cmd","id":N,"c":"mock","wG":4200}    define o peso simulado
 *   {"t":"cmd","id":N,"c":"sensor","ok":false} simula sensor mudo
 *   {"t":"cmd","id":N,"c":"status"}            pede o status agora
 *   {"t":"cmd","id":N,"c":"reset"}             rearma a maquina de estados
 *   {"t":"cmd","id":N,"c":"evclear"}           apaga o historico (mantem o seq)
 *   {"t":"cmd","id":N,"c":"evtrim","after":S}  apaga so os eventos com seq > S
 *   {"t":"cfg",...,"closedDeg":0,"openDeg":90}  posicoes da porta, em graus
 *   {"t":"cfg",...,"minG":3000,"maxG":8000}     janela de bancada (sem gatos)
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

  void handleMessage(const char *raw, size_t len);

  // Chamar no loop(): status, eventos novos e o sync em andamento.
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

  String profile_;  // copia do que esta na NVS, para nao ler a flash a cada cfgDump

  uint32_t lastPushedSeq_ = 0;
  unsigned long lastStatusMs_ = 0;
  String lastStatusKey_;

  struct SyncJob {
    bool     active = false;
    int      id = 0;
    uint32_t cursor = 0;  // ultimo seq ja mandado
  } sync_;

  void sendDoc(JsonDocument &doc, bool logSerial);
  void sendAck(int id, bool ok, const char *err = nullptr);
  void sendCfgDump();
  void sendEvent(const FeederEvent &e);
  void pushNewEvents();
  void pumpSync();
  String statusKey() const;

  // Grava e aplica so se algo mudou. true se gravou.
  bool saveIfChanged(AppConfig &cfg);
  void persisteCalibracao();

  void handleCmd(int id, JsonDocument &doc);
  void handleCfg(int id, JsonDocument &doc);
  void handleCats(int id, JsonDocument &doc);
  void handleMode(int id, JsonDocument &doc);
  void handleProfile(int id, JsonDocument &doc);
  void handleGrant(int id, JsonDocument &doc);
  void handleSync(int id, JsonDocument &doc);
};
