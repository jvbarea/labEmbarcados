#pragma once
/*
 * BleControl.h
 * Canal BLE de CONTROLE do alimentador (nao mais so provisionamento de Wi-Fi).
 *
 * Usa o Nordic UART Service, que o nRF Connect reconhece de cara:
 *   RX (write)  app -> ESP : uma mensagem JSON por escrita
 *   TX (notify) ESP -> app : status, acks e eventos
 *
 * Regra de concorrencia: o callback do BLE roda em outra task e por isso SO
 * ENFILEIRA. Quem interpreta e age e o loop(). No codigo original o callback
 * escrevia direto em Strings lidas pelo loop, sem protecao (REVISAO.md).
 */
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include "config.h"

struct BleMessage {
  char data[BLE_MSG_MAX];
};

class BleControl {
 public:
  void begin();

  // Retira uma mensagem da fila. false se nao havia nada.
  bool popMessage(BleMessage &out);

  void notify(const String &json);

  bool isConnected() const { return clientConnected_; }

 private:
  BLEServer *server_ = nullptr;
  BLECharacteristic *charTx_ = nullptr;
  BLECharacteristic *charRx_ = nullptr;

  QueueHandle_t rxQueue_ = nullptr;
  volatile bool clientConnected_ = false;

  friend class RxCallbacks;
  friend class ServerCallbacks;
};
