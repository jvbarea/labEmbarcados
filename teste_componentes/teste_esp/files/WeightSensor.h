#pragma once
/*
 * WeightSensor.h
 * Camada de aquisição: leitura do HX711 (4 células de carga em ponte de
 * Wheatstone), filtro digital (média móvel exponencial) e detecção de
 * estabilidade. Atende RF01, RF07 e RNF01.
 *
 * Depende da biblioteca "HX711" (bogde/HX711), disponível no Library Manager
 * do Arduino IDE.
 */
#include <Arduino.h>
#include <HX711.h>
#include "config.h"
#include "Storage.h"

class WeightSensor {
 public:
  void begin(const AppConfig &cfg);

  // Deve ser chamado periodicamente no loop(); respeita WEIGHT_SAMPLE_INTERVAL_MS
  // internamente e não bloqueia se não houver amostra nova disponível.
  void update();

  float filteredWeightG() const { return filteredWeightG_; }
  bool  isStable() const { return stable_; }
  bool  isReady() const { return hx_.is_ready(); } // false indica falha de sensor (RNF03)

  // RF07: tara e calibração com massa-padrão conhecida.
  void tare();
  // knownMassG: massa de referência colocada na plataforma no momento da chamada.
  void calibrateWithKnownMass(float knownMassG);

  long   tareOffset() const { return tareOffset_; }
  float  calibrationFactor() const { return calibrationFactor_; }

 private:
  HX711 hx_;
  unsigned long lastSampleMs_ = 0;

  float filteredWeightG_ = 0.0f;
  bool  filterInitialized_ = false;

  float  history_[WEIGHT_STABILITY_WINDOW] = {0};
  size_t historyIndex_ = 0;
  size_t historyCount_ = 0;
  bool   stable_ = false;

  long  tareOffset_ = 0;
  float calibrationFactor_ = HX711_CALIBRATION_DEFAULT;

  void pushHistory(float value);
  void evaluateStability();
};
