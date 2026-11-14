#include "Hx711WeightSource.h"

void Hx711WeightSource::begin(float calibFactor, long tareOffset) {
  hx_.begin(PIN_HX711_DT, PIN_HX711_SCK);  // ganho 128 no canal A, o padrao

  fator_  = calibFactor;
  offset_ = tareOffset;

  reiniciaFiltro();

  // Nasce "saudavel": sem isto a FSM entraria em falha no primeiro loop,
  // antes de a primeira conversao do HX711 ficar pronta.
  ultimaAmostraMs_ = millis();
}

void Hx711WeightSource::reiniciaFiltro() {
  filtroIniciado_ = false;
  historyCount_ = 0;
  historyIndex_ = 0;
  estavel_ = false;
}

float Hx711WeightSource::paraGramas(double cru) const {
  if (fator_ == 0.0f) return 0.0f;
  return (float)((cru - (double)offset_) / (double)fator_);
}

bool Hx711WeightSource::isHealthy() const {
  return (millis() - ultimaAmostraMs_) <= WEIGHT_HEALTH_TIMEOUT_MS;
}

void Hx711WeightSource::update() {
  // is_ready() aqui e so "chegou amostra nova", que e o uso correto dele.
  // Quando nao ha, simplesmente nao se faz nada - e isHealthy() cuida de
  // detectar o caso em que ele para de chegar de vez.
  if (!hx_.is_ready()) return;

  ultimaCrua_ = hx_.read();
  ultimaAmostraMs_ = millis();

  float g = paraGramas((double)ultimaCrua_);

  if (!filtroIniciado_) {
    filtradoG_ = g;
    filtroIniciado_ = true;
  } else {
    filtradoG_ = WEIGHT_FILTER_ALPHA * g + (1.0f - WEIGHT_FILTER_ALPHA) * filtradoG_;
  }

  pushHistory(filtradoG_);
  avaliaEstabilidade();
}

void Hx711WeightSource::pushHistory(float v) {
  history_[historyIndex_] = v;
  historyIndex_ = (historyIndex_ + 1) % WEIGHT_STABILITY_WINDOW;
  if (historyCount_ < WEIGHT_STABILITY_WINDOW) historyCount_++;
}

void Hx711WeightSource::avaliaEstabilidade() {
  if (historyCount_ < WEIGHT_STABILITY_WINDOW) {
    estavel_ = false;
    return;
  }
  float minV = history_[0], maxV = history_[0];
  for (size_t i = 1; i < WEIGHT_STABILITY_WINDOW; i++) {
    if (history_[i] < minV) minV = history_[i];
    if (history_[i] > maxV) maxV = history_[i];
  }
  estavel_ = (maxV - minV) <= WEIGHT_STABILITY_TOLERANCE_G;
}

// Media de n leituras cruas. Bloqueia: a 10 SPS, 20 leituras levam ~2 s.
bool Hx711WeightSource::mediaCrua(int n, double &media) {
  double soma = 0;
  for (int i = 0; i < n; i++) {
    uint32_t t0 = millis();
    while (!hx_.is_ready()) {
      if (millis() - t0 > 500) return false;  // modulo mudo
      delay(1);
    }
    soma += (double)hx_.read();
  }
  media = soma / n;
  ultimaAmostraMs_ = millis();
  return true;
}

bool Hx711WeightSource::tare() {
  double media;
  if (!mediaCrua(HX711_AVG_SAMPLES, media)) return false;

  offset_ = (long)llround(media);
  reiniciaFiltro();
  return true;
}

bool Hx711WeightSource::calibrate(float massaConhecidaG) {
  if (massaConhecidaG <= 0.0f) return false;

  double media;
  if (!mediaCrua(HX711_AVG_SAMPLES, media)) return false;

  double delta = media - (double)offset_;
  // Sem variacao nao ha o que calibrar: ou a massa nao esta na plataforma, ou
  // a ponte nao esta respondendo.
  if (fabs(delta) < 100.0) return false;

  fator_ = (float)(delta / (double)massaConhecidaG);
  reiniciaFiltro();
  return true;
}
