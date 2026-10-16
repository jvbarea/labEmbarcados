#include "AccessStateMachine.h"

const char *accessStateName(AccessState s) {
  switch (s) {
    case AccessState::IDLE:           return "idle";
    case AccessState::STABILIZING:    return "stabilizing";
    case AccessState::EVALUATING:     return "evaluating";
    case AccessState::ACCESS_OPEN:    return "access_open";
    case AccessState::ACCESS_BLOCKED: return "access_blocked";
    case AccessState::VACATE_CONFIRM: return "vacate_confirm";
    case AccessState::SAFE_FAULT:     return "safe_fault";
    default:                          return "unknown";
  }
}

void AccessStateMachine::begin(IWeightSource *sensor, DoorActuator *door,
                               EventLog *log, const AppConfig &cfg) {
  sensor_ = sensor;
  door_   = door;
  log_    = log;
  cfg_    = cfg;
  door_->setPositions(cfg.doorClosedDeg, cfg.doorOpenDeg);
  enterState(AccessState::IDLE);
}

void AccessStateMachine::applyConfig(const AppConfig &cfg) {
  cfg_ = cfg;
  if (door_) door_->setPositions(cfg.doorClosedDeg, cfg.doorOpenDeg);
}

void AccessStateMachine::enterState(AccessState newState) {
  state_ = newState;
  stateEnteredMs_ = millis();
}

bool AccessStateMachine::platformOccupied() const {
  return sensor_->weightG() > PRESENCE_THRESHOLD_G;
}

bool AccessStateMachine::platformEmpty() const {
  return !platformOccupied();
}

/*
 * Decide se este peso pode comer.
 * Com gatos cadastrados, vale a faixa e a regra de cada um (protocolo v2).
 * Sem cadastro nenhum, cai na janela unica do modo de bancada, que e como o
 * firmware foi testado antes do app existir.
 */
bool AccessStateMachine::weightAllowed(float w, uint8_t &catOut, EvWhy &whyOut) const {
  catOut = 0;

  if (cfg_.catCount > 0) {
    for (uint8_t i = 0; i < cfg_.catCount; i++) {
      const DeviceCat &c = cfg_.cats[i];
      if (w >= c.minG && w <= c.maxG) {
        catOut = c.k;
        whyOut = EvWhy::RULE;
        return c.allow;
      }
    }
    whyOut = EvWhy::UNKNOWN;  // peso nao bate com nenhum gato conhecido
    return false;
  }

  whyOut = EvWhy::RULE;
  return w >= cfg_.benchMinG && w <= cfg_.benchMaxG;
}

uint8_t AccessStateMachine::currentCat() const {
  if (!sensor_ || platformEmpty()) return 0;
  float w = sensor_->weightG();
  for (uint8_t i = 0; i < cfg_.catCount; i++) {
    const DeviceCat &c = cfg_.cats[i];
    if (w >= c.minG && w <= c.maxG) return c.k;
  }
  return 0;
}

// ------------------------------------------------------------------ visitas

void AccessStateMachine::beginVisit() {
  visitActive_ = true;
  visitStartMs_ = millis();
  visitPeakG_ = (int32_t)sensor_->weightG();
  visitCat_ = 0;
  visitOpened_ = false;
  visitWhy_ = EvWhy::UNKNOWN;
}

void AccessStateMachine::endVisit() {
  if (!visitActive_) return;
  visitActive_ = false;
  if (!log_) return;
  uint32_t durS = (millis() - visitStartMs_) / 1000;
  log_->addVisit(visitCat_, visitPeakG_, durS, visitOpened_, visitWhy_);
}

// ------------------------------------------------------------------ comandos

void AccessStateMachine::manualOpen() {
  startRefill(cfg_.maxOpenS);
}

void AccessStateMachine::startRefill(uint32_t timeoutS) {
  manualOverride_ = true;
  manualCloseRequested_ = false;
  refillActive_ = true;
  refillStartMs_ = millis();
  refillTimeoutS_ = timeoutS > 0 ? timeoutS : cfg_.maxOpenS;
  door_->requestOpen();
  enterState(AccessState::ACCESS_OPEN);
}

void AccessStateMachine::manualClose() {
  manualOverride_ = true;
  if (platformEmpty()) {
    door_->requestClose();
    if (refillActive_ && log_) log_->addRefill((millis() - refillStartMs_) / 1000);
    refillActive_ = false;
    manualOverride_ = false;
    manualCloseRequested_ = false;
    enterState(AccessState::IDLE);
  } else {
    // RNF04: nao fecha com o animal na plataforma. Fica pendente, e o timer da
    // confirmacao comeca a contar AGORA (no original ele herdava um instante
    // antigo e o fechamento saia sem esperar - REVISAO.md item 6).
    manualCloseRequested_ = true;
    manualCloseSinceMs_ = millis();
  }
}

void AccessStateMachine::reset() {
  manualOverride_ = false;
  manualCloseRequested_ = false;
  refillActive_ = false;
  visitActive_ = false;
  doorFaultReported_ = false;
  sensorFaultReported_ = false;
  door_->reset();
  enterState(AccessState::IDLE);
}

// -------------------------------------------------------------------- update

void AccessStateMachine::update() {
  // Em calibracao de bancada quem manda no servo e o operador.
  if (door_->isCalibrating()) return;

  // Falha de sensor -> estado seguro, porta fica como esta.
  if (!sensor_->isHealthy()) {
    if (state_ != AccessState::SAFE_FAULT) {
      if (!sensorFaultReported_ && log_) {
        log_->addFault(EvErr::SENSOR);
        sensorFaultReported_ = true;
      }
      visitActive_ = false;  // a visita em curso perde o sentido
      enterState(AccessState::SAFE_FAULT);
    }
    return;
  }
  if (state_ == AccessState::SAFE_FAULT) {
    sensorFaultReported_ = false;
    enterState(AccessState::IDLE);
  }

  // Porta em falha: registra uma vez e para de decidir ate alguem rearmar.
  if (door_->isFault()) {
    if (!doorFaultReported_) {
      doorFaultReported_ = true;
      if (log_) log_->addFault(EvErr::DOOR_TIMEOUT);
    }
    return;
  }
  doorFaultReported_ = false;

  if (visitActive_) {
    int32_t w = (int32_t)sensor_->weightG();
    if (w > visitPeakG_) visitPeakG_ = w;
  }

  if (manualOverride_) {
    // Reabastecer tem prazo: passou do limite, fecha sozinho.
    if (refillActive_ && refillTimeoutS_ > 0
        && (millis() - refillStartMs_) / 1000 >= refillTimeoutS_) {
      manualClose();
      return;
    }
    if (manualCloseRequested_ && platformEmpty()
        && (millis() - manualCloseSinceMs_ >= cfg_.leaveMs)) {
      door_->requestClose();
      if (refillActive_ && log_) log_->addRefill((millis() - refillStartMs_) / 1000);
      refillActive_ = false;
      manualOverride_ = false;
      manualCloseRequested_ = false;
      enterState(AccessState::IDLE);
    } else if (manualCloseRequested_ && platformOccupied()) {
      manualCloseSinceMs_ = millis();  // animal voltou: recomeca a contagem
    }
    return;
  }

  switch (state_) {
    case AccessState::IDLE:
      if (platformOccupied()) {
        beginVisit();
        enterState(AccessState::STABILIZING);
      }
      break;

    case AccessState::STABILIZING:
      if (platformEmpty()) {
        endVisit();  // subiu e desceu antes de estabilizar
        enterState(AccessState::IDLE);
      } else if (sensor_->isStable() && (millis() - stateEnteredMs_ >= cfg_.confMs)) {
        enterState(AccessState::EVALUATING);
      }
      break;

    case AccessState::EVALUATING: {
      float w = sensor_->weightG();

      // Modo adaptacao: porta abre para qualquer um e a visita fica marcada
      // como amostra de peso.
      if (cfg_.learnMode) {
        visitCat_ = 0;
        visitOpened_ = true;
        visitWhy_ = EvWhy::LEARN;
        door_->requestOpen();
        enterState(AccessState::ACCESS_OPEN);
        break;
      }

      uint8_t cat = 0;
      EvWhy why = EvWhy::RULE;
      bool allowed = weightAllowed(w, cat, why);
      visitCat_ = cat;
      visitWhy_ = why;
      visitOpened_ = allowed;

      if (allowed) {
        door_->requestOpen();
        enterState(AccessState::ACCESS_OPEN);
      } else {
        enterState(AccessState::ACCESS_BLOCKED);
      }
      break;
    }

    case AccessState::ACCESS_OPEN:
    case AccessState::ACCESS_BLOCKED:
      if (platformEmpty()) enterState(AccessState::VACATE_CONFIRM);
      break;

    case AccessState::VACATE_CONFIRM:
      if (platformOccupied()) {
        enterState(door_->isOpen() || door_->isMoving() ? AccessState::ACCESS_OPEN
                                                        : AccessState::ACCESS_BLOCKED);
      } else if (millis() - stateEnteredMs_ >= cfg_.leaveMs) {
        if (door_->isOpen() || door_->isMoving()) door_->requestClose();
        endVisit();
        enterState(AccessState::IDLE);
      }
      break;

    case AccessState::SAFE_FAULT:
    default:
      break;
  }
}
