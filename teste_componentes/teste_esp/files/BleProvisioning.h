#pragma once
/*
 * BleProvisioning.h
 * Serviço BLE (GATT) usado pelo aplicativo para:
 *  - enviar SSID e senha da rede Wi-Fi doméstica (RF08, RF09);
 *  - disparar o comando de conexão;
 *  - opcionalmente "esquecer" as credenciais salvas.
 * Também publica (notify) um status resumido do dispositivo.
 *
 * Fica sempre ativo: além do provisionamento inicial, serve como canal de
 * fallback caso a rede Wi-Fi fique indisponível.
 *
 * Depende da biblioteca embutida "ESP32 BLE Arduino" (BLEDevice.h).
 */
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include "WifiManager.h"

class BleProvisioning {
 public:
  void begin(WifiManager *wifiManager);

  // Publica um status JSON curto na característica de status (notify),
  // para o app acompanhar o estado da conexão sem precisar consultar HTTP.
  void publishStatus(const String &statusJson);

  bool hasPendingCredentials() const { return pendingApply_; }
  void consumePendingCredentials(); // aplica (conecta + salva) as credenciais recebidas

 private:
  WifiManager *wifiManager_ = nullptr;

  BLEServer *server_ = nullptr;
  BLECharacteristic *charSsid_ = nullptr;
  BLECharacteristic *charPass_ = nullptr;
  BLECharacteristic *charCommand_ = nullptr;
  BLECharacteristic *charStatus_ = nullptr;

  String pendingSsid_;
  String pendingPass_;
  volatile bool pendingApply_ = false;
  volatile bool pendingForget_ = false;

  // Callbacks internos (definidos em BleProvisioning.cpp) precisam escrever
  // diretamente nos campos pendentes ao receber dados via BLE.
  friend class SsidCallbacks;
  friend class PassCallbacks;
  friend class CommandCallbacks;

 public:
  bool hasPendingForget() const { return pendingForget_; }
  void consumePendingForget();
};
