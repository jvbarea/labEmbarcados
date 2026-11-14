#pragma once
/*
 * EventLog.h
 * Historico de eventos no formato do protocolo Feed Cat v2
 * (plano/01-app.md secao 7), guardado na flash.
 *
 *  - "seq" e um contador que NUNCA reinicia e fica salvo em NVS. E por ele que
 *    o app sincroniza o historico ("sync since").
 *  - os eventos ficam num anel de EVENT_LOG_CAPACITY posicoes num arquivo do
 *    LittleFS (docs/08 secao 4.2). O evento de seq s mora na posicao s % N, entao
 *    gravar um evento e escrever 32 bytes num lugar fixo, sem reescrever o resto.
 *    Uma copia do anel fica na RAM para o sync nao ler a flash.
 *  - "ts" e epoch em segundos. A ESP32 nao tem RTC: a base vem do campo "now"
 *    do "hello" do app. Eventos que nascem antes do primeiro hello deste boot
 *    ficam com ts = 0 e sao corrigidos quando o hello chega, pelo tempo desde o
 *    boot. Os de um boot que nunca viu hello continuam com 0.
 *  - uma "visita" e gerada AO FINAL (quando a balanca esvazia), com a duracao.
 */
#include <Arduino.h>
#include "config.h"

enum class EvKind : uint8_t { VISIT, REFILL, FAULT, BOOT };
enum class EvWhy  : uint8_t { NONE, RULE, GRANT, UNKNOWN, MANUAL, LEARN };
enum class EvErr  : uint8_t { NONE, DOOR_TIMEOUT, SENSOR };

const char *evKindName(EvKind k);
const char *evWhyName(EvWhy w);
const char *evErrName(EvErr e);

struct FeederEvent {
  uint32_t seq;       // 0 = posicao vazia
  uint32_t ts;        // epoch s; 0 = sem hora
  EvKind   kind;
  uint8_t  cat;       // 0 = desconhecido
  uint8_t  rsv0[2];   // campos explicitos no lugar do padding: o registro vai
  int32_t  wG;        // byte a byte para a flash e entra no checksum
  uint32_t durS;
  bool     opened;    // resultado da porta na visita
  EvWhy    why;
  EvErr    err;
  uint8_t  rsv1;
};
static_assert(sizeof(FeederEvent) == 24, "FeederEvent mudou de tamanho: troque EV_MAGIC");

class EventLog {
 public:
  // Monta o LittleFS, carrega o anel e o proximo seq. Chamar uma vez no boot.
  void begin();

  uint32_t addVisit(uint8_t cat, int32_t wG, uint32_t durS, bool opened, EvWhy why);
  uint32_t addRefill(uint32_t durS);
  uint32_t addFault(EvErr err);
  uint32_t addBoot();

  // Base de tempo vinda do "hello" (now = epoch em segundos). Tambem corrige o ts
  // dos eventos deste boot que nasceram sem hora.
  void setEpochBase(uint32_t now);
  uint32_t nowEpoch() const;
  bool hasClock() const { return epochBase_ != 0; }

  uint32_t lastSeq() const { return nextSeq_ - 1; }

  // Primeiro evento guardado com seq > after. false quando nao ha mais.
  bool nextAfter(uint32_t after, FeederEvent &out) const;

  // Os ultimos ate maxItems eventos, do mais antigo para o mais recente.
  size_t latest(FeederEvent *out, size_t maxItems) const;
  size_t count() const;

  // Comando de bancada "evclear": esquece os eventos e MANTEM o seq, para o app
  // nao confundir eventos novos com os que ja tem.
  void clear();

  // Comando de bancada "evtrim": apaga so os eventos com seq > after. O teste de
  // bancada (tools/v2_handshake.py) usa para nao deixar as visitas de teste no
  // historico que o app vai sincronizar. O seq tambem continua.
  size_t dropAfter(uint32_t after);

  bool persistent() const { return fsOk_; }

 private:
  FeederEvent ring_[EVENT_LOG_CAPACITY];
  uint32_t bornUptimeS_[EVENT_LOG_CAPACITY];  // so vale para seq >= firstSeqThisBoot_

  uint32_t nextSeq_ = 1;
  uint32_t firstSeqThisBoot_ = 1;
  uint32_t epochBase_ = 0;  // epoch no instante em que o uptime era 0
  bool fsOk_ = false;

  uint32_t add(FeederEvent e);
  void persistSeq();
  void writeSlot(size_t slot);
  bool loadFile(uint32_t &maxSeq);
  bool createFile();
};
