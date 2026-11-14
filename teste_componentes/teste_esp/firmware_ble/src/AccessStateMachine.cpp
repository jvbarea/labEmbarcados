#include "AccessStateMachine.h"

const char *accessStateName(AccessState s) {
  switch (s) {
    case AccessState::IDLE:       return "idle";
    case AccessState::CONFIRMING: return "confirming";
    case AccessState::VISIT:      return "visit";
    case AccessState::LEAVING:    return "leaving";
    case AccessState::SAFE_FAULT: return "safe_fault";
    default:                      return "unknown";
  }
}

void AccessStateMachine::begin(IWeightSource *sensor, DoorActuator *door,
                               EventLog *log, const AppConfig &cfg) {
  sensor_ = sensor;
  door_   = door;
  log_    = log;
  cfg_    = cfg;
  door_->setPositions(cfg.doorClosedDeg, cfg.doorOpenDeg);
  door_->setServoCalibration(cfg.servoUs0, cfg.servoUs90);
  initialized_ = true;
  setState(AccessState::IDLE);

  // Modo adaptacao gravado na NVS, com a porta que fica aberta o modo inteiro:
  // reabre depois do homing do boot.
  if (cfg_.learnMode && LEARN_DOOR_STAYS_OPEN) openDoor();
}

void AccessStateMachine::applyConfig(const AppConfig &cfg) {
  const bool modeChanged = (cfg.learnMode != cfg_.learnMode);
  cfg_ = cfg;
  if (door_) {
    door_->setPositions(cfg.doorClosedDeg, cfg.doorOpenDeg);
    door_->setServoCalibration(cfg.servoUs0, cfg.servoUs90);
  }
  if (modeChanged && initialized_) onModeChanged();
}

void AccessStateMachine::setState(AccessState s) {
  if (s == state_) return;
  Serial.printf("[fsm] %s -> %s\n", accessStateName(state_), accessStateName(s));
  state_ = s;
}

// ----------------------------------------------------------------- regras

uint8_t AccessStateMachine::identify(float w) const {
  if (!present(w)) return 0;
  for (uint8_t i = 0; i < cfg_.catCount; i++) {
    const DeviceCat &c = cfg_.cats[i];
    if (w >= c.minG && w <= c.maxG) return c.k;
  }
  return 0;
}

bool AccessStateMachine::catRegistered(uint8_t cat) const {
  for (uint8_t i = 0; i < cfg_.catCount; i++) {
    if (cfg_.cats[i].k == cat) return true;
  }
  return false;
}

bool AccessStateMachine::catAllowed(uint8_t cat) const {
  for (uint8_t i = 0; i < cfg_.catCount; i++) {
    if (cfg_.cats[i].k == cat) return cfg_.cats[i].allow;
  }
  return false;
}

uint8_t AccessStateMachine::currentCat() const {
  if (!sensor_) return 0;
  return identify(sensor_->weightG());
}

// grantApplies() de app/src/domain/rules.ts
bool AccessStateMachine::grantApplies(uint8_t cat, uint32_t nowS) const {
  if (!grant_.active || cat == 0 || grant_.cat != cat) return false;
  if (grant_.hasUntil && (nowS == 0 || nowS >= grant_.untilS)) return false;
  if (grant_.hasVisits && grant_.visits <= 0) return false;
  return true;
}

// decide() de app/src/domain/rules.ts
Decision AccessStateMachine::decide(float w) const {
  Decision d{0, false, EvWhy::UNKNOWN};

  if (cfg_.catCount == 0) {
    // Bancada, sem cadastro: janela unica de peso.
    d.why = EvWhy::RULE;
    d.allowed = (w >= cfg_.benchMinG && w <= cfg_.benchMaxG);
    return d;
  }

  d.cat = identify(w);
  if (d.cat == 0) return d;  // desconhecido: nega

  if (grantApplies(d.cat, log_ ? log_->nowEpoch() : 0)) {
    d.allowed = true;
    d.why = EvWhy::GRANT;
    return d;
  }
  d.why = EvWhy::RULE;
  d.allowed = catAllowed(d.cat);
  return d;
}

// consumeGrant() de app/src/domain/rules.ts
void AccessStateMachine::consumeGrant() {
  if (!grant_.active || !grant_.hasVisits) return;
  grant_.visits--;
  if (grant_.visits <= 0) {
    grant_ = GrantState{};
    Serial.println("[fsm] liberacao por visitas usada ate o fim");
  }
}

// grantExpired() de app/src/domain/rules.ts
void AccessStateMachine::expireGrant() {
  if (!grant_.active) return;
  const uint32_t nowS = log_ ? log_->nowEpoch() : 0;
  const bool venceuPrazo = grant_.hasUntil && nowS != 0 && nowS >= grant_.untilS;
  const bool acabaram = grant_.hasVisits && grant_.visits <= 0;
  if (venceuPrazo || acabaram) {
    grant_ = GrantState{};
    Serial.println("[fsm] liberacao expirou");
  }
}

const char *AccessStateMachine::grant(uint8_t cat, bool hasUntil, uint32_t untilS,
                                      bool hasVisits, int32_t visits) {
  if ((cat != 1 && cat != 2) || !catRegistered(cat)) return "unknown_cat";
  // O prazo e epoch do celular; sem o hello a ESP32 nao sabe que horas sao.
  if (hasUntil && (!log_ || !log_->hasClock())) return "no_clock";
  if (hasVisits && visits <= 0) return "bad_range";

  grant_.active = true;
  grant_.cat = cat;
  grant_.hasUntil = hasUntil;
  grant_.untilS = hasUntil ? untilS : 0;
  grant_.hasVisits = hasVisits;
  grant_.visits = hasVisits ? visits : 0;
  Serial.printf("[fsm] gato %u liberado", (unsigned)cat);
  if (hasUntil && log_) {
    const uint32_t agora = log_->nowEpoch();
    Serial.printf(" por %ld s", (long)(untilS > agora ? untilS - agora : 0));
  }
  if (hasVisits) Serial.printf(" por %ld visita(s)", (long)visits);
  Serial.println();
  return nullptr;
}

void AccessStateMachine::revoke(uint8_t cat) {
  if (grant_.active && grant_.cat == cat) {
    grant_ = GrantState{};
    Serial.printf("[fsm] liberacao do gato %u cancelada\n", (unsigned)cat);
  }
}

// ------------------------------------------------------------------ porta

bool AccessStateMachine::openDoor() {
  if (door_->isFault() || door_->isCalibrating()) return false;
  closePending_ = false;  // decidir abrir vence um pedido de fechar antigo
  door_->requestOpen();
  return true;
}

// RNF04: com peso na plataforma nao fecha; o pedido espera ela esvaziar por
// leaveMs (ver o fim de update()).
void AccessStateMachine::closeWhenSafe() {
  if (door_->isFault() || door_->isCalibrating()) return;
  const DoorState s = door_->state();
  if (s == DoorState::CLOSED || s == DoorState::CLOSING) {
    closePending_ = false;
    return;
  }
  if (!present(sensor_->weightG())) {
    door_->requestClose();
    closePending_ = false;
  } else {
    if (!closePending_) Serial.println("[fsm] fechar pendente: animal na plataforma (RNF04)");
    closePending_ = true;
  }
}

bool AccessStateMachine::shouldCloseAfterVisit() const {
  if (refillActive_) return false;
  if (cfg_.learnMode && LEARN_DOOR_STAYS_OPEN) return false;
  if (door_->isFault()) return false;
  const DoorState s = door_->state();
  return s == DoorState::OPEN || s == DoorState::OPENING;
}

uint32_t AccessStateMachine::refillLeftS() const {
  if (!refillActive_) return 0;
  const unsigned long passou = millis() - refillStartMs_;
  if (passou >= refillTimeoutMs_) return 0;
  return (uint32_t)((refillTimeoutMs_ - passou + 999UL) / 1000UL);  // arredonda para cima
}

void AccessStateMachine::endRefill(bool logEvent) {
  if (!refillActive_) return;
  refillActive_ = false;
  if (logEvent && log_) log_->addRefill((millis() - refillStartMs_) / 1000);
}

// ---------------------------------------------------------------- comandos

bool AccessStateMachine::startRefill(uint32_t timeoutS) {
  if (door_->isFault() || door_->isCalibrating()) return false;
  if (timeoutS == 0) timeoutS = cfg_.maxOpenS;
  refillActive_ = true;
  refillStartMs_ = millis();
  refillTimeoutMs_ = timeoutS * 1000UL;
  openDoor();
  Serial.printf("[fsm] reabastecer: porta aberta por ate %lu s\n", (unsigned long)timeoutS);
  return true;
}

void AccessStateMachine::requestClose() {
  endRefill(true);
  if (door_->isCalibrating()) return;
  if (door_->isFault()) {
    // Fechar tambem serve de rearme, como no simulador do app.
    closePending_ = false;
    doorFaultLogged_ = false;
    door_->reset();
    Serial.println("[fsm] porta rearmada");
    return;
  }
  closeWhenSafe();
}

void AccessStateMachine::reset() {
  endRefill(true);
  closePending_ = false;
  doorFaultLogged_ = false;
  resetVisit();
  if (!door_->isCalibrating()) door_->reset();
  setState(AccessState::IDLE);
}

void AccessStateMachine::onModeChanged() {
  Serial.printf("[fsm] modo %s\n", cfg_.learnMode ? "ADAPTACAO" : "automatico");
  endRefill(true);  // trocar de modo encerra o reabastecer, como no simulador
  if (door_->isFault() || door_->isCalibrating()) return;
  if (cfg_.learnMode) {
    if (LEARN_DOOR_STAYS_OPEN) openDoor();
    // Senao a porta abre a cada visita (decisao em updateVisit).
  } else {
    closeWhenSafe();
  }
}

// ----------------------------------------------------------------- visitas

void AccessStateMachine::resetVisit() {
  onPlatform_ = false;
  leaving_ = false;
  decided_ = false;
  visitWeightG_ = 0;
  visitCat_ = 0;
  visitOpened_ = false;
  visitWhy_ = EvWhy::UNKNOWN;
}

// updateVisit() do MockLink, com uma diferenca: a decisao tambem espera a
// balanca estabilizar. O peso do simulador nao tem tranco; o da celula real tem.
void AccessStateMachine::updateVisit(unsigned long now, float w) {
  if (present(w)) {
    leaving_ = false;
    if (!onPlatform_) {
      onPlatform_ = true;
      aboveSinceMs_ = now;
      visitWeightG_ = 0;
      setState(AccessState::CONFIRMING);
    }
    if (sensor_->isStable() && w > visitWeightG_) visitWeightG_ = w;

    if (!decided_ && now - aboveSinceMs_ >= cfg_.confMs && sensor_->isStable()) {
      decided_ = true;
      visitStartMs_ = aboveSinceMs_;
      const Decision d = decide(w);
      visitCat_ = d.cat;

      if (cfg_.learnMode) {
        // Adaptacao: abre para qualquer um e so registra quem subiu e quanto pesava.
        visitWhy_ = EvWhy::LEARN;
        visitOpened_ = openDoor();
      } else {
        visitWhy_ = d.why;
        visitOpened_ = false;
        if (d.allowed) {
          if (d.why == EvWhy::GRANT) consumeGrant();
          visitOpened_ = openDoor();
        }
      }
      Serial.printf("[fsm] decisao: %.0f g, gato %u, %s (%s)\n", w, (unsigned)visitCat_,
                    visitOpened_ ? "ABRE" : "nega", evWhyName(visitWhy_));
      setState(AccessState::VISIT);
    }
    return;
  }

  // Plataforma vazia.
  if (!onPlatform_) return;
  if (!leaving_) {
    leaving_ = true;
    belowSinceMs_ = now;
    setState(AccessState::LEAVING);
  }
  if (now - belowSinceMs_ < cfg_.leaveMs) return;

  if (decided_ && log_) {
    const uint32_t durS = (belowSinceMs_ - visitStartMs_) / 1000;
    log_->addVisit(visitCat_, (int32_t)lroundf(visitWeightG_), durS, visitOpened_, visitWhy_);
  }
  if (shouldCloseAfterVisit()) {
    door_->requestClose();
    closePending_ = false;
  }
  resetVisit();
  setState(AccessState::IDLE);
}

// -------------------------------------------------------------------- update

void AccessStateMachine::update() {
  // Em calibracao de bancada quem manda no servo e o operador.
  if (door_->isCalibrating()) return;

  const unsigned long now = millis();

  // 1) Sensor mudo: estado seguro, a porta fica como esta e a visita se perde.
  if (!sensor_->isHealthy()) {
    if (state_ != AccessState::SAFE_FAULT) {
      if (log_) log_->addFault(EvErr::SENSOR);
      resetVisit();
      setState(AccessState::SAFE_FAULT);
    }
    return;
  }
  if (state_ == AccessState::SAFE_FAULT) setState(AccessState::IDLE);

  const float w = sensor_->weightG();
  if (present(w)) {
    emptyTracking_ = false;
  } else if (!emptyTracking_) {
    emptyTracking_ = true;
    emptySinceMs_ = now;
  }

  // 2) Porta em falha: registra uma vez. As visitas continuam sendo anotadas.
  if (door_->isFault()) {
    if (!doorFaultLogged_) {
      doorFaultLogged_ = true;
      closePending_ = false;
      if (log_) log_->addFault(EvErr::DOOR_TIMEOUT);
      Serial.println("[fsm] porta em FALHA - 'close' pelo app (ou 'reset' no serial) rearma");
    }
  } else {
    doorFaultLogged_ = false;
  }

  // 3) Liberacao vencida.
  expireGrant();

  // 4) Reabastecer com prazo esgotado: fecha (respeitando o RNF04).
  if (refillActive_ && now - refillStartMs_ >= refillTimeoutMs_) {
    endRefill(true);
    Serial.println("[fsm] reabastecer: prazo esgotado, fechando");
    closeWhenSafe();
  }

  // 5) Visitas.
  updateVisit(now, w);

  // 6) Fechamento pendente: plataforma vazia por leaveMs.
  if (closePending_) {
    const DoorState s = door_->state();
    if (door_->isFault() || s == DoorState::CLOSED || s == DoorState::CLOSING) {
      closePending_ = false;
    } else if (emptyTracking_ && now - emptySinceMs_ >= cfg_.leaveMs) {
      door_->requestClose();
      closePending_ = false;
      Serial.println("[fsm] plataforma vazia - fechamento pendente executado");
    }
  }
}
