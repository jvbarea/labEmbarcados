#pragma once
/*
 * Pilha BLE de mentira. Tem so a API que o BleControl usa, e um "radio" que o
 * teste controla: conectar com um MTU, escrever na caracteristica RX como o
 * celular faria, e recolher as notificacoes que sairam pela TX.
 */
#include <Arduino.h>
#include <string>
#include <vector>

struct esp_ble_gatts_cb_param_t {
  struct {
    uint16_t conn_id;
    uint16_t mtu;
  } mtu;
};

class BLEServer;
class BLECharacteristic;

class BLEServerCallbacks {
 public:
  virtual ~BLEServerCallbacks() {}
  virtual void onConnect(BLEServer *) {}
  virtual void onDisconnect(BLEServer *) {}
  virtual void onMtuChanged(BLEServer *, esp_ble_gatts_cb_param_t *) {}
};

class BLECharacteristicCallbacks {
 public:
  virtual ~BLECharacteristicCallbacks() {}
  virtual void onWrite(BLECharacteristic *) {}
};

class BLEDescriptor {
 public:
  virtual ~BLEDescriptor() {}
};

namespace sim {
// O que o teste enxerga do radio.
struct Radio {
  BLEServer *server = nullptr;
  BLECharacteristic *rx = nullptr;
  BLECharacteristic *tx = nullptr;
  bool connected = false;
  uint16_t mtu = 23;
  std::vector<std::string> notifications;  // o que o "celular" recebeu, na ordem
  bool advertising = false;
};
extern Radio radio;
extern unsigned oversizedNotifications;  // notificacoes maiores que MTU - 3 (a pilha corta)
void bleConnect(uint16_t mtu);
void bleDisconnect();
void bleWrite(const std::string &bytes);  // uma escrita do celular na RX
}

class BLECharacteristic {
 public:
  static const uint32_t PROPERTY_READ = 1 << 0;
  static const uint32_t PROPERTY_WRITE = 1 << 1;
  static const uint32_t PROPERTY_NOTIFY = 1 << 2;
  static const uint32_t PROPERTY_WRITE_NR = 1 << 3;

  explicit BLECharacteristic(const std::string &uuid) : uuid_(uuid) {}
  void setCallbacks(BLECharacteristicCallbacks *cb) { cb_ = cb; }
  void addDescriptor(BLEDescriptor *) {}
  void setValue(const char *s) { value_ = s ? s : ""; }
  void setValue(uint8_t *data, size_t len) { value_.assign((const char *)data, len); }
  std::string getValue() const { return value_; }
  void notify();

  // usado pelo radio de mentira
  BLECharacteristicCallbacks *callbacks() const { return cb_; }
  const std::string &uuid() const { return uuid_; }

 private:
  std::string uuid_;
  std::string value_;
  BLECharacteristicCallbacks *cb_ = nullptr;
};

class BLEService {
 public:
  BLECharacteristic *createCharacteristic(const char *uuid, uint32_t props);
  void start() {}
};

class BLEServer {
 public:
  void setCallbacks(BLEServerCallbacks *cb) { cb_ = cb; }
  BLEService *createService(const char *) { return new BLEService(); }
  BLEServerCallbacks *callbacks() const { return cb_; }

 private:
  BLEServerCallbacks *cb_ = nullptr;
};

class BLEAdvertising {
 public:
  void addServiceUUID(const char *) {}
  void setScanResponse(bool) {}
  void setMinPreferred(int) {}
};

class BLEDevice {
 public:
  static void init(const char *) {}
  static void setMTU(uint16_t) {}
  static BLEServer *createServer();
  static BLEAdvertising *getAdvertising();
  static void startAdvertising() { sim::radio.advertising = true; }
};
