#pragma once
// So o ESP32PWM, que e o que o DoorActuator usa. Guarda o ultimo duty e se o
// pino esta energizado, para o teste conferir o DOOR_HOLD_OPEN.
#include <Arduino.h>

namespace sim {
extern bool servoAttached;
extern uint32_t servoDuty;
}

class ESP32PWM {
 public:
  static void allocateTimer(int) {}
  bool attached() const { return sim::servoAttached; }
  void attachPin(int, double, int) { sim::servoAttached = true; }
  void detachPin(int) { sim::servoAttached = false; }
  void write(uint32_t duty) { sim::servoDuty = duty; }
};
