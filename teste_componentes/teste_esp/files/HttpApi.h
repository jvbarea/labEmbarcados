#pragma once
/*
 * HttpApi.h
 * Servidor HTTP (fase 2, na rede Wi-Fi local) usado pelo aplicativo para:
 *  - GET  /api/status   -> peso atual, estado da porta, estado da máquina de acesso
 *  - GET  /api/config   -> janela de peso e temporizadores atuais
 *  - POST /api/config   -> atualiza janela de peso e temporizadores (RF05)
 *  - POST /api/door     -> abre/fecha a porta manualmente (RF06)
 *  - POST /api/calibrate-> tara e calibração com massa-padrão (RF07)
 *  - GET  /api/events   -> histórico de eventos (RF11)
 *
 * Corpo das requisições POST em JSON. Mantido propositalmente genérico,
 * conforme solicitado: os campos podem ser estendidos depois sem quebrar
 * o formato básico {"campo": valor, ...}.
 *
 * Depende de WebServer (embutida no core ESP32) e ArduinoJson.
 */
#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "config.h"
#include "Storage.h"
#include "WeightSensor.h"
#include "DoorActuator.h"
#include "AccessStateMachine.h"
#include "EventLog.h"

class HttpApi {
 public:
  void begin(Storage *storage, WeightSensor *sensor, DoorActuator *door,
             AccessStateMachine *stateMachine, EventLog *log);

  // Deve ser chamada a cada iteração do loop() quando o Wi-Fi estiver conectado.
  void handleClient();

 private:
  WebServer server_{HTTP_SERVER_PORT};

  Storage *storage_ = nullptr;
  WeightSensor *sensor_ = nullptr;
  DoorActuator *door_ = nullptr;
  AccessStateMachine *stateMachine_ = nullptr;
  EventLog *log_ = nullptr;

  void handleGetStatus();
  void handleGetConfig();
  void handlePostConfig();
  void handlePostDoor();
  void handlePostCalibrate();
  void handleGetEvents();
  void handleNotFound();

  void sendJson(int code, const String &json);
  void sendError(int code, const String &message);
};
