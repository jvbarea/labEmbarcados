#include "AccessStateMachine.h"
#include "config.h"

void AccessStateMachine::begin(WeightSensor *sensor, DoorActuator *door, EventLog *log, const AppConfig &cfg) {
  sensor_ = sensor;
  door_   = door;
  log_    = log;
  cfg_    = cfg;
  enterState(AccessState::IDLE);
}

void AccessStateMachine::applyConfig(const AppConfig &cfg) {
  cfg_ = cfg;
}

void AccessStateMachine::enterState(AccessState newState) {
  state_ = newState;
  stateEnteredMs_ = millis();
}

bool AccessStateMachine::platformOccupied() const {
  return sensor_->filteredWeightG() > WEIGHT_EMPTY_THRESHOLD_G;
}

bool AccessStateMachine::platformEmpty() const {
  return !platformOccupied();
}

bool AccessStateMachine::weightWithinWindow(float w) const {
  return w >= cfg_.weightMinG && w <= cfg_.weightMaxG;
}

void AccessStateMachine::manualOpen() {
  door_->requestOpen();
  manualOverride_ = true;
  if (log_) log_->add(EventType::DOOR_OPENED_MANUAL, sensor_->filteredWeightG());
}

void AccessStateMachine::manualClose() {
  manualOverride_ = true;
  if (platformEmpty()) {
    door_->requestClose();
    if (log_) log_->add(EventType::DOOR_CLOSED_MANUAL, sensor_->filteredWeightG());
    manualOverride_ = false;
    enterState(AccessState::IDLE);
  } else {
    // RNF04: não fecha com o animal na plataforma. Fica pendente e será
    // concluído por update() assim que a plataforma for confirmadamente
    // esvaziada.
    manualCloseRequested_ = true;
  }
}

void AccessStateMachine::update() {
  // RNF03: falha de sensor -> estado seguro, mantém a porta como está.
  if (!sensor_->isReady()) {
    if (state_ != AccessState::SAFE_FAULT && log_) {
      log_->add(EventType::FAULT_SAFE_STATE, sensor_->filteredWeightG());
    }
    enterState(AccessState::SAFE_FAULT);
    return;
  }
  if (state_ == AccessState::SAFE_FAULT) {
    // Sensor voltou a responder: reavalia a partir do estado inicial.
    enterState(AccessState::IDLE);
  }

  if (manualOverride_) {
    if (manualCloseRequested_ && platformEmpty()
        && (millis() - stateEnteredMs_ >= cfg_.vacateConfirmMs)) {
      door_->requestClose();
      if (log_) log_->add(EventType::DOOR_CLOSED_MANUAL, sensor_->filteredWeightG());
      manualOverride_ = false;
      manualCloseRequested_ = false;
      enterState(AccessState::IDLE);
    }
    return; // enquanto em override manual, não roda a lógica automática
  }

  switch (state_) {
    case AccessState::IDLE:
      if (platformOccupied()) enterState(AccessState::STABILIZING);
      break;

    case AccessState::STABILIZING:
      if (platformEmpty()) {
        enterState(AccessState::IDLE);
      } else if (sensor_->isStable() && (millis() - stateEnteredMs_ >= cfg_.stabilizeMs)) {
        enterState(AccessState::EVALUATING);
      }
      break;

    case AccessState::EVALUATING: {
      float w = sensor_->filteredWeightG();
      if (weightWithinWindow(w)) {
        door_->requestOpen();
        if (log_) log_->add(EventType::ACCESS_GRANTED, w);
        enterState(AccessState::ACCESS_OPEN);
      } else {
        if (log_) log_->add(EventType::ACCESS_BLOCKED, w);
        enterState(AccessState::ACCESS_BLOCKED);
      }
      break;
    }

    case AccessState::ACCESS_OPEN:
      if (platformEmpty()) enterState(AccessState::VACATE_CONFIRM);
      break;

    case AccessState::ACCESS_BLOCKED:
      if (platformEmpty()) enterState(AccessState::VACATE_CONFIRM);
      break;

    case AccessState::VACATE_CONFIRM:
      if (platformOccupied()) {
        // Animal voltou antes da confirmação: retoma o estado anterior.
        enterState(door_->isOpen() ? AccessState::ACCESS_OPEN : AccessState::ACCESS_BLOCKED);
      } else if (millis() - stateEnteredMs_ >= cfg_.vacateConfirmMs) {
        if (door_->isOpen() || door_->isMoving()) {
          door_->requestClose(); // RF04: só fecha com a plataforma confirmadamente desocupada
        }
        enterState(AccessState::IDLE);
      }
      break;

    case AccessState::SAFE_FAULT:
    default:
      break;
  }
}
