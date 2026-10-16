#pragma once
/*
 * EventLog.h
 * Log circular de eventos no formato do protocolo Feed Cat v2
 * (plano/01-app.md secao 7).
 *
 * Diferencas em relacao a versao anterior deste arquivo:
 *  - "seq" e um contador que NUNCA reinicia e fica salvo em NVS. E por ele que
 *    o app sincroniza o historico ("sync since").
 *  - "ts" e epoch em segundos. A ESP32 nao tem RTC: a base vem do campo "now"
 *    da mensagem "hello" do app. Antes do primeiro hello, ts = 0.
 *  - uma "visita" e gerada AO FINAL (quando a balanca volta a zero), com a
 *    duracao. Nao no instante da decisao.
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
  uint32_t seq;
  uint32_t ts;       // epoch s; 0 enquanto o app nao mandou "hello"
  EvKind   kind;
  uint8_t  cat;      // 0 = desconhecido
  int32_t  wG;
  uint32_t durS;
  bool     opened;   // resultado da porta na visita
  EvWhy    why;
  EvErr    err;
};

class EventLog {
 public:
  // Carrega o proximo "seq" da NVS. Chamar uma vez no boot.
  void begin();

  uint32_t addVisit(uint8_t cat, int32_t wG, uint32_t durS, bool opened, EvWhy why);
  uint32_t addRefill(uint32_t durS);
  uint32_t addFault(EvErr err);
  uint32_t addBoot();

  // Base de tempo vinda do "hello". now = epoch em segundos no instante da chamada.
  void setEpochBase(uint32_t now);
  uint32_t nowEpoch() const;
  bool hasClock() const { return epochBase_ != 0; }

  uint32_t lastSeq() const { return nextSeq_ - 1; }

  // Copia ate maxItems eventos com seq > since, do mais antigo para o mais
  // recente. Devolve quantos foram copiados.
  size_t since(uint32_t sinceSeq, FeederEvent *out, size_t maxItems) const;

  const FeederEvent *last() const;
  size_t count() const { return count_; }

 private:
  FeederEvent buffer_[EVENT_LOG_CAPACITY];
  size_t count_ = 0;
  size_t head_  = 0;

  uint32_t nextSeq_ = 1;
  uint32_t epochBase_ = 0;   // epoch correspondente a millis() == 0

  FeederEvent &slot();
  uint32_t commit();
  void persistSeq();
};
