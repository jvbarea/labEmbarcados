#pragma once
/*
 * BleControl.h
 * Canal BLE de CONTROLE do alimentador (nao mais so provisionamento de Wi-Fi).
 *
 * Usa o Nordic UART Service, que o nRF Connect reconhece de cara:
 *   RX (write)  app -> ESP : mensagens JSON
 *   TX (notify) ESP -> app : status, acks e eventos
 *
 * Mensagens grandes viajam em PEDACOS. Cada escrita ou notificacao e:
 *   - uma mensagem inteira: o JSON, que comeca com '{'; ou
 *   - um pedaco: cabecalho ASCII "#i/n:" (i de 1 a n, n de 2 a 99) seguido de um
 *     trecho dos BYTES da mensagem. O corte e por byte, entao um emoji pode ficar
 *     dividido entre dois pedacos: quem recebe junta os bytes e so depois le.
 * Quem recebe descarta a montagem quando um pedaco chega fora de ordem ou falta.
 * O app faz o mesmo do lado dele (app/src/device/framing.ts).
 *
 * Por que: uma notificacao carrega no maximo MTU - 3 bytes, e o "cfgDump" com os
 * nomes e emoji dos gatos passa disso. Antes o firmware simplesmente omitia o
 * que nao cabia.
 *
 * Regras de concorrencia: os callbacks do BLE rodam em outra task e por isso SO
 * ENFILEIRAM. Quem remonta, interpreta e responde e o loop(). No codigo original
 * o callback escrevia direto em Strings lidas pelo loop, sem protecao (REVISAO.md).
 * As notificacoes tambem saem pelo loop, uma a cada BLE_TX_GAP_MS: uma rajada
 * (o sync do historico) enchia a fila da pilha BLE e as ultimas se perdiam.
 */
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <deque>
#include "config.h"

struct BleMessage {
  char     data[BLE_MSG_MAX + 1];  // sempre terminado em '\0'
  uint16_t len;
};

class BleControl {
 public:
  void begin();

  // Chamar no loop(): manda a proxima notificacao da fila e limpa o estado
  // quando a conexao cai.
  void update();

  // Retira uma mensagem COMPLETA, ja remontada. false se nao ha nenhuma.
  bool popMessage(BleMessage &out);

  // Enfileira uma mensagem para o app, em pedacos se passar de chunkSize().
  // Sem cliente conectado ela e descartada: o app recupera pelo status e pelo sync.
  void send(const String &json);

  bool isConnected() const { return clientConnected_; }
  size_t txBacklog() const { return txQueue_.size(); }
  uint16_t mtu() const { return peerMtu_; }
  size_t chunkSize() const;

 private:
  BLEServer *server_ = nullptr;
  BLECharacteristic *charTx_ = nullptr;
  BLECharacteristic *charRx_ = nullptr;

  QueueHandle_t rxQueue_ = nullptr;
  volatile bool clientConnected_ = false;
  volatile uint16_t peerMtu_ = 23;  // 23 = MTU minimo do BLE, ate o celular negociar
  bool wasConnected_ = false;

  std::deque<String> txQueue_;
  unsigned long lastTxMs_ = 0;

  // Remontagem do que chega em pedacos (so o loop mexe aqui).
  char   asm_[BLE_MSG_MAX + 1];
  size_t asmLen_ = 0;
  int    asmNext_ = 0;   // proximo pedaco esperado; 0 = nenhuma montagem em curso
  int    asmTotal_ = 0;

  void pushTx(const String &part);
  bool feedFragment(const BleMessage &raw, BleMessage &out);
  void resetAssembly() { asmLen_ = 0; asmNext_ = 0; asmTotal_ = 0; }

  friend class RxCallbacks;
  friend class ServerCallbacks;
};
