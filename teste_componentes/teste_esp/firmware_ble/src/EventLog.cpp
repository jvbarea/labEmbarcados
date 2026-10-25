#include "EventLog.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_timer.h>
#include <algorithm>

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

// ------------------------------------------------------- formato no arquivo

namespace {

// "FCE1" em little-endian. Mudou o FeederEvent? Troque o numero: os registros
// antigos passam a ser ignorados em vez de lidos com os campos trocados.
const uint32_t EV_MAGIC = 0x31454346;

struct EventRecord {
  uint32_t    magic;
  FeederEvent ev;
  uint32_t    check;  // FNV-1a dos bytes de ev: pega registro meio gravado
};
static_assert(sizeof(EventRecord) == 32, "EventRecord deve ter 32 bytes");

uint32_t fnv1a(const uint8_t *p, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

// Segundos desde o boot. millis() volta a zero a cada 49,7 dias e o comedouro
// pode ficar ligado mais que isso; o esp_timer tem 64 bits.
uint32_t uptimeS() {
  return (uint32_t)(esp_timer_get_time() / 1000000LL);
}

}  // namespace

// ------------------------------------------------------------------- boot

void EventLog::begin() {
  memset(ring_, 0, sizeof(ring_));
  memset(bornUptimeS_, 0, sizeof(bornUptimeS_));

  uint32_t nvsNext = 1;
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE_APP, /*readOnly=*/true)) {
    nvsNext = prefs.getUInt("seq", 1);
    prefs.end();
  }
  if (nvsNext == 0) nvsNext = 1;

  // formatOnFail: na primeira vez a particao vem vazia (ou com SPIFFS) e o
  // LittleFS so monta depois de formatar. Leva menos de 1 s.
  fsOk_ = LittleFS.begin(/*formatOnFail=*/true);
  uint32_t maxSeq = 0;
  if (fsOk_) {
    if (!loadFile(maxSeq)) {
      memset(ring_, 0, sizeof(ring_));
      maxSeq = 0;
      fsOk_ = createFile();
    }
  }
  if (!fsOk_) {
    Serial.println("[log] LittleFS indisponivel - o historico fica so na RAM ate o proximo boot");
  }

  // O seq da NVS manda; o do arquivo cobre o caso de a NVS ter ficado para tras.
  nextSeq_ = (maxSeq + 1 > nvsNext) ? maxSeq + 1 : nvsNext;
  firstSeqThisBoot_ = nextSeq_;
  Serial.printf("[log] %u evento(s) na flash, proximo seq = %lu\n", (unsigned)count(),
                (unsigned long)nextSeq_);
}

bool EventLog::loadFile(uint32_t &maxSeq) {
  File f = LittleFS.open(EVENT_LOG_FILE, "r");
  if (!f) return false;
  if (f.size() != EVENT_LOG_CAPACITY * sizeof(EventRecord)) {
    Serial.println("[log] arquivo de eventos com tamanho inesperado - recriando");
    f.close();
    return false;
  }

  EventRecord r;
  for (size_t slot = 0; slot < EVENT_LOG_CAPACITY; slot++) {
    if (f.read((uint8_t *)&r, sizeof(r)) != sizeof(r)) {
      f.close();
      return false;
    }
    if (r.magic != EV_MAGIC) continue;  // posicao vazia
    if (r.check != fnv1a((const uint8_t *)&r.ev, sizeof(r.ev))) continue;
    if (r.ev.seq == 0 || r.ev.seq % EVENT_LOG_CAPACITY != slot) continue;
    ring_[slot] = r.ev;
    if (r.ev.seq > maxSeq) maxSeq = r.ev.seq;
  }
  f.close();
  return true;
}

bool EventLog::createFile() {
  File f = LittleFS.open(EVENT_LOG_FILE, "w");
  if (!f) {
    Serial.println("[log] nao consegui criar o arquivo de eventos");
    return false;
  }
  uint8_t zeros[256];
  memset(zeros, 0, sizeof(zeros));
  size_t falta = EVENT_LOG_CAPACITY * sizeof(EventRecord);
  while (falta > 0) {
    const size_t n = falta < sizeof(zeros) ? falta : sizeof(zeros);
    if (f.write(zeros, n) != n) {
      f.close();
      return false;
    }
    falta -= n;
  }
  f.close();
  return true;
}

void EventLog::writeSlot(size_t slot) {
  if (!fsOk_) return;
  File f = LittleFS.open(EVENT_LOG_FILE, "r+");
  if (!f) {
    Serial.println("[log] falha ao abrir o arquivo de eventos para gravar");
    return;
  }
  EventRecord r;
  memset(&r, 0, sizeof(r));
  if (ring_[slot].seq != 0) {
    r.magic = EV_MAGIC;
    r.ev = ring_[slot];
    r.check = fnv1a((const uint8_t *)&r.ev, sizeof(r.ev));
  }
  if (!f.seek(slot * sizeof(EventRecord)) || f.write((const uint8_t *)&r, sizeof(r)) != sizeof(r)) {
    Serial.println("[log] falha ao gravar evento na flash");
  }
  f.close();
}

void EventLog::persistSeq() {
  // Uma escrita por evento: dezenas por dia, bem abaixo do desgaste que
  // preocupa em docs/08-persistencia-dados.md.
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE_APP, /*readOnly=*/false)) return;
  prefs.putUInt("seq", nextSeq_);
  prefs.end();
}

// ------------------------------------------------------------------ relogio

void EventLog::setEpochBase(uint32_t now) {
  const uint32_t up = uptimeS();
  if (now <= up) return;  // hora absurda; fica sem relogio
  epochBase_ = now - up;

  // Corrige quem nasceu sem hora neste boot (o "boot" sempre, e o que mais
  // tenha acontecido antes de o celular conectar).
  uint32_t de = firstSeqThisBoot_;
  if (nextSeq_ - de > EVENT_LOG_CAPACITY) de = nextSeq_ - EVENT_LOG_CAPACITY;
  unsigned corrigidos = 0;
  for (uint32_t s = de; s < nextSeq_; s++) {
    const size_t slot = s % EVENT_LOG_CAPACITY;
    FeederEvent &e = ring_[slot];
    if (e.seq == s && e.ts == 0) {
      e.ts = epochBase_ + bornUptimeS_[slot];
      writeSlot(slot);
      corrigidos++;
    }
  }
  if (corrigidos > 0) {
    Serial.printf("[log] hora aplicada a %u evento(s) que nasceram antes do hello\n", corrigidos);
  }
}

uint32_t EventLog::nowEpoch() const {
  if (epochBase_ == 0) return 0;
  return epochBase_ + uptimeS();
}

// ---------------------------------------------------------------- gravacao

uint32_t EventLog::add(FeederEvent e) {
  e.seq = nextSeq_;
  e.ts = nowEpoch();
  const size_t slot = e.seq % EVENT_LOG_CAPACITY;
  ring_[slot] = e;
  bornUptimeS_[slot] = uptimeS();
  nextSeq_++;
  persistSeq();
  writeSlot(slot);
  return e.seq;
}

uint32_t EventLog::addVisit(uint8_t cat, int32_t wG, uint32_t durS, bool opened, EvWhy why) {
  FeederEvent e{};
  e.kind = EvKind::VISIT;
  e.cat = cat;
  e.wG = wG;
  e.durS = durS;
  e.opened = opened;
  e.why = why;
  return add(e);
}

uint32_t EventLog::addRefill(uint32_t durS) {
  FeederEvent e{};
  e.kind = EvKind::REFILL;
  e.durS = durS;
  return add(e);
}

uint32_t EventLog::addFault(EvErr err) {
  FeederEvent e{};
  e.kind = EvKind::FAULT;
  e.err = err;
  return add(e);
}

uint32_t EventLog::addBoot() {
  FeederEvent e{};
  e.kind = EvKind::BOOT;
  return add(e);
}

void EventLog::clear() {
  memset(ring_, 0, sizeof(ring_));
  if (fsOk_) fsOk_ = createFile();
  Serial.printf("[log] historico apagado; o proximo evento continua do seq %lu\n",
                (unsigned long)nextSeq_);
}

// So os N ultimos seq podem estar no anel; o resto ja foi sobrescrito.
static uint32_t firstInWindow(uint32_t last) {
  return (last >= EVENT_LOG_CAPACITY) ? last - EVENT_LOG_CAPACITY + 1 : 1;
}

size_t EventLog::dropAfter(uint32_t after) {
  const uint32_t last = lastSeq();
  size_t n = 0;
  for (uint32_t s = firstInWindow(last); s <= last && last > 0; s++) {
    if (s <= after) continue;
    const size_t slot = s % EVENT_LOG_CAPACITY;
    if (ring_[slot].seq != s) continue;
    ring_[slot] = FeederEvent{};
    writeSlot(slot);
    n++;
  }
  Serial.printf("[log] %u evento(s) depois do seq %lu apagados\n", (unsigned)n,
                (unsigned long)after);
  return n;
}

// ------------------------------------------------------------------ leitura

bool EventLog::nextAfter(uint32_t after, FeederEvent &out) const {
  const uint32_t last = lastSeq();
  if (after >= last) return false;
  const uint32_t first = firstInWindow(last);
  for (uint32_t s = (after + 1 > first) ? after + 1 : first; s <= last; s++) {
    const FeederEvent &e = ring_[s % EVENT_LOG_CAPACITY];
    if (e.seq == s) {
      out = e;
      return true;
    }
  }
  return false;
}

size_t EventLog::latest(FeederEvent *out, size_t maxItems) const {
  const uint32_t last = lastSeq();
  if (last == 0 || maxItems == 0) return 0;
  const uint32_t first = firstInWindow(last);
  size_t n = 0;
  for (uint32_t s = last; s >= first && n < maxItems; s--) {
    const FeederEvent &e = ring_[s % EVENT_LOG_CAPACITY];
    if (e.seq == s) out[n++] = e;
  }
  std::reverse(out, out + n);
  return n;
}

size_t EventLog::count() const {
  const uint32_t last = lastSeq();
  if (last == 0) return 0;
  size_t n = 0;
  for (uint32_t s = firstInWindow(last); s <= last; s++) {
    if (ring_[s % EVENT_LOG_CAPACITY].seq == s) n++;
  }
  return n;
}
