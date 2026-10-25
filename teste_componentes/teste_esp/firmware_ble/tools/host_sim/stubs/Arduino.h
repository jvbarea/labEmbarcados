#pragma once
/*
 * Substituto MINIMO do Arduino.h para compilar a logica do firmware no PC.
 * So tem o que os arquivos de src/ usam. O relogio e falso: o teste avanca o
 * tempo com sim::advance(), e millis()/esp_timer_get_time() leem dele.
 */
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#ifndef ARDUINO
#define ARDUINO 10819
#endif

namespace sim {
extern uint64_t nowUs;              // relogio falso, em microssegundos
inline void advance(uint64_t ms) { nowUs += ms * 1000ULL; }
extern bool quietSerial;            // true = engole o que o firmware imprime
}

inline unsigned long millis() { return (unsigned long)(sim::nowUs / 1000ULL); }
inline unsigned long micros() { return (unsigned long)sim::nowUs; }
inline void delay(unsigned long ms) { sim::advance(ms); }
inline void yield() {}

template <typename T, typename L, typename H>
inline T constrain(T v, L lo, H hi) {
  return v < (T)lo ? (T)lo : (v > (T)hi ? (T)hi : v);
}
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define LOW 0
#define HIGH 1
inline void pinMode(int, int) {}
inline int digitalRead(int) { return HIGH; }
inline void digitalWrite(int, int) {}

// ------------------------------------------------------------------ String
// Copia do comportamento do String do Arduino que o firmware usa: concatena
// numeros como texto, compara, corta por BYTE.
class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  String(const String &o) = default;
  String(char c) : s_(1, c) {}
  explicit String(unsigned char v) : s_(std::to_string((unsigned)v)) {}
  explicit String(int v) : s_(std::to_string(v)) {}
  explicit String(unsigned int v) : s_(std::to_string(v)) {}
  explicit String(long v) : s_(std::to_string(v)) {}
  explicit String(unsigned long v) : s_(std::to_string(v)) {}

  String &operator=(const String &o) = default;
  String &operator=(const char *s) {
    s_ = s ? s : "";
    return *this;
  }

  const char *c_str() const { return s_.c_str(); }
  unsigned int length() const { return (unsigned int)s_.size(); }
  void reserve(unsigned int n) { s_.reserve(n); }
  char charAt(unsigned int i) const { return i < s_.size() ? s_[i] : 0; }
  char operator[](unsigned int i) const { return charAt(i); }

  bool concat(const char *s) {
    if (s) s_ += s;
    return true;
  }
  bool concat(const String &o) {
    s_ += o.s_;
    return true;
  }
  bool concat(char c) {
    s_ += c;
    return true;
  }
  bool concat(unsigned char v) { return concat(std::to_string((unsigned)v).c_str()); }
  bool concat(int v) { return concat(std::to_string(v).c_str()); }
  bool concat(unsigned int v) { return concat(std::to_string(v).c_str()); }
  bool concat(long v) { return concat(std::to_string(v).c_str()); }
  bool concat(unsigned long v) { return concat(std::to_string(v).c_str()); }

  template <typename T>
  String &operator+=(const T &v) {
    concat(v);
    return *this;
  }

  String substring(unsigned int from) const { return substring(from, length()); }
  String substring(unsigned int from, unsigned int to) const {
    if (from > to) std::swap(from, to);
    if (from >= s_.size()) return String();
    if (to > s_.size()) to = (unsigned)s_.size();
    return String(s_.substr(from, to - from));
  }
  int indexOf(char c) const {
    auto p = s_.find(c);
    return p == std::string::npos ? -1 : (int)p;
  }
  void trim() {
    size_t a = s_.find_first_not_of(" \t\r\n");
    size_t b = s_.find_last_not_of(" \t\r\n");
    s_ = (a == std::string::npos) ? "" : s_.substr(a, b - a + 1);
  }
  void toLowerCase() {
    for (auto &c : s_) c = (char)std::tolower((unsigned char)c);
  }
  long toInt() const { return std::atol(s_.c_str()); }
  float toFloat() const { return (float)std::atof(s_.c_str()); }
  bool startsWith(const char *p) const { return s_.rfind(p, 0) == 0; }

  bool operator==(const String &o) const { return s_ == o.s_; }
  bool operator!=(const String &o) const { return s_ != o.s_; }
  bool operator==(const char *o) const { return s_ == (o ? o : ""); }
  bool operator!=(const char *o) const { return !(*this == o); }

  const std::string &str() const { return s_; }

 private:
  std::string s_;
};

inline String operator+(const String &a, const String &b) {
  String r(a);
  r += b;
  return r;
}
inline String operator+(const char *a, const String &b) {
  String r(a);
  r += b;
  return r;
}
inline String operator+(const String &a, const char *b) {
  String r(a);
  r += b;
  return r;
}

// ------------------------------------------------------------------ Serial
class HostSerial {
 public:
  void begin(unsigned long) {}
  int available() { return 0; }
  int read() { return -1; }
  int printf(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (sim::quietSerial) return 0;
    va_list ap;
    va_start(ap, fmt);
    int n = std::vprintf(fmt, ap);
    va_end(ap);
    return n;
  }
  size_t print(const char *s) {
    if (!sim::quietSerial) std::fputs(s, stdout);
    return std::strlen(s);
  }
  size_t print(const String &s) { return print(s.c_str()); }
  size_t println() { return print("\n"); }
  size_t println(const char *s) {
    print(s);
    return print("\n");
  }
  size_t println(const String &s) { return println(s.c_str()); }
};
extern HostSerial Serial;

// ---------------------------------------------------------- FreeRTOS (fila)
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
#define pdTRUE 1
#define pdFALSE 0
struct HostQueue {
  size_t itemSize;
  size_t capacity;
  std::deque<std::vector<uint8_t>> items;
};
typedef HostQueue *QueueHandle_t;
inline QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t itemSize) {
  return new HostQueue{itemSize, len, {}};
}
inline BaseType_t xQueueSend(QueueHandle_t q, const void *item, int) {
  if (q->items.size() >= q->capacity) return pdFALSE;
  const uint8_t *p = (const uint8_t *)item;
  q->items.emplace_back(p, p + q->itemSize);
  return pdTRUE;
}
inline BaseType_t xQueueReceive(QueueHandle_t q, void *out, int) {
  if (q->items.empty()) return pdFALSE;
  std::memcpy(out, q->items.front().data(), q->itemSize);
  q->items.pop_front();
  return pdTRUE;
}
