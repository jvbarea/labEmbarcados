#include "DoorActuator.h"

const char *doorStateName(DoorState s) {
  switch (s) {
    case DoorState::CLOSED:      return "closed";
    case DoorState::OPENING:     return "opening";
    case DoorState::OPEN:        return "open";
    case DoorState::CLOSING:     return "closing";
    case DoorState::FAULT:       return "fault";
    case DoorState::CALIBRATING: return "calibrating";
    default:                     return "unknown";
  }
}

int doorDegToUs(int deg) {
  deg = constrain(deg, 0, 180);
  return map(deg, 0, 180, SERVO_US_AT_0DEG, SERVO_US_AT_180DEG);
}

int doorUsToDeg(int us) {
  us = constrain(us, SERVO_US_AT_0DEG, SERVO_US_AT_180DEG);
  return map(us, SERVO_US_AT_0DEG, SERVO_US_AT_180DEG, 0, 180);
}

void DoorActuator::begin(int closedDeg, int openDeg) {
  // A ESP32Servo exige timer alocado antes do attach. Faltava no original
  // (REVISAO.md, item 3).
  ESP32PWM::allocateTimer(0);
  servo_.setPeriodHertz(50);

#if USE_LIMIT_SWITCHES
  pinMode(PIN_LIMIT_OPEN, INPUT_PULLUP);
  pinMode(PIN_LIMIT_CLOSED, INPUT_PULLUP);
#endif

  setPositions(closedDeg, openDeg);

  // Homing: leva a porta ate a posicao de fechada, para partir de um estado
  // conhecido mesmo que alguem a tenha movido com a mao.
  currentUs_ = doorDegToUs(closedDeg_);
  startMovement(DoorState::CLOSING, currentUs_);
}

void DoorActuator::setPositions(int closedDeg, int openDeg) {
  closedDeg_ = constrain(closedDeg, 0, 180);
  openDeg_   = constrain(openDeg, 0, 180);
}

bool DoorActuator::limitOpenActive() const {
#if USE_LIMIT_SWITCHES
  return digitalRead(PIN_LIMIT_OPEN) == LIMIT_SWITCH_ACTIVE_LEVEL;
#else
  return false;
#endif
}

bool DoorActuator::limitClosedActive() const {
#if USE_LIMIT_SWITCHES
  return digitalRead(PIN_LIMIT_CLOSED) == LIMIT_SWITCH_ACTIVE_LEVEL;
#else
  return false;
#endif
}

bool DoorActuator::fcClosed() const {
#if USE_LIMIT_SWITCHES
  return limitClosedActive();
#else
  return state_ == DoorState::CLOSED;
#endif
}

bool DoorActuator::fcOpen() const {
#if USE_LIMIT_SWITCHES
  return limitOpenActive();
#else
  return state_ == DoorState::OPEN;
#endif
}

void DoorActuator::energize() {
  if (!servo_.attached()) servo_.attach(PIN_SERVO, SERVO_US_MIN, SERVO_US_MAX);
}

void DoorActuator::deenergize() {
  if (servo_.attached()) servo_.detach();  // RNF04: servo desenergizado parado
}

void DoorActuator::startMovement(DoorState movingState, int targetUs) {
  targetUs_ = constrain(targetUs, SERVO_US_MIN, SERVO_US_MAX);
  state_ = movingState;
  moveStartMs_ = millis();
  lastStepMs_ = millis();
  settling_ = false;
  energize();
  servo_.writeMicroseconds(currentUs_);
}

void DoorActuator::finishMovement(DoorState finalState) {
  deenergize();
  settling_ = false;
  state_ = finalState;
}

void DoorActuator::requestOpen() {
  if (state_ == DoorState::OPEN || state_ == DoorState::OPENING) return;
  if (state_ == DoorState::CALIBRATING) return;
  startMovement(DoorState::OPENING, doorDegToUs(openDeg_));
}

void DoorActuator::requestClose() {
  if (state_ == DoorState::CLOSED || state_ == DoorState::CLOSING) return;
  if (state_ == DoorState::CALIBRATING) return;
  startMovement(DoorState::CLOSING, doorDegToUs(closedDeg_));
}

void DoorActuator::reset() {
  startMovement(DoorState::CLOSING, doorDegToUs(closedDeg_));
}

// ----------------------------------------------------------- calibracao

void DoorActuator::enterCalibration() {
  state_ = DoorState::CALIBRATING;
  targetUs_ = currentUs_;
  settling_ = false;
  lastStepMs_ = millis();
  energize();
  servo_.writeMicroseconds(currentUs_);
}

void DoorActuator::exitCalibration() {
  if (state_ != DoorState::CALIBRATING) return;
  startMovement(DoorState::CLOSING, doorDegToUs(closedDeg_));
}

void DoorActuator::jogToDeg(int deg) {
  if (state_ != DoorState::CALIBRATING) return;
  targetUs_ = doorDegToUs(deg);
}

// --------------------------------------------------------------- update

bool DoorActuator::stepTowardTarget() {
  if (currentUs_ == targetUs_) return true;
  if (millis() - lastStepMs_ < SERVO_STEP_MS) return false;
  lastStepMs_ = millis();

  int falta = targetUs_ - currentUs_;
  if (abs(falta) <= SERVO_STEP_US) currentUs_ = targetUs_;
  else                             currentUs_ += (falta > 0) ? SERVO_STEP_US : -SERVO_STEP_US;

  servo_.writeMicroseconds(currentUs_);
  return currentUs_ == targetUs_;
}

void DoorActuator::update() {
  // Em calibracao o servo fica energizado e segue o alvo, sem timeout nem
  // transicao automatica: quem manda e o operador.
  if (state_ == DoorState::CALIBRATING) {
    stepTowardTarget();
    return;
  }

  if (state_ != DoorState::OPENING && state_ != DoorState::CLOSING) return;

  const bool opening = (state_ == DoorState::OPENING);

  // Guarda de tempo: vale em qualquer fase do movimento.
  if (millis() - moveStartMs_ > DOOR_MOVE_TIMEOUT_MS) {
    finishMovement(DoorState::FAULT);
    return;
  }

  // A chave de fim de curso, quando existe, tem prioridade sobre a rampa.
  if ((opening && limitOpenActive()) || (!opening && limitClosedActive())) {
    finishMovement(opening ? DoorState::OPEN : DoorState::CLOSED);
    return;
  }

  if (settling_) {
    if (millis() - settleStartMs_ >= SERVO_SETTLE_MS) {
      finishMovement(opening ? DoorState::OPEN : DoorState::CLOSED);
    }
    return;
  }

  if (stepTowardTarget()) {
    settling_ = true;
    settleStartMs_ = millis();
  }
}
