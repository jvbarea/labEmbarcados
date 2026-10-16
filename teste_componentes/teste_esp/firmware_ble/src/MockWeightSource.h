#pragma once
/*
 * MockWeightSource.h
 * Balanca simulada: o peso e definido por comando (serial ou BLE) e converge
 * para o alvo em rampa, para que a deteccao de estabilidade da FSM seja
 * exercitada de verdade em vez de pular direto para o valor final.
 *
 * setHealthy(false) simula sensor mudo, para testar o estado SAFE_FAULT sem
 * precisar arrancar fio nenhum.
 */
#include <Arduino.h>
#include "IWeightSource.h"
#include "config.h"

class MockWeightSource : public IWeightSource {
 public:
  void begin();

  void update() override;
  float weightG() const override { return currentG_; }
  bool isStable() const override { return stable_; }
  bool isHealthy() const override;
  const char *sourceName() const override { return "mock"; }

  // "Coloca o gato na balanca": o peso caminha ate targetG em MOCK_RAMP_MS.
  void setTargetWeightG(float targetG);
  float targetWeightG() const { return targetG_; }

  void setHealthy(bool healthy);

 private:
  float currentG_ = 0.0f;
  float targetG_  = 0.0f;
  float rampStartG_ = 0.0f;
  unsigned long rampStartMs_ = 0;

  unsigned long lastSampleMs_ = 0;
  bool healthy_ = true;

  float history_[WEIGHT_STABILITY_WINDOW] = {0};
  size_t historyIndex_ = 0;
  size_t historyCount_ = 0;
  bool stable_ = false;

  void pushHistory(float v);
  void evaluateStability();
};
