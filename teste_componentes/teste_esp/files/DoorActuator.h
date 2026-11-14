#pragma once
/*
 * DoorActuator.h
 * Controle da porta do compartimento de ração, acionada por servomotor de
 * rotação contínua (360°) acoplado a um mecanismo pinhão-cremalheira, com
 * realimentação de posição por chaves fim de curso.
 *
 * Atende RF04, RF06, RNF04 (fechamento seguro e servo desenergizado ao fim
 * do movimento) e contribui para RNF02 (abertura em <3s).
 *
 * Depende da biblioteca "ESP32Servo".
 */
#include <Arduino.h>
#include <ESP32Servo.h>

enum class DoorState : uint8_t {
  CLOSED,
  OPENING,
  OPEN,
  CLOSING,
  FAULT, // não atingiu a chave fim de curso dentro do timeout (RNF03: mantém posição)
};

class DoorActuator {
 public:
  void begin();

  // Não-bloqueante: deve ser chamada a cada iteração do loop() para
  // monitorar as chaves fim de curso e aplicar o timeout de movimento.
  void update();

  // Solicita abertura/fechamento. Ignorado se já estiver no estado pedido
  // ou em movimento para lá.
  void requestOpen();
  void requestClose();

  DoorState state() const { return state_; }
  bool isOpen()  const { return state_ == DoorState::OPEN; }
  bool isClosed() const { return state_ == DoorState::CLOSED; }
  bool isMoving() const { return state_ == DoorState::OPENING || state_ == DoorState::CLOSING; }

 private:
  Servo servo_;
  DoorState state_ = DoorState::CLOSED;
  unsigned long moveStartMs_ = 0;

  bool limitOpenActive() const;
  bool limitClosedActive() const;

  void startMovement(DoorState movingState, int pulseUs);
  void stopServo(); // envia pulso neutro e desacopla (desenergiza) o servo
};
