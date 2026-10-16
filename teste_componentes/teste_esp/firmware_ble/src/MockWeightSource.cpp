#include "MockWeightSource.h"

void MockWeightSource::begin() {
  currentG_ = targetG_ = rampStartG_ = 0.0f;
  rampStartMs_ = millis();
  lastSampleMs_ = millis();
  historyCount_ = historyIndex_ = 0;
  stable_ = false;
  healthy_ = true;
}

void MockWeightSource::setTargetWeightG(float targetG) {
  if (targetG < 0.0f) targetG = 0.0f;
  rampStartG_  = currentG_;
  targetG_     = targetG;
  rampStartMs_ = millis();
  stable_      = false;
  historyCount_ = historyIndex_ = 0;  // a janela de estabilidade recomeca
}

void MockWeightSource::setHealthy(bool healthy) {
  healthy_ = healthy;
  if (healthy) lastSampleMs_ = millis();  // nao nasce "atrasado" ao voltar
}

bool MockWeightSource::isHealthy() const {
  if (!healthy_) return false;
  return (millis() - lastSampleMs_) <= WEIGHT_HEALTH_TIMEOUT_MS;
}

void MockWeightSource::update() {
  if (!healthy_) return;  // sensor mudo: para de produzir amostras

  unsigned long now = millis();
  if (now - lastSampleMs_ < WEIGHT_SAMPLE_INTERVAL_MS) return;
  lastSampleMs_ = now;

  unsigned long elapsed = now - rampStartMs_;
  if (elapsed >= MOCK_RAMP_MS) {
    currentG_ = targetG_;
  } else {
    float k = (float)elapsed / (float)MOCK_RAMP_MS;
    currentG_ = rampStartG_ + (targetG_ - rampStartG_) * k;
  }

  pushHistory(currentG_);
  evaluateStability();
}

void MockWeightSource::pushHistory(float v) {
  history_[historyIndex_] = v;
  historyIndex_ = (historyIndex_ + 1) % WEIGHT_STABILITY_WINDOW;
  if (historyCount_ < WEIGHT_STABILITY_WINDOW) historyCount_++;
}

void MockWeightSource::evaluateStability() {
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
