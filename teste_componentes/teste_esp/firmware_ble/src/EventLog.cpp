#include "EventLog.h"
#include <Preferences.h>

const char *evKindName(EvKind k) {
  switch (k) {
    case EvKind::VISIT:  return "visit";
    case EvKind::REFILL: return "refill";
    case EvKind::FAULT:  return "fault";
    case EvKind::BOOT:   return "boot";
    default:             return "boot";
  }
}

const char *evWhyName(EvWhy w) {
  switch (w) {
    case EvWhy::RULE:    return "rule";
    case EvWhy::GRANT:   return "grant";
    case EvWhy::UNKNOWN: return "unknown";
    case EvWhy::MANUAL:  return "manual";
    case EvWhy::LEARN:   return "learn";
    default:             return nullptr;
  }
}

const char *evErrName(EvErr e) {
  switch (e) {
    case EvErr::DOOR_TIMEOUT: return "door_timeout";
    case EvErr::SENSOR:       return "sensor_fault";
    default:                  return nullptr;
  }
}

void EventLog::begin() {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE_APP, /*readOnly=*/true)) {
    nextSeq_ = prefs.getUInt("seq", 1);
    prefs.end();
  }
  if (nextSeq_ == 0) nextSeq_ = 1;
}

void EventLog::persistSeq() {
  // Grava so o contador, nao os eventos. Sao poucas dezenas de escritas por dia,
  // bem abaixo do desgaste que preocupa em docs/08-persistencia-dados.md.
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE_APP, /*readOnly=*/false)) return;
  prefs.putUInt("seq", nextSeq_);
  prefs.end();
}

void EventLog::setEpochBase(uint32_t now) {
  if (now == 0) return;
  epochBase_ = now - (millis() / 1000);
}

uint32_t EventLog::nowEpoch() const {
  if (epochBase_ == 0) return 0;
  return epochBase_ + (millis() / 1000);
}

FeederEvent &EventLog::slot() {
  FeederEvent &e = buffer_[head_];
  e = FeederEvent{};
  e.seq = nextSeq_;
  e.ts  = nowEpoch();
  return e;
}

uint32_t EventLog::commit() {
  uint32_t seq = nextSeq_;
  head_ = (head_ + 1) % EVENT_LOG_CAPACITY;
  if (count_ < EVENT_LOG_CAPACITY) count_++;
  nextSeq_++;
  persistSeq();
  return seq;
}

uint32_t EventLog::addVisit(uint8_t cat, int32_t wG, uint32_t durS, bool opened, EvWhy why) {
  FeederEvent &e = slot();
  e.kind = EvKind::VISIT;
  e.cat = cat;
  e.wG = wG;
  e.durS = durS;
  e.opened = opened;
  e.why = why;
  return commit();
}

uint32_t EventLog::addRefill(uint32_t durS) {
  FeederEvent &e = slot();
  e.kind = EvKind::REFILL;
  e.durS = durS;
  return commit();
}

uint32_t EventLog::addFault(EvErr err) {
  FeederEvent &e = slot();
  e.kind = EvKind::FAULT;
  e.err = err;
  return commit();
}

uint32_t EventLog::addBoot() {
  FeederEvent &e = slot();
  e.kind = EvKind::BOOT;
  return commit();
}

size_t EventLog::since(uint32_t sinceSeq, FeederEvent *out, size_t maxItems) const {
  size_t n = 0;
  size_t start = (head_ + EVENT_LOG_CAPACITY - count_) % EVENT_LOG_CAPACITY;
  for (size_t i = 0; i < count_ && n < maxItems; i++) {
    const FeederEvent &e = buffer_[(start + i) % EVENT_LOG_CAPACITY];
    if (e.seq > sinceSeq) out[n++] = e;
  }
  return n;
}

const FeederEvent *EventLog::last() const {
  if (count_ == 0) return nullptr;
  return &buffer_[(head_ + EVENT_LOG_CAPACITY - 1) % EVENT_LOG_CAPACITY];
}
