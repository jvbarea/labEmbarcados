#include "WeightSensor.h"
#include "config.h"

void WeightSensor::begin(const AppConfig &cfg) {
  hx_.begin(PIN_HX711_DT, PIN_HX711_SCK);

  tareOffset_        = cfg.tareOffset;
  calibrationFactor_ = cfg.calibrationFactor;

  if (calibrationFactor_ == 0.0f) calibrationFactor_ = HX711_CALIBRATION_DEFAULT;

  hx_.set_scale(calibrationFactor_);
  hx_.set_offset(tareOffset_);

  filterInitialized_ = false;
  historyCount_ = 0;
  historyIndex_ = 0;
  stable_ = false;
}

void WeightSensor::update() {
  unsigned long now = millis();
  if (now - lastSampleMs_ < WEIGHT_SAMPLE_INTERVAL_MS) return;
  lastSampleMs_ = now;

  if (!hx_.is_ready()) {
    // RNF03: falha do sensor. Não atualiza o peso filtrado; quem consome
    // isReady()/isStable() deve tratar isso como "manter estado seguro".
    stable_ = false;
    return;
  }

  // get_units(1): uma leitura bruta convertida por scale/offset já configurados.
  float rawGrams = hx_.get_units(1);

  if (!filterInitialized_) {
    filteredWeightG_ = rawGrams;
    filterInitialized_ = true;
  } else {
    filteredWeightG_ = WEIGHT_FILTER_ALPHA * rawGrams
                      + (1.0f - WEIGHT_FILTER_ALPHA) * filteredWeightG_;
  }

  pushHistory(filteredWeightG_);
  evaluateStability();
}

void WeightSensor::pushHistory(float value) {
  history_[historyIndex_] = value;
  historyIndex_ = (historyIndex_ + 1) % WEIGHT_STABILITY_WINDOW;
  if (historyCount_ < WEIGHT_STABILITY_WINDOW) historyCount_++;
}

void WeightSensor::evaluateStability() {
  if (historyCount_ < WEIGHT_STABILITY_WINDOW) {
    stable_ = false;
    return;
  }
  float minV = history_[0], maxV = history_[0];
  for (size_t i = 1; i < WEIGHT_STABILITY_WINDOW; i++) {
    if (history_[i] < minV) minV = history_[i];
    if (history_[i] > maxV) maxV = history_[i];
  }
  stable_ = (maxV - minV) <= WEIGHT_STABILITY_TOLERANCE_G;
}

void WeightSensor::tare() {
  hx_.tare();
  tareOffset_ = hx_.get_offset();
  filterInitialized_ = false; // força reinicializar o filtro com a nova referência
  historyCount_ = 0;
}

void WeightSensor::calibrateWithKnownMass(float knownMassG) {
  if (knownMassG <= 0.0f) return;

  // Leitura bruta (sem escala) já com o offset de tara atual removido.
  long rawValue = hx_.read_average(10) - tareOffset_;
  if (rawValue == 0) return;

  calibrationFactor_ = (float)rawValue / knownMassG;
  hx_.set_scale(calibrationFactor_);
}
