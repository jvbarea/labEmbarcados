// Estado global dos substitutos (relogio, NVS, arquivos, radio, servo).
#include <Arduino.h>
#include <BLEDevice.h>
#include <ESP32Servo.h>
#include <LittleFS.h>
#include <Preferences.h>

namespace sim {
uint64_t nowUs = 0;
bool quietSerial = true;

std::map<std::string, NvsNamespace> nvs;
unsigned nvsWrites = 0;

std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;
bool fsMountFails = false;
unsigned fsWrites = 0;

bool servoAttached = false;
uint32_t servoDuty = 0;

Radio radio;
unsigned oversizedNotifications = 0;

void bleConnect(uint16_t mtu) {
  radio.connected = true;
  radio.mtu = 23;
  if (radio.server && radio.server->callbacks()) radio.server->callbacks()->onConnect(radio.server);
  if (mtu != 23 && radio.server && radio.server->callbacks()) {
    radio.mtu = mtu;
    esp_ble_gatts_cb_param_t p{};
    p.mtu.mtu = mtu;
    radio.server->callbacks()->onMtuChanged(radio.server, &p);
  }
}

void bleDisconnect() {
  radio.connected = false;
  if (radio.server && radio.server->callbacks()) radio.server->callbacks()->onDisconnect(radio.server);
}

void bleWrite(const std::string &bytes) {
  if (!radio.connected || !radio.rx) return;
  radio.rx->setValue((uint8_t *)bytes.data(), bytes.size());
  if (radio.rx->callbacks()) radio.rx->callbacks()->onWrite(radio.rx);
}
}  // namespace sim

HostSerial Serial;
HostLittleFS LittleFS;

void BLECharacteristic::notify() {
  if (!sim::radio.connected) return;
  std::string v = value_;
  // Como a pilha real: o que passa de MTU - 3 e cortado.
  if (v.size() > (size_t)(sim::radio.mtu - 3)) {
    sim::oversizedNotifications++;
    v.resize(sim::radio.mtu - 3);
  }
  sim::radio.notifications.push_back(v);
}

BLECharacteristic *BLEService::createCharacteristic(const char *uuid, uint32_t props) {
  auto *c = new BLECharacteristic(uuid);
  if (props & BLECharacteristic::PROPERTY_NOTIFY) sim::radio.tx = c;
  else sim::radio.rx = c;
  return c;
}

BLEServer *BLEDevice::createServer() {
  sim::radio.server = new BLEServer();
  return sim::radio.server;
}

BLEAdvertising *BLEDevice::getAdvertising() {
  static BLEAdvertising adv;
  return &adv;
}
