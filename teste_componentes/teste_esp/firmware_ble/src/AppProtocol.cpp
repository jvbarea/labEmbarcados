#include "AppProtocol.h"
#include <ArduinoJson.h>

// Com que frequencia o loop confere se o status mudou (e se vale mandar ja).
static const unsigned long STATUS_CHECK_MS = 50;
// Intervalo minimo entre dois status "na hora", para um peso oscilando bem na
// borda de uma faixa nao inundar o BLE.
static const unsigned long STATUS_MIN_GAP_MS = 100;
// Quantas notificacoes podem estar na fila antes de o sync e os eventos ao vivo
// esperarem. Os acks e o status nunca esperam.
static const size_t SYNC_MAX_BACKLOG = 3;
static const size_t LIVE_MAX_BACKLOG = 6;
// Maior "profile" aceito. Os dois gatos com nome, emoji e cor dao ~200 bytes.
static const size_t PROFILE_MAX = 400;

void AppProtocol::begin(BleControl *ble, IWeightSource *scale, MockWeightSource *mock,
                        Hx711WeightSource *hx, DoorActuator *door,
                        AccessStateMachine *fsm, EventLog *log, Storage *storage) {
  ble_ = ble;
  scale_ = scale;
  mock_ = mock;
  hx_ = hx;
  door_ = door;
  fsm_ = fsm;
  log_ = log;
  storage_ = storage;
  profile_ = storage_->loadProfile();
  lastPushedSeq_ = log_->lastSeq();
}

// ------------------------------------------------------------------- saidas

/*
 * O app so aceita door em {closed, opening, open, closing, fault} - qualquer
 * outro valor faz o decodificador descartar a mensagem inteira. O estado
 * CALIBRATING, que e so de bancada, vira "fault": do ponto de vista do app o
 * dispositivo realmente nao esta em operacao normal.
 */
static const char *doorStateV2(DoorState s) {
  if (s == DoorState::CALIBRATING) return "fault";
  return doorStateName(s);
}

static const char *modeName(const AppConfig &cfg) {
  return cfg.learnMode ? "learn" : "auto";
}

void AppProtocol::sendDoc(JsonDocument &doc, bool logSerial) {
  String out;
  serializeJson(doc, out);
  ble_->send(out);
  if (logSerial) Serial.println("[tx] " + out);
}

String AppProtocol::statusJson() const {
  JsonDocument doc;
  doc["t"]    = "st";
  doc["wG"]   = (int32_t)lroundf(scale_->weightG());
  doc["cat"]  = fsm_->currentCat();
  doc["door"] = doorStateV2(door_->state());
  doc["fcC"]  = door_->fcClosed() ? 1 : 0;
  doc["fcO"]  = door_->fcOpen() ? 1 : 0;

  const GrantState &g = fsm_->grantState();
  if (g.active) {
    JsonObject go = doc["grant"].to<JsonObject>();
    go["cat"] = g.cat;
    if (g.hasUntil)  go["untilS"] = g.untilS;
    if (g.hasVisits) go["visits"] = g.visits;
  } else {
    doc["grant"] = nullptr;
  }

  doc["mode"] = modeName(fsm_->config());
  doc["cfgV"] = fsm_->config().cfgV;
  // So durante o reabastecer: o app conta o tempo a partir daqui (campo opcional,
  // o app antigo ignora).
  if (fsm_->refillActive()) doc["refillS"] = fsm_->refillLeftS();
  String out;
  serializeJson(doc, out);
  return out;
}

// Tudo o que, mudando, merece um status na hora (o peso fica de fora: ele muda o
// tempo todo e ja sai no periodico).
String AppProtocol::statusKey() const {
  const GrantState &g = fsm_->grantState();
  String k;
  k.reserve(40);
  k += doorStateV2(door_->state());
  k += '|';
  k += fsm_->currentCat();
  k += '|';
  k += fsm_->config().learnMode ? 'L' : 'A';
  k += '|';
  if (g.active) {
    k += g.cat;
    k += ',';
    k += g.hasUntil ? g.untilS : 0;
    k += ',';
    k += g.hasVisits ? g.visits : -1;
  }
  k += '|';
  k += fsm_->config().cfgV;
  k += fsm_->refillActive() ? 'R' : '-';
  return k;
}

void AppProtocol::sendStatus() {
  ble_->send(statusJson());
  lastStatusMs_ = millis();
  lastStatusKey_ = statusKey();
}

void AppProtocol::sendAck(int id, bool ok, const char *err) {
  JsonDocument doc;
  doc["t"]  = "ack";
  doc["id"] = id;
  doc["ok"] = ok;
  if (err) doc["err"] = err;
  sendDoc(doc, true);
}

void AppProtocol::sendEvent(const FeederEvent &e) {
  JsonDocument doc;
  doc["t"]    = "ev";
  doc["seq"]  = e.seq;
  doc["ts"]   = e.ts;
  doc["kind"] = evKindName(e.kind);

  if (e.kind == EvKind::VISIT) {
    doc["cat"]  = e.cat;
    doc["wG"]   = e.wG;
    doc["durS"] = e.durS;
    doc["door"] = e.opened ? "opened" : "denied";
    const char *why = evWhyName(e.why);
    if (why) doc["why"] = why;
    if (e.why == EvWhy::LEARN) doc["learn"] = 1;
  } else if (e.kind == EvKind::REFILL) {
    doc["durS"] = e.durS;
  } else if (e.kind == EvKind::FAULT) {
    const char *err = evErrName(e.err);
    if (err) doc["err"] = err;
  }

  // Os do sync nao vao para o serial: com o historico cheio seriam 256 linhas
  // segurando o loop.
  sendDoc(doc, !sync_.active);
}

void AppProtocol::sendCfgDump() {
  const AppConfig &cfg = fsm_->config();

  JsonDocument doc;
  doc["t"] = "cfgDump";
  JsonArray cats = doc["cats"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.catCount; i++) {
    JsonObject c = cats.add<JsonObject>();
    c["k"]    = cfg.cats[i].k;
    c["minG"] = cfg.cats[i].minG;
    c["maxG"] = cfg.cats[i].maxG;
    c["rule"] = cfg.cats[i].allow ? "allow" : "deny";
  }
  doc["confMs"]   = cfg.confMs;
  doc["leaveMs"]  = cfg.leaveMs;
  doc["maxOpenS"] = cfg.maxOpenS;
  doc["mode"]     = modeName(cfg);
  doc["cfgV"]     = cfg.cfgV;
  // Passa do MTU com folga; o BleControl manda em pedacos.
  if (profile_.length() > 0) doc["profile"] = profile_;

  sendDoc(doc, true);
}

// Eventos novos vao ao vivo, um por volta do loop. So depois do hello: antes
// disso o ts ainda e 0, e o app descartaria o evento (e nao pediria de novo).
void AppProtocol::pushNewEvents() {
  if (!log_->hasClock()) return;
  if (ble_->txBacklog() >= LIVE_MAX_BACKLOG) return;
  FeederEvent e;
  if (log_->nextAfter(lastPushedSeq_, e)) {
    sendEvent(e);
    lastPushedSeq_ = e.seq;
  } else {
    lastPushedSeq_ = log_->lastSeq();
  }
}

// O sync anda um evento por vez, quando a fila da pra mais um: o loop nunca
// para, e o servo e a balanca seguem sendo atendidos durante a sincronizacao.
void AppProtocol::pumpSync() {
  if (!sync_.active) return;
  if (ble_->txBacklog() >= SYNC_MAX_BACKLOG) return;

  FeederEvent e;
  if (log_->nextAfter(sync_.cursor, e)) {
    sendEvent(e);
    sync_.cursor = e.seq;
    return;
  }

  JsonDocument fim;
  fim["t"]    = "syncEnd";
  fim["id"]   = sync_.id;
  fim["last"] = log_->lastSeq();
  sync_.active = false;
  sendDoc(fim, true);
}

// ------------------------------------------------------------------ entradas

static bool sameCats(const AppConfig &a, const AppConfig &b) {
  if (a.catCount != b.catCount) return false;
  for (uint8_t i = 0; i < a.catCount; i++) {
    const DeviceCat &x = a.cats[i];
    const DeviceCat &y = b.cats[i];
    if (x.k != y.k || x.minG != y.minG || x.maxG != y.maxG || x.allow != y.allow) return false;
  }
  return true;
}

static bool sameConfig(const AppConfig &a, const AppConfig &b) {
  return a.confMs == b.confMs && a.leaveMs == b.leaveMs && a.maxOpenS == b.maxOpenS &&
         a.doorClosedDeg == b.doorClosedDeg && a.doorOpenDeg == b.doorOpenDeg &&
         a.servoUs0 == b.servoUs0 && a.servoUs90 == b.servoUs90 &&
         a.learnMode == b.learnMode && sameCats(a, b) &&
         a.calibFactor == b.calibFactor && a.tareOffset == b.tareOffset &&
         a.benchMinG == b.benchMinG && a.benchMaxG == b.benchMaxG;
}

bool AppProtocol::saveIfChanged(AppConfig &cfg) {
  if (sameConfig(cfg, fsm_->config())) return false;
  storage_->saveAppConfig(cfg);  // incrementa o cfgV
  fsm_->applyConfig(cfg);
  return true;
}

// Guarda em NVS o que a tara ou a calibracao acabou de produzir, para
// sobreviver ao reboot sem precisar refazer com a massa padrao.
void AppProtocol::persisteCalibracao() {
  if (!hx_) return;
  AppConfig cfg = fsm_->config();
  cfg.calibFactor = hx_->calibFactor();
  cfg.tareOffset  = hx_->tareOffset();
  saveIfChanged(cfg);
  Serial.printf("[proto] calibracao gravada: fator=%.3f offset=%ld\n",
                hx_->calibFactor(), hx_->tareOffset());
}

void AppProtocol::handleCmd(int id, JsonDocument &doc) {
  // Extensoes de bancada usam "c"; o protocolo v2 usa "op".
  if (doc["c"].is<const char *>()) {
    const char *c = doc["c"];
    if (strcmp(c, "mock") == 0) {
      if (!mock_) { sendAck(id, false, "real_scale"); return; }
      if (!doc["wG"].is<float>()) { sendAck(id, false, "bad_range"); return; }
      mock_->setTargetWeightG(doc["wG"].as<float>());
      sendAck(id, true);
    } else if (strcmp(c, "sensor") == 0) {
      if (!mock_) { sendAck(id, false, "real_scale"); return; }
      mock_->setHealthy(doc["ok"] | true);
      sendAck(id, true);
    } else if (strcmp(c, "status") == 0) {
      sendAck(id, true);
      sendStatus();
    } else if (strcmp(c, "reset") == 0) {
      fsm_->reset();
      sendAck(id, true);
    } else if (strcmp(c, "evclear") == 0) {
      log_->clear();
      lastPushedSeq_ = log_->lastSeq();
      sendAck(id, true);
    } else if (strcmp(c, "evtrim") == 0) {
      if (!doc["after"].is<uint32_t>()) { sendAck(id, false, "bad_range"); return; }
      log_->dropAfter(doc["after"].as<uint32_t>());
      sendAck(id, true);
    } else {
      sendAck(id, false, "bad_cmd");
    }
    return;
  }

  const char *op = doc["op"] | "";

  if (strcmp(op, "refill") == 0) {
    uint32_t timeoutS = fsm_->config().maxOpenS;
    if (doc["timeoutS"].is<long>()) {
      const long v = doc["timeoutS"].as<long>();
      if (v < MAX_OPEN_S_MIN || v > MAX_OPEN_S_MAX) { sendAck(id, false, "bad_range"); return; }
      timeoutS = (uint32_t)v;
    }
    if (!fsm_->startRefill(timeoutS)) { sendAck(id, false, "door_fault"); return; }
    sendAck(id, true);

  } else if (strcmp(op, "close") == 0) {
    // Com a porta em falha, rearma. Com animal na plataforma o ack sai na hora
    // e a porta fecha quando ele sair (RNF04).
    fsm_->requestClose();
    sendAck(id, true);

  } else if (strcmp(op, "tare") == 0) {
    if (!hx_) {  // balanca simulada: tarar e so zerar o valor
      mock_->setTargetWeightG(0);
      sendAck(id, true);
      return;
    }
    // A tara bloqueia ~2 s tirando a media. Com a porta em movimento isso
    // atrasaria a avaliacao do timeout, com o servo energizado.
    if (door_->isMoving()) { sendAck(id, false, "door_moving"); return; }
    if (!hx_->tare()) { sendAck(id, false, "scale_mute"); return; }
    persisteCalibracao();
    sendAck(id, true);

  } else if (strcmp(op, "calibrate") == 0) {
    if (!hx_) { sendAck(id, false, "no_scale"); return; }
    if (!doc["refG"].is<float>() || doc["refG"].as<float>() <= 0) {
      sendAck(id, false, "missing_ref");
      return;
    }
    if (door_->isMoving()) { sendAck(id, false, "door_moving"); return; }
    if (!hx_->calibrate(doc["refG"].as<float>())) {
      // Ou o modulo emudeceu, ou a leitura nao mudou com a massa em cima.
      sendAck(id, false, "calib_failed");
      return;
    }
    persisteCalibracao();
    sendAck(id, true);

  } else {
    sendAck(id, false, "bad_op");
  }
}

void AppProtocol::handleCfg(int id, JsonDocument &doc) {
  AppConfig cfg = fsm_->config();

  if (doc["confMs"].is<long>()) {
    const long v = doc["confMs"].as<long>();
    if (v < CONF_MS_MIN || v > CONF_MS_MAX) { sendAck(id, false, "bad_range"); return; }
    cfg.confMs = (uint32_t)v;
  }
  if (doc["leaveMs"].is<long>()) {
    const long v = doc["leaveMs"].as<long>();
    if (v < LEAVE_MS_MIN || v > LEAVE_MS_MAX) { sendAck(id, false, "bad_range"); return; }
    cfg.leaveMs = (uint32_t)v;
  }
  if (doc["maxOpenS"].is<long>()) {
    const long v = doc["maxOpenS"].as<long>();
    if (v < MAX_OPEN_S_MIN || v > MAX_OPEN_S_MAX) { sendAck(id, false, "bad_range"); return; }
    cfg.maxOpenS = (uint32_t)v;
  }

  // Extensoes de bancada (fora do v2).
  if (doc["closedDeg"].is<int>() || doc["openDeg"].is<int>()) {
    const int fechada = doc["closedDeg"] | cfg.doorClosedDeg;
    const int aberta  = doc["openDeg"] | cfg.doorOpenDeg;
    if (fechada < 0 || fechada > 180 || aberta < 0 || aberta > 180) {
      sendAck(id, false, "bad_range");
      return;
    }
    cfg.doorClosedDeg = fechada;
    cfg.doorOpenDeg = aberta;
  }
  if (doc["minG"].is<float>() || doc["maxG"].is<float>()) {
    const float minG = doc["minG"] | cfg.benchMinG;
    const float maxG = doc["maxG"] | cfg.benchMaxG;
    if (minG < 0 || minG > maxG) { sendAck(id, false, "bad_range"); return; }
    cfg.benchMinG = minG;
    cfg.benchMaxG = maxG;
  }

  saveIfChanged(cfg);
  sendAck(id, true);
}

void AppProtocol::handleCats(int id, JsonDocument &doc) {
  JsonArray arr = doc["cats"].as<JsonArray>();
  if (arr.isNull() || arr.size() > MAX_CATS) { sendAck(id, false, "bad_cats"); return; }

  DeviceCat novos[MAX_CATS];
  bool visto[3] = {false, false, false};
  uint8_t n = 0;
  for (JsonObject c : arr) {
    const int k = c["k"] | 0;
    if ((k != 1 && k != 2) || visto[k]) { sendAck(id, false, "unknown_cat"); return; }
    visto[k] = true;
    if (!c["minG"].is<long>() || !c["maxG"].is<long>()) { sendAck(id, false, "bad_range"); return; }
    const long minG = c["minG"].as<long>();
    const long maxG = c["maxG"].as<long>();
    if (minG < 0 || maxG > CAT_WEIGHT_MAX_G || minG > maxG) {
      sendAck(id, false, "bad_range");
      return;
    }
    const char *rule = c["rule"] | "";
    if (strcmp(rule, "allow") != 0 && strcmp(rule, "deny") != 0) {
      sendAck(id, false, "bad_cats");
      return;
    }
    novos[n].k = (uint8_t)k;
    novos[n].minG = (int32_t)minG;
    novos[n].maxG = (int32_t)maxG;
    novos[n].allow = (strcmp(rule, "allow") == 0);
    n++;
  }

  AppConfig cfg = fsm_->config();
  for (uint8_t i = 0; i < MAX_CATS; i++) cfg.cats[i] = (i < n) ? novos[i] : DeviceCat{};
  cfg.catCount = n;
  saveIfChanged(cfg);
  sendAck(id, true);
}

void AppProtocol::handleMode(int id, JsonDocument &doc) {
  const char *m = doc["mode"] | "";
  const bool learn = (strcmp(m, "learn") == 0);
  if (!learn && strcmp(m, "auto") != 0) { sendAck(id, false, "bad_range"); return; }

  AppConfig cfg = fsm_->config();
  cfg.learnMode = learn;
  // Grava (o modo dura dias e precisa sobreviver a queda de energia) e aplica:
  // a AccessStateMachine faz com a porta o que o modo novo pede.
  saveIfChanged(cfg);
  sendAck(id, true);
}

void AppProtocol::handleProfile(int id, JsonDocument &doc) {
  if (!doc["blob"].is<const char *>()) { sendAck(id, false, "bad_json"); return; }
  const String blob = doc["blob"].as<const char *>();
  if (blob.length() > PROFILE_MAX) { sendAck(id, false, "too_big"); return; }

  if (blob != profile_) {
    storage_->saveProfile(blob);
    profile_ = blob;
    // O simulador sobe o cfgV quando o perfil muda; aqui tambem.
    AppConfig cfg = fsm_->config();
    storage_->saveAppConfig(cfg);
    fsm_->applyConfig(cfg);
  }
  sendAck(id, true);
}

void AppProtocol::handleGrant(int id, JsonDocument &doc) {
  const int cat = doc["cat"] | 0;
  if (cat != 1 && cat != 2) { sendAck(id, false, "unknown_cat"); return; }

  const bool hasUntil = doc["untilS"].is<uint32_t>();
  const bool hasVisits = doc["visits"].is<int>();
  const char *err = fsm_->grant((uint8_t)cat,
                                hasUntil, hasUntil ? doc["untilS"].as<uint32_t>() : 0,
                                hasVisits, hasVisits ? doc["visits"].as<int>() : 0);
  sendAck(id, err == nullptr, err);
}

void AppProtocol::handleSync(int id, JsonDocument &doc) {
  // Como no simulador: ack primeiro, depois os eventos, depois o syncEnd.
  sendAck(id, true);
  sync_.active = true;
  sync_.id = id;
  sync_.cursor = doc["since"].as<uint32_t>();  // ausente ou invalido = 0, manda tudo
}

void AppProtocol::handleMessage(const char *raw, size_t len) {
  Serial.printf("[rx] %.*s\n", (int)len, raw);

  JsonDocument doc;
  if (deserializeJson(doc, raw, len)) {
    sendAck(0, false, "bad_json");
    return;
  }

  const int id = doc["id"] | 0;
  const char *t = doc["t"] | "";

  if (strcmp(t, "hello") == 0) {
    log_->setEpochBase(doc["now"].as<uint32_t>());
    Serial.printf("[proto] hello v%d, relogio %s\n",
                  (int)(doc["v"] | 0), log_->hasClock() ? "ajustado" : "sem hora");
    sendAck(id, true);
    sendStatus();
    return;
  }

  if (strcmp(t, "cmd") == 0)     { handleCmd(id, doc); return; }
  if (strcmp(t, "cfg") == 0)     { handleCfg(id, doc); return; }
  if (strcmp(t, "cats") == 0)    { handleCats(id, doc); return; }
  if (strcmp(t, "mode") == 0)    { handleMode(id, doc); return; }
  if (strcmp(t, "profile") == 0) { handleProfile(id, doc); return; }
  if (strcmp(t, "grant") == 0)   { handleGrant(id, doc); return; }
  if (strcmp(t, "sync") == 0)    { handleSync(id, doc); return; }
  if (strcmp(t, "cfgGet") == 0)  { sendCfgDump(); sendAck(id, true); return; }

  if (strcmp(t, "revoke") == 0) {
    fsm_->revoke((uint8_t)(doc["cat"] | 0));
    sendAck(id, true);
    return;
  }

  sendAck(id, false, "bad_type");
}

// --------------------------------------------------------------------- tick

void AppProtocol::tick() {
  if (!ble_->isConnected()) {
    // O que acontece sem ninguem ouvindo chega ao app pelo sync da proxima conexao.
    sync_.active = false;
    lastPushedSeq_ = log_->lastSeq();
    lastStatusKey_ = "";
    return;
  }

  pumpSync();
  pushNewEvents();

  const unsigned long now = millis();
  if (now - lastStatusMs_ >= STATUS_PERIOD_MS) {
    sendStatus();
    return;
  }
  static unsigned long ultimaConferencia = 0;
  if (now - ultimaConferencia < STATUS_CHECK_MS) return;
  ultimaConferencia = now;
  if (now - lastStatusMs_ >= STATUS_MIN_GAP_MS && statusKey() != lastStatusKey_) sendStatus();
}
