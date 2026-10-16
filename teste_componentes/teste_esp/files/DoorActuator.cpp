#include "DoorActuator.h"
#include "config.h"

void DoorActuator::begin() {
  pinMode(PIN_LIMIT_OPEN, INPUT_PULLUP);
  pinMode(PIN_LIMIT_CLOSED, INPUT_PULLUP);

  // Servo começa desacoplado (desenergizado); só é acoplado durante o movimento.
  state_ = limitClosedActive() ? DoorState::CLOSED : DoorState::FAULT;
}

bool DoorActuator::limitOpenActive() const {
  return digitalRead(PIN_LIMIT_OPEN) == LIMIT_SWITCH_ACTIVE_LEVEL;
}

bool DoorActuator::limitClosedActive() const {
  return digitalRead(PIN_LIMIT_CLOSED) == LIMIT_SWITCH_ACTIVE_LEVEL;
}

void DoorActuator::startMovement(DoorState movingState, int pulseUs) {
  if (!servo_.attached()) {
    servo_.attach(PIN_SERVO);
  }
  servo_.writeMicroseconds(pulseUs);
  state_ = movingState;
  moveStartMs_ = millis();
}

void DoorActuator::stopServo() {
  servo_.writeMicroseconds(SERVO_US_STOP);
  delay(50); // curto, apenas para garantir que o pulso neutro seja enviado antes de desacoplar
  servo_.detach(); // RNF04: servo desenergizado ao fim do movimento
}

void DoorActuator::requestOpen() {
  if (state_ == DoorState::OPEN || state_ == DoorState::OPENING) return;
  startMovement(DoorState::OPENING, SERVO_US_OPEN_DIR);
}

void DoorActuator::requestClose() {
  if (state_ == DoorState::CLOSED || state_ == DoorState::CLOSING) return;
  startMovement(DoorState::CLOSING, SERVO_US_CLOSE_DIR);
}

void DoorActuator::update() {
  switch (state_) {
    case DoorState::OPENING:
      if (limitOpenActive()) {
        stopServo();
        state_ = DoorState::OPEN;
      } else if (millis() - moveStartMs_ > DOOR_MOVE_TIMEOUT_MS) {
        // RNF03: falha ao atingir a posição esperada -> mantém posição atual,
        // não força novo movimento sozinho.
        stopServo();
        state_ = DoorState::FAULT;
      }
      break;

    case DoorState::CLOSING:
      if (limitClosedActive()) {
        stopServo();
        state_ = DoorState::CLOSED;
      } else if (millis() - moveStartMs_ > DOOR_MOVE_TIMEOUT_MS) {
        stopServo();
        state_ = DoorState::FAULT;
      }
      break;

    case DoorState::OPEN:
    case DoorState::CLOSED:
    case DoorState::FAULT:
    default:
      // Repouso: servo já está desacoplado (não energizado).
      break;
  }
}
