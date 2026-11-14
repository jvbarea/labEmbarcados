#pragma once
/*
 * AccessStateMachine.h
 * Decide, a partir do peso, se libera o acesso a racao.
 *
 * Nao toca em hardware: recebe IWeightSource e DoorActuator por ponteiro, o
 * que permite rodar a mesma logica com a balanca real ou com o mock.
 *
 * As regras sao as mesmas do simulador do app (app/src/device/MockLink.ts,
 * metodo updateVisit, e app/src/domain/rules.ts), que funciona como a
 * especificacao do comportamento do comedouro:
 *
 *  - VISITA: comeca quando o peso passa de PRESENCE_THRESHOLD_G. A decisao sai
 *    depois de confMs com alguem em cima E a balanca estavel; so entao a visita
 *    conta. Termina quando a plataforma fica vazia por leaveMs, e o evento e
 *    gravado nesse momento, com a duracao ate a saida (sem a espera).
 *    Encostar e sair antes da decisao nao gera evento.
 *  - DECISAO: identifica o gato pela faixa de peso. Desconhecido = nega. Com
 *    liberacao valida para ele = abre (e consome uma visita, se for por visitas).
 *    Senao vale a regra do cadastro (allow / deny).
 *  - MODO ADAPTACAO: abre para qualquer gato e registra a visita com why=learn.
 *    Ver LEARN_DOOR_STAYS_OPEN em config.h para o que acontece entre visitas.
 *  - REABASTECER: porta aberta ate o prazo; as visitas continuam sendo
 *    registradas, mas a porta so fecha no fim do prazo.
 *  - RNF04: a porta nunca fecha com peso na plataforma. Todo pedido de fechar
 *    com animal em cima fica pendente ate a plataforma ficar vazia por leaveMs.
 *  - FALHA DA PORTA: as visitas continuam sendo registradas, sem mover a porta.
 *    O comando "close" rearma, como no simulador.
 *  - FALHA DO SENSOR: estado seguro, a porta fica como esta, a visita em curso
 *    e descartada.
 *
 * Sem nenhum gato cadastrado vale a janela de bancada (benchMinG..benchMaxG),
 * que e como o firmware era testado antes do app existir.
 */
#include <Arduino.h>
#include "IWeightSource.h"
#include "DoorActuator.h"
#include "Storage.h"
#include "EventLog.h"

/**
 * Liberacao temporaria de um gato bloqueado, por prazo (untilS, epoch) ou por
 * numero de visitas. Fica so na RAM, de proposito (docs/08 secao 4.1): sem
 * relogio depois de uma queda de energia, o seguro e o gato continuar bloqueado.
 */
struct GrantState {
  bool     active = false;
  uint8_t  cat = 0;
  bool     hasUntil = false;
  uint32_t untilS = 0;
  bool     hasVisits = false;
  int32_t  visits = 0;
};

struct Decision {
  uint8_t cat;    // 0 = ninguem reconhecido
  bool    allowed;
  EvWhy   why;
};

enum class AccessState : uint8_t {
  IDLE,        // plataforma vazia
  CONFIRMING,  // alguem subiu; esperando confMs e a balanca estabilizar
  VISIT,       // decidido: porta aberta ou negada
  LEAVING,     // plataforma vazia, esperando leaveMs para encerrar a visita
  SAFE_FAULT,  // sensor sem resposta
};

const char *accessStateName(AccessState s);

class AccessStateMachine {
 public:
  void begin(IWeightSource *sensor, DoorActuator *door, EventLog *log, const AppConfig &cfg);
  void update();

  // Aplica configuracao nova. Se o modo mudou, faz com a porta o que o modo pede.
  void applyConfig(const AppConfig &cfg);
  const AppConfig &config() const { return cfg_; }

  // --- comandos ---
  // Reabastecer: abre e fecha sozinha depois de timeoutS (0 = maxOpenS).
  // false com a porta em falha ou em calibracao.
  bool startRefill(uint32_t timeoutS);
  void manualOpen() { startRefill(cfg_.maxOpenS); }
  // Fechar: encerra o reabastecer. Com a porta em falha, rearma. Com animal na
  // plataforma, fica pendente (RNF04).
  void requestClose();
  // Rearme de bancada: limpa falhas, visita em curso e pendencias.
  void reset();

  // Liberacao temporaria: nullptr se aceitou, ou o codigo de erro do protocolo.
  const char *grant(uint8_t cat, bool hasUntil, uint32_t untilS, bool hasVisits, int32_t visits);
  void revoke(uint8_t cat);
  const GrantState &grantState() const { return grant_; }

  // Gato reconhecido pelo peso atual: 0 quando nao bate com nenhum cadastro.
  uint8_t currentCat() const;

  AccessState state() const { return state_; }
  bool refillActive() const { return refillActive_; }
  // Segundos que faltam para o reabastecer fechar sozinho (0 fora dele). Vai no
  // "st" como refillS, para o app mostrar a contagem mesmo depois de reabrir.
  uint32_t refillLeftS() const;
  bool closePending() const { return closePending_; }

 private:
  IWeightSource *sensor_ = nullptr;
  DoorActuator  *door_   = nullptr;
  EventLog      *log_    = nullptr;

  AppConfig cfg_;
  bool initialized_ = false;
  AccessState state_ = AccessState::IDLE;

  GrantState grant_;

  // Reabastecer
  bool refillActive_ = false;
  unsigned long refillStartMs_ = 0;
  unsigned long refillTimeoutMs_ = 0;

  // Fechamento pedido com animal na plataforma (RNF04)
  bool closePending_ = false;
  bool emptyTracking_ = false;
  unsigned long emptySinceMs_ = 0;

  // Visita em andamento (mesmos campos do MockLink)
  bool onPlatform_ = false;
  bool leaving_ = false;
  bool decided_ = false;
  unsigned long aboveSinceMs_ = 0;
  unsigned long belowSinceMs_ = 0;
  unsigned long visitStartMs_ = 0;
  float visitWeightG_ = 0;  // maior leitura ESTAVEL da visita: tranco de pulo nao conta
  uint8_t visitCat_ = 0;
  bool visitOpened_ = false;
  EvWhy visitWhy_ = EvWhy::UNKNOWN;

  bool doorFaultLogged_ = false;

  static bool present(float w) { return w >= PRESENCE_THRESHOLD_G; }
  uint8_t identify(float w) const;
  bool catAllowed(uint8_t cat) const;
  bool catRegistered(uint8_t cat) const;
  bool grantApplies(uint8_t cat, uint32_t nowS) const;
  Decision decide(float w) const;
  void consumeGrant();
  void expireGrant();

  void updateVisit(unsigned long now, float w);
  void resetVisit();
  bool openDoor();                      // false se a porta nao pode abrir (falha)
  void closeWhenSafe();                 // fecha agora ou deixa pendente (RNF04)
  bool shouldCloseAfterVisit() const;
  void endRefill(bool logEvent);
  void onModeChanged();
  void setState(AccessState s);
};
