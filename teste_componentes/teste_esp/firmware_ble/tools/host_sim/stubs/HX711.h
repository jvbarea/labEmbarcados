#pragma once
// O simulador compila com USE_HX711=0; isto so existe para o Hx711WeightSource.cpp
// compilar (o AppProtocol tem ponteiro para ele).
#include <Arduino.h>

class HX711 {
 public:
  void begin(int, int, int = 128) {}
  bool is_ready() { return false; }
  long read() { return 0; }
};
