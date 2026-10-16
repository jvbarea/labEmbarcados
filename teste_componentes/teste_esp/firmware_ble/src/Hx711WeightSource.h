#pragma once
/*
 * Hx711WeightSource.h
 * Balanca real: modulo XFW-HX711 com 4 celulas de meia-ponte em ponte de
 * Wheatstone. Implementa a mesma interface que o mock, entao a
 * AccessStateMachine nao muda uma linha.
 *
 * A calibracao (offset de tara e fator de escala) NAO mora aqui: vem do
 * AppConfig, que a le da NVS. Assim o mesmo firmware serve a qualquer maquete,
 * e o app pode recalibrar sem recompilar.
 *
 * Diferenca essencial em relacao ao codigo herdado (REVISAO.md item 1):
 * isHealthy() e "faz tempo demais que nao chega amostra", NAO hx.is_ready().
 * O is_ready() so diz "tem amostra agora" e e falso na maior parte do tempo
 * entre conversoes - usar ele como saude derrubava a FSM em toda iteracao.
 *
 * Depende da biblioteca "HX711" (bogde), a mesma do teste de componente.
 */
#include <Arduino.h>
#include <HX711.h>
#include "IWeightSource.h"
#include "config.h"

class Hx711WeightSource : public IWeightSource {
 public:
  // calibFactor em contagens por grama; tareOffset em contagens cruas.
  // calibFactor = 0 significa "ainda nao calibrado": o peso sai como 0 e
  // isCalibrated() devolve false.
  void begin(float calibFactor, long tareOffset);

  void update() override;
  float weightG() const override { return filtradoG_; }
  bool isStable() const override { return estavel_; }
  bool isHealthy() const override;
  const char *sourceName() const override { return "hx711"; }

  // RF07. As duas bloqueiam ~2 s (media de HX711_AVG_SAMPLES leituras a
  // 10 SPS), por isso quem chama deve recusar com a porta em movimento.
  bool tare();
  bool calibrate(float massaConhecidaG);

  bool  isCalibrated() const { return fator_ != 0.0f; }
  float calibFactor() const { return fator_; }
  long  tareOffset() const { return offset_; }

  // Ultima leitura crua, para diagnostico pelo serial.
  long rawCounts() const { return ultimaCrua_; }

 private:
  HX711 hx_;

  float fator_ = 0.0f;   // contagens por grama
  long  offset_ = 0;     // contagens com a plataforma vazia

  float filtradoG_ = 0.0f;
  bool  filtroIniciado_ = false;
  long  ultimaCrua_ = 0;

  unsigned long ultimaAmostraMs_ = 0;

  float history_[WEIGHT_STABILITY_WINDOW] = {0};
  size_t historyIndex_ = 0;
  size_t historyCount_ = 0;
  bool estavel_ = false;

  float paraGramas(double cru) const;
  bool mediaCrua(int n, double &media);
  void pushHistory(float v);
  void avaliaEstabilidade();
  void reiniciaFiltro();
};
