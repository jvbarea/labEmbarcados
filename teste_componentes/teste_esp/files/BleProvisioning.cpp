#include "BleProvisioning.h"
#include "config.h"
#include <BLE2902.h>

// ---------------------------------------------------------------------------
// Callbacks das características de escrita (SSID, senha, comando)
// ---------------------------------------------------------------------------
class SsidCallbacks : public BLECharacteristicCallbacks {
 public:
  explicit SsidCallbacks(BleProvisioning *owner) : owner_(owner) {}
  void onWrite(BLECharacteristic *c) override {
    owner_->pendingSsid_ = String(c->getValue().c_str());
    Serial.printf("[BLE] SSID recebido: %s\n", owner_->pendingSsid_.c_str());
  }
 private:
  BleProvisioning *owner_;
};

class PassCallbacks : public BLECharacteristicCallbacks {
 public:
  explicit PassCallbacks(BleProvisioning *owner) : owner_(owner) {}
  void onWrite(BLECharacteristic *c) override {
    owner_->pendingPass_ = String(c->getValue().c_str());
    Serial.println("[BLE] Senha Wi-Fi recebida.");
  }
 private:
  BleProvisioning *owner_;
};

class CommandCallbacks : public BLECharacteristicCallbacks {
 public:
  explicit CommandCallbacks(BleProvisioning *owner) : owner_(owner) {}
  void onWrite(BLECharacteristic *c) override {
    String cmd = String(c->getValue().c_str());
    cmd.trim();
    cmd.toUpperCase();
    if (cmd == "CONNECT") {
      owner_->pendingApply_ = true;
    } else if (cmd == "FORGET") {
      owner_->pendingForget_ = true;
    } else {
      Serial.printf("[BLE] Comando desconhecido: %s\n", cmd.c_str());
    }
  }
 private:
  BleProvisioning *owner_;
};

// ---------------------------------------------------------------------------
// BleProvisioning
// ---------------------------------------------------------------------------
void BleProvisioning::begin(WifiManager *wifiManager) {
  wifiManager_ = wifiManager;

  BLEDevice::init(BLE_DEVICE_NAME);
  server_ = BLEDevice::createServer();
  BLEService *service = server_->createService(BLE_SERVICE_UUID);

  charSsid_ = service->createCharacteristic(
      BLE_CHAR_SSID_UUID, BLECharacteristic::PROPERTY_WRITE);
  charSsid_->setCallbacks(new SsidCallbacks(this));

  charPass_ = service->createCharacteristic(
      BLE_CHAR_PASS_UUID, BLECharacteristic::PROPERTY_WRITE);
  charPass_->setCallbacks(new PassCallbacks(this));

  charCommand_ = service->createCharacteristic(
      BLE_CHAR_COMMAND_UUID, BLECharacteristic::PROPERTY_WRITE);
  charCommand_->setCallbacks(new CommandCallbacks(this));

  charStatus_ = service->createCharacteristic(
      BLE_CHAR_STATUS_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  charStatus_->addDescriptor(new BLE2902());
  charStatus_->setValue("{\"status\":\"boot\"}");

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Serviço de provisionamento ativo e anunciando.");
}

void BleProvisioning::publishStatus(const String &statusJson) {
  if (!charStatus_) return;
  charStatus_->setValue(statusJson.c_str());
  charStatus_->notify();
}

void BleProvisioning::consumePendingCredentials() {
  if (!pendingApply_) return;
  pendingApply_ = false;

  if (wifiManager_) {
    bool ok = wifiManager_->connectAndSave(pendingSsid_, pendingPass_);
    publishStatus(ok ? "{\"status\":\"wifi_connected\"}" : "{\"status\":\"wifi_failed\"}");
  }
}

void BleProvisioning::consumePendingForget() {
  pendingForget_ = false;
  // A limpeza efetiva da NVS é feita por quem possui o Storage (main.ino),
  // pois este módulo não guarda a referência para evitar acoplamento extra.
}
