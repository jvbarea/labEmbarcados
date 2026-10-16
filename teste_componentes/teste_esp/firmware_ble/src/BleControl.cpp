#include "BleControl.h"
#include <BLE2902.h>

class RxCallbacks : public BLECharacteristicCallbacks {
 public:
  explicit RxCallbacks(BleControl *owner) : owner_(owner) {}
  void onWrite(BLECharacteristic *c) override {
    // getValue() devolve std::string no core 2.x e String no 3.x; c_str() e
    // length() existem nos dois, entao este trecho compila em ambos.
    auto v = c->getValue();
    size_t n = v.length();
    if (n == 0 || !owner_->rxQueue_) return;
    if (n >= BLE_MSG_MAX) n = BLE_MSG_MAX - 1;

    BleMessage msg;
    memcpy(msg.data, v.c_str(), n);
    msg.data[n] = '\0';
    xQueueSend(owner_->rxQueue_, &msg, 0);  // sem bloquear a task do BLE
  }

 private:
  BleControl *owner_;
};

class ServerCallbacks : public BLEServerCallbacks {
 public:
  explicit ServerCallbacks(BleControl *owner) : owner_(owner) {}
  void onConnect(BLEServer *) override {
    owner_->clientConnected_ = true;
    Serial.println("[ble] cliente conectado");
  }
  void onDisconnect(BLEServer *) override {
    owner_->clientConnected_ = false;
    Serial.println("[ble] cliente desconectado - voltando a anunciar");
    BLEDevice::startAdvertising();  // sem isto o dispositivo some apos a 1a conexao
  }

 private:
  BleControl *owner_;
};

void BleControl::begin() {
  rxQueue_ = xQueueCreate(BLE_RX_QUEUE_LEN, sizeof(BleMessage));

  BLEDevice::init(BLE_DEVICE_NAME);
  BLEDevice::setMTU(185);  // cabe o JSON de status inteiro numa notificacao

  server_ = BLEDevice::createServer();
  server_->setCallbacks(new ServerCallbacks(this));

  BLEService *service = server_->createService(BLE_SERVICE_UUID);

  charRx_ = service->createCharacteristic(
      BLE_CHAR_RX_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  charRx_->setCallbacks(new RxCallbacks(this));

  charTx_ = service->createCharacteristic(
      BLE_CHAR_TX_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  charTx_->addDescriptor(new BLE2902());
  charTx_->setValue("{\"t\":\"boot\"}");

  service->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);  // ajuda na descoberta por iPhone
  BLEDevice::startAdvertising();

  Serial.println("[ble] anunciando como \"" BLE_DEVICE_NAME "\"");
}

bool BleControl::popMessage(BleMessage &out) {
  if (!rxQueue_) return false;
  return xQueueReceive(rxQueue_, &out, 0) == pdTRUE;
}

void BleControl::notify(const String &json) {
  if (!charTx_) return;
  charTx_->setValue(json.c_str());
  if (clientConnected_) charTx_->notify();
}
