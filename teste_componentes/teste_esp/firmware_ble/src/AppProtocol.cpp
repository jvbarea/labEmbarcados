#include "AppProtocol.h"
#include <ArduinoJson.h>

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

String AppProtocol::statusJson() const {
  JsonDocument doc;
  doc["t"]    = "st";
  doc["wG"]   = (int32_t)lroundf(scale_->weightG());
  doc["cat"]  = fsm_->currentCat();
  doc["door"] = doorStateV2(door_->state());
  doc["fcC"]  = door_->fcClosed() ? 1 : 0;
  doc["fcO"]  = door_->fcOpen() ? 1 : 0;
  doc["grant"] = nullptr;  // Etapa 3
  doc["mode"] = fsm_->config().learnMode ? "learn" : "auto";
  doc["cfgV"] = fsm_->config().cfgV;
  String out;
  serializeJson(doc, out);
  return out;
}

void AppProtocol::sendStatus() {
  String s = statusJson();
  ble_->notify(s);
}

void AppProtocol::sendAck(int id, bool ok, const char *err) {
  JsonDocument doc;
  doc["t"]  = "ack";
  doc["id"] = id;
  doc["ok"] = ok;
  if (err) doc["err"] = err;
  String out;
  serializeJson(doc, out);
  ble_->notify(out);
  Serial.println("[tx] " + out);
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

  String out;
  serializeJson(doc, out);
  ble_->notify(out);
  Serial.println("[ev] " + out);
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
  doc["mode"]     = cfg.learnMode ? "learn" : "auto";
  doc["cfgV"]     = cfg.cfgV;

  // O profile e um blob livre do app e pode passar do MTU. So vai junto se
  // couber; senao o app percebe a ausencia e reenvia o dele.
  String profile = storage_->loadProfile();
  if (profile.length() > 0 && profile.length() <= 90) {
    doc["profile"] = profile;
  } else if (profile.length() > 90) {
    Serial.printf("[proto] profile de %u bytes nao cabe no MTU, omitido do cfgDump\n",
                  (unsigned)profile.length());
  }

  String out;
  serializeJson(doc, out);
  ble_->notify(out);
  Serial.println("[tx] " + out);
}

void AppProtocol::pushNewEvents() {
  if (log_->lastSeq() == lastPushedSeq_) return;

  FeederEvent novos[8];
  size_t n = log_->since(lastPushedSeq_, novos, 8);
  for (size_t i = 0; i < n; i++) {
    sendEvent(novos[i]);
    lastPushedSeq_ = novos[i].seq;
    delay(20);  // espaco para a pilha BLE escoar a fila de notificacoes
  }
  lastPushedSeq_ = log_->lastSeq();
}

// ------------------------------------------------------------------ entradas

void AppProtocol::handleCmd(int id, JsonDocument &doc) {
  // Extensoes de bancada usam "c"; o protocolo v2 usa "op".
  if (doc["c"].is<const char *>()) {
    const char *c = doc["c"];
    if (strcmp(c, "mock") == 0) {
      if (!mock_) { sendAck(id, false, "balanca real"); return; }
      if (!doc["wG"].is<float>()) { sendAck(id, false, "falta wG"); return; }
      mock_->setTargetWeightG(doc["wG"].as<float>());
      sendAck(id, true);
    } else if (strcmp(c, "sensor") == 0) {
      if (!mock_) { sendAck(id, false, "balanca real"); return; }
      mock_->setHealthy(doc["ok"] | true);
      sendAck(id, true);
    } else if (strcmp(c, "status") == 0) {
      sendAck(id, true);
      sendStatus();
    } else if (strcmp(c, "reset") == 0) {
      fsm_->reset();
      sendAck(id, true);
    } else {
      sendAck(id, false, "bad_cmd");
    }
    return;
  }

  const char *op = doc["op"] | "";

  if (strcmp(op, "refill") == 0) {
    uint32_t timeoutS = doc["timeoutS"] | fsm_->config().maxOpenS;
    fsm_->startRefill(timeoutS);
    sendAck(id, true);
  } else if (strcmp(op, "close") == 0) {
    fsm_->manualClose();
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
    if (!doc["refG"].is<float>()) { sendAck(id, false, "falta refG"); return; }
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

// Guarda em NVS o que a tara ou a calibracao acabou de produzir, para
// sobreviver ao reboot sem precisar refazer com a massa padrao.
void AppProtocol::persisteCalibracao() {
  if (!hx_) return;
  AppConfig cfg = fsm_->config();
  cfg.calibFactor = hx_->calibFactor();
  cfg.tareOffset  = hx_->tareOffset();
  storage_->saveAppConfig(cfg);
  fsm_->applyConfig(cfg);
  Serial.printf("[proto] calibracao gravada: fator=%.3f offset=%ld\n",
                hx_->calibFactor(), hx_->tareOffset());
}

void AppProtocol::handleCfg(int id, JsonDocument &doc) {
  AppConfig cfg = fsm_->config();
  if (doc["confMs"].is<int>())   cfg.confMs   = doc["confMs"].as<uint32_t>();
  if (doc["leaveMs"].is<int>())  cfg.leaveMs  = doc["leaveMs"].as<uint32_t>();
  if (doc["maxOpenS"].is<int>()) cfg.maxOpenS = doc["maxOpenS"].as<uint32_t>();
  // Extensao fora do v2: posicoes da porta, em graus.
  if (doc["closedDeg"].is<int>()) cfg.doorClosedDeg = doc["closedDeg"].as<int>();
  if (doc["openDeg"].is<int>())   cfg.doorOpenDeg   = doc["openDeg"].as<int>();

  storage_->saveAppConfig(cfg);
  fsm_->applyConfig(cfg);
  sendAck(id, true);
}

void AppProtocol::handleCats(int id, JsonDocument &doc) {
  JsonArray arr = doc["cats"].as<JsonArray>();
  if (arr.isNull()) { sendAck(id, false, "bad_cats"); return; }

  AppConfig cfg = fsm_->config();
  uint8_t n = 0;
  for (JsonObject c : arr) {
    if (n >= MAX_CATS) break;
    int k = c["k"] | 0;
    if (k != 1 && k != 2) { sendAck(id, false, "unknown_cat"); return; }
    const char *rule = c["rule"] | "allow";
    cfg.cats[n].k     = (uint8_t)k;
    cfg.cats[n].minG  = c["minG"] | 0;
    cfg.cats[n].maxG  = c["maxG"] | 0;
    cfg.cats[n].allow = (strcmp(rule, "deny") != 0);
    if (cfg.cats[n].minG > cfg.cats[n].maxG) { sendAck(id, false, "bad_range"); return; }
    n++;
  }
  cfg.catCount = n;

  storage_->saveAppConfig(cfg);
  fsm_->applyConfig(cfg);
  sendAck(id, true);
}

void AppProtocol::handleSync(int id, JsonDocument &doc) {
  uint32_t since = doc["since"] | 0;

  FeederEvent itens[8];
  size_t n = log_->since(since, itens, 8);
  for (size_t i = 0; i < n; i++) {
    sendEvent(itens[i]);
    delay(20);
  }

  JsonDocument fim;
  fim["t"]    = "syncEnd";
  fim["id"]   = id;
  fim["last"] = log_->lastSeq();
  String out;
  serializeJson(fim, out);
  ble_->notify(out);
  Serial.println("[tx] " + out);
}

void AppProtocol::handleJson(const char *raw) {
  Serial.printf("[rx] %s\n", raw);

  JsonDocument doc;
  if (deserializeJson(doc, raw)) {
    sendAck(0, false, "bad_json");
    return;
  }

  int id = doc["id"] | 0;
  const char *t = doc["t"] | "";

  if (strcmp(t, "hello") == 0) {
    uint32_t now = doc["now"] | 0;
    log_->setEpochBase(now);
    Serial.printf("[proto] hello v%d, relogio %s\n",
                  (int)(doc["v"] | 0), log_->hasClock() ? "ajustado" : "sem hora");
    sendAck(id, true);
    sendStatus();
    return;
  }

  if (strcmp(t, "cmd") == 0)    { handleCmd(id, doc); return; }
  if (strcmp(t, "cfg") == 0)    { handleCfg(id, doc); return; }
  if (strcmp(t, "cats") == 0)   { handleCats(id, doc); return; }
  if (strcmp(t, "sync") == 0)   { handleSync(id, doc); return; }
  if (strcmp(t, "cfgGet") == 0) { sendCfgDump(); sendAck(id, true); return; }

  if (strcmp(t, "mode") == 0) {
    const char *m = doc["mode"] | "auto";
    AppConfig cfg = fsm_->config();
    cfg.learnMode = (strcmp(m, "learn") == 0);
    storage_->saveAppConfig(cfg);
    fsm_->applyConfig(cfg);
    sendAck(id, true);
    return;
  }

  if (strcmp(t, "profile") == 0) {
    const char *blob = doc["blob"] | "";
    storage_->saveProfile(String(blob));
    sendAck(id, true);
    return;
  }

  // Etapa 3: liberacao temporaria de um gato bloqueado.
  if (strcmp(t, "grant") == 0 || strcmp(t, "revoke") == 0) {
    sendAck(id, false, "not_supported");
    return;
  }

  sendAck(id, false, "bad_type");
}

// --------------------------------------------------------------------- tick

void AppProtocol::tick() {
  pushNewEvents();

  if (millis() - lastStatusMs_ >= STATUS_PERIOD_MS) {
    lastStatusMs_ = millis();
    if (ble_->isConnected()) sendStatus();
  }
}
