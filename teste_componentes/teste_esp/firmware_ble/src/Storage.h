#pragma once
/*
 * Storage.h
 * Persistencia da configuracao em NVS (Preferences).
 *
 * Os nomes dos campos seguem o protocolo Feed Cat v2 (plano/01-app.md secao 7):
 * confMs (tempo estavel antes de decidir), leaveMs (tempo vazio antes de
 * fechar), maxOpenS (limite do modo reabastecer).
 *
 * O "profile" e um blob opaco: o app guarda ali nomes, emoji e cores dos gatos,
 * que o firmware nunca interpreta. Serve para um celular novo recuperar o
 * cadastro. Fica fora do AppConfig para nao encarecer as copias da struct.
 */
#include <Arduino.h>
#include "config.h"

/** Um gato como o dispositivo o conhece: faixa de peso e regra. */
struct DeviceCat {
  uint8_t k = 0;        // 1 ou 2
  int32_t minG = 0;
  int32_t maxG = 0;
  bool    allow = true; // false = "deny"
};

struct AppConfig {
  uint32_t confMs   = DEFAULT_CONF_MS;
  uint32_t leaveMs  = DEFAULT_LEAVE_MS;
  uint32_t maxOpenS = DEFAULT_MAX_OPEN_S;

  // Posicoes da porta em graus: o mecanismo usa 0 (fechada) e 90 (aberta).
  int doorClosedDeg = DEFAULT_DOOR_CLOSED_DEG;
  int doorOpenDeg   = DEFAULT_DOOR_OPEN_DEG;

  // Calibracao do SERVO: pulsos em que o eixo esta fisicamente em 0 e em 90
  // graus. Medidos em bancada com transferidor (comandos set0 e set90). Os
  // padroes sao a faixa nominal da familia, nao uma medida do nosso servo.
  int servoUs0  = DEFAULT_SERVO_US_AT_0DEG;
  int servoUs90 = DEFAULT_SERVO_US_AT_90DEG;

  bool learnMode = false;   // modo "learn" do protocolo v2

  // Gatos cadastrados. Na Etapa 2 o firmware guarda e devolve, mas ainda nao
  // decide por eles (ver REVISAO.md, secao Escopo).
  DeviceCat cats[MAX_CATS];
  uint8_t   catCount = 0;

  // Calibracao da balanca. fator = contagens por grama; offset = contagens com
  // a plataforma vazia. fator 0 = ainda nao calibrada.
  float calibFactor = 0.0f;
  long  tareOffset  = 0;

  // Muda a cada gravacao; o app compara para saber se precisa reenviar.
  uint32_t cfgV = 0;

  // Janela de peso do modo de bancada, quando nao ha gatos cadastrados.
  float benchMinG = DEFAULT_WEIGHT_MIN_G;
  float benchMaxG = DEFAULT_WEIGHT_MAX_G;
};

class Storage {
 public:
  AppConfig loadAppConfig();
  void saveAppConfig(AppConfig &cfg);  // incrementa cfgV e grava

  String loadProfile();
  void saveProfile(const String &blob);

  /*
   * O teste de componente em teste_componentes/hx711 grava a calibracao no
   * namespace "hx711-cal". Se este firmware ainda nao tem calibracao propria,
   * aproveita aquela - evita recalibrar a maquete so por trocar de firmware.
   * Devolve true se importou alguma coisa.
   */
  bool importarCalibracaoDaBancada(AppConfig &cfg);
};
