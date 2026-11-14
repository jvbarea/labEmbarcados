#pragma once
/*
 * IWeightSource.h
 * Interface da fonte de peso. Existe para que a AccessStateMachine nao saiba
 * se por tras ha um HX711 real ou um mock - e a mesma logica roda nos dois.
 *
 * Diferenca importante em relacao ao codigo original: aqui existe isHealthy(),
 * separado de "tem amostra nova". No original a FSM usava hx.is_ready() como
 * indicador de saude, e como ele e falso entre conversoes a maquina caia em
 * SAFE_FAULT em quase toda iteracao (ver REVISAO.md, item 1).
 */
#include <Arduino.h>

class IWeightSource {
 public:
  virtual ~IWeightSource() {}

  // Nao bloqueante; respeita o proprio periodo de amostragem.
  virtual void update() = 0;

  virtual float weightG() const = 0;

  // true quando as ultimas amostras variaram menos que a tolerancia.
  virtual bool isStable() const = 0;

  // false quando ha tempo demais sem amostra valida (sensor mudo / desligado).
  virtual bool isHealthy() const = 0;

  virtual const char *sourceName() const = 0;
};
