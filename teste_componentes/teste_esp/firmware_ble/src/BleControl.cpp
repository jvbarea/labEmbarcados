#include "BleControl.h"
#include <BLE2902.h>

// Maior cabecalho possivel, "#99/99:". O trecho de dados de cada pedaco e o
// tamanho do pedaco menos isso, para a conta nao depender de quantos digitos
// o i e o n tem.
static const size_t FRAG_HEADER_MAX = 7;

class RxCallbacks : public BLECharacteristicCallbacks {
 public:
  explicit RxCallbacks(BleControl *owner) : owner_(owner) {}
  void onWrite(BLECharacteristic *c) override {
    // getValue() devolve std::string no core 2.x e String no 3.x; c_str() e
    // length() existem nos dois, entao este trecho compila em ambos.
    auto v = c->getValue();
    size_t n = v.length();
    if (n == 0 || !owner_->rxQueue_) return;
    if (n > BLE_MSG_MAX) {
      // Escrita longa (prepare write) maior que o firmware aceita. Sem ler o id
      // nao ha como responder; o app vai ver o timeout do ack.
      Serial.printf("[ble] escrita de %u bytes descartada (maximo %u)\n",
                    (unsigned)n, (unsigned)BLE_MSG_MAX);
      return;
    }

    BleMessage msg;
    memcpy(msg.data, v.c_str(), n);
    msg.data[n] = '\0';
    msg.len = (uint16_t)n;
    if (xQueueSend(owner_->rxQueue_, &msg, 0) != pdTRUE) {  // sem bloquear a task do BLE
      Serial.println("[ble] fila de entrada cheia - mensagem descartada");
    }
  }

 private:
  BleControl *owner_;
};

class ServerCallbacks : public BLEServerCallbacks {
 public:
  explicit ServerCallbacks(BleControl *owner) : owner_(owner) {}
  void onConnect(BLEServer *) override {
    owner_->peerMtu_ = 23;
    owner_->clientConnected_ = true;
    Serial.println("[ble] cliente conectado");
  }
  void onMtuChanged(BLEServer *, esp_ble_gatts_cb_param_t *param) override {
    owner_->peerMtu_ = param->mtu.mtu;
    Serial.printf("[ble] MTU negociado: %u (pedacos de ate %u bytes)\n",
                  (unsigned)param->mtu.mtu, (unsigned)owner_->chunkSize());
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
  BLEDevice::setMTU(BLE_MTU_REQUEST);

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

size_t BleControl::chunkSize() const {
  size_t c = (peerMtu_ > 3) ? (size_t)peerMtu_ - 3 : 20;
  if (c > BLE_CHUNK_MAX) c = BLE_CHUNK_MAX;
  if (c < 20) c = 20;
  return c;
}

// ------------------------------------------------------------------ saida

void BleControl::pushTx(const String &part) {
  if (txQueue_.size() >= BLE_TX_QUEUE_MAX) {
    // So acontece se o loop parar de escoar. Perder o mais antigo e melhor que
    // a memoria acabar; o app se recupera pelo proximo status e pelo sync.
    txQueue_.pop_front();
    Serial.println("[ble] fila de saida cheia - notificacao mais antiga descartada");
  }
  txQueue_.push_back(part);
}

void BleControl::send(const String &json) {
  if (!clientConnected_ || !charTx_) return;

  const size_t len = json.length();
  const size_t chunk = chunkSize();
  if (len <= chunk) {
    pushTx(json);
    return;
  }

  const size_t body = chunk - FRAG_HEADER_MAX;
  const size_t n = (len + body - 1) / body;
  if (n > 99) {
    Serial.printf("[ble] mensagem de %u bytes grande demais para o cabecalho #i/n - descartada\n",
                  (unsigned)len);
    return;
  }
  for (size_t i = 0; i < n; i++) {
    const size_t de = i * body;
    const size_t ate = (de + body < len) ? de + body : len;
    String part;
    part.reserve(FRAG_HEADER_MAX + (ate - de));
    part += '#';
    part += String((unsigned)(i + 1));
    part += '/';
    part += String((unsigned)n);
    part += ':';
    part += json.substring(de, ate);  // substring do Arduino corta por byte
    pushTx(part);
  }
}

void BleControl::update() {
  const bool conectado = clientConnected_;
  if (conectado != wasConnected_) {
    // Conexao nova ou queda: nada do que estava na fila ou pela metade vale mais.
    txQueue_.clear();
    resetAssembly();
    wasConnected_ = conectado;
  }
  if (!conectado || txQueue_.empty()) return;
  if (millis() - lastTxMs_ < BLE_TX_GAP_MS) return;
  lastTxMs_ = millis();

  const String &s = txQueue_.front();
  charTx_->setValue((uint8_t *)s.c_str(), s.length());
  charTx_->notify();
  txQueue_.pop_front();
}

// ---------------------------------------------------------------- entrada

// "#i/n:" -> i, n e o tamanho do cabecalho. false se nao for um cabecalho valido.
static bool parseHeader(const char *d, size_t len, int &i, int &n, size_t &hdr) {
  if (len < 5 || d[0] != '#') return false;
  size_t p = 1;
  int a = 0, b = 0, dig = 0;
  while (p < len && p <= FRAG_HEADER_MAX && isdigit((unsigned char)d[p])) {
    a = a * 10 + (d[p] - '0');
    p++;
    dig++;
  }
  if (dig == 0 || p >= len || d[p] != '/') return false;
  p++;
  dig = 0;
  while (p < len && p <= FRAG_HEADER_MAX && isdigit((unsigned char)d[p])) {
    b = b * 10 + (d[p] - '0');
    p++;
    dig++;
  }
  if (dig == 0 || p >= len || d[p] != ':') return false;
  p++;
  if (b < 2 || b > 99 || a < 1 || a > b) return false;
  i = a;
  n = b;
  hdr = p;
  return true;
}

bool BleControl::feedFragment(const BleMessage &raw, BleMessage &out) {
  int i = 0, n = 0;
  size_t hdr = 0;
  if (!parseHeader(raw.data, raw.len, i, n, hdr)) {
    Serial.println("[ble] cabecalho de pedaco invalido - descartado");
    resetAssembly();
    return false;
  }

  if (i == 1) {
    asmLen_ = 0;
    asmTotal_ = n;
    asmNext_ = 1;
  }
  if (asmNext_ == 0 || i != asmNext_ || n != asmTotal_) {
    Serial.printf("[ble] pedaco %d/%d fora de ordem - mensagem descartada\n", i, n);
    resetAssembly();
    return false;
  }

  const size_t body = raw.len - hdr;
  if (asmLen_ + body > BLE_MSG_MAX) {
    Serial.printf("[ble] mensagem em pedacos passou de %u bytes - descartada\n",
                  (unsigned)BLE_MSG_MAX);
    resetAssembly();
    return false;
  }
  memcpy(asm_ + asmLen_, raw.data + hdr, body);
  asmLen_ += body;
  asmNext_++;

  if (i < n) return false;

  memcpy(out.data, asm_, asmLen_);
  out.data[asmLen_] = '\0';
  out.len = (uint16_t)asmLen_;
  resetAssembly();
  return true;
}

bool BleControl::popMessage(BleMessage &out) {
  if (!rxQueue_) return false;
  BleMessage raw;
  while (xQueueReceive(rxQueue_, &raw, 0) == pdTRUE) {
    if (raw.len == 0) continue;
    if (raw.data[0] == '#') {
      if (feedFragment(raw, out)) return true;
      continue;  // pedaco do meio: espera o resto
    }
    // Mensagem inteira. Se havia uma montagem pela metade, ela nao vai mais
    // terminar: o app manda uma mensagem de cada vez.
    resetAssembly();
    out = raw;
    return true;
  }
  return false;
}
