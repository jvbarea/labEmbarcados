#pragma once
// NVS de mentira: um mapa em memoria que SOBREVIVE ao "reboot" do simulador
// (o teste destroi e recria os objetos do firmware, nao este mapa).
#include <Arduino.h>
#include <map>
#include <string>
#include <vector>

namespace sim {
using NvsNamespace = std::map<std::string, std::vector<uint8_t>>;
extern std::map<std::string, NvsNamespace> nvs;
extern unsigned nvsWrites;  // quantas gravacoes houve (para conferir o "so grava se mudou")
}

class Preferences {
 public:
  bool begin(const char *ns, bool readOnly = false) {
    auto it = sim::nvs.find(ns);
    if (it == sim::nvs.end()) {
      if (readOnly) return false;  // como na ESP32: so leitura nao cria o namespace
      it = sim::nvs.emplace(ns, sim::NvsNamespace{}).first;
    }
    ns_ = &it->second;
    readOnly_ = readOnly;
    return true;
  }
  void end() { ns_ = nullptr; }

  uint32_t getUInt(const char *k, uint32_t d = 0) { return get<uint32_t>(k, d); }
  int32_t getInt(const char *k, int32_t d = 0) { return get<int32_t>(k, d); }
  bool getBool(const char *k, bool d = false) { return get<uint8_t>(k, d ? 1 : 0) != 0; }
  float getFloat(const char *k, float d = 0) { return get<float>(k, d); }
  long getLong(const char *k, long d = 0) { return (long)get<int32_t>(k, (int32_t)d); }
  uint8_t getUChar(const char *k, uint8_t d = 0) { return get<uint8_t>(k, d); }
  String getString(const char *k, const String &d = String()) {
    auto *v = find(k);
    if (!v) return d;
    return String(std::string(v->begin(), v->end()));
  }
  size_t getBytes(const char *k, void *buf, size_t len) {
    auto *v = find(k);
    if (!v || v->size() > len) return 0;
    std::memcpy(buf, v->data(), v->size());
    return v->size();
  }

  size_t putUInt(const char *k, uint32_t v) { return put(k, &v, sizeof v); }
  size_t putInt(const char *k, int32_t v) { return put(k, &v, sizeof v); }
  size_t putBool(const char *k, bool v) {
    uint8_t b = v ? 1 : 0;
    return put(k, &b, 1);
  }
  size_t putFloat(const char *k, float v) { return put(k, &v, sizeof v); }
  size_t putLong(const char *k, long v) {
    int32_t x = (int32_t)v;
    return put(k, &x, sizeof x);
  }
  size_t putUChar(const char *k, uint8_t v) { return put(k, &v, 1); }
  size_t putString(const char *k, const String &v) { return put(k, v.c_str(), v.length()); }
  size_t putBytes(const char *k, const void *buf, size_t len) { return put(k, buf, len); }

 private:
  sim::NvsNamespace *ns_ = nullptr;
  bool readOnly_ = false;

  std::vector<uint8_t> *find(const char *k) {
    if (!ns_) return nullptr;
    auto it = ns_->find(k);
    return it == ns_->end() ? nullptr : &it->second;
  }
  template <typename T>
  T get(const char *k, T d) {
    auto *v = find(k);
    if (!v || v->size() != sizeof(T)) return d;
    T out;
    std::memcpy(&out, v->data(), sizeof(T));
    return out;
  }
  size_t put(const char *k, const void *p, size_t n) {
    if (!ns_ || readOnly_) return 0;
    const uint8_t *b = (const uint8_t *)p;
    (*ns_)[k] = std::vector<uint8_t>(b, b + n);
    sim::nvsWrites++;
    return n;
  }
};
