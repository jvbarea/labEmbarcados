#pragma once
// LittleFS de mentira: arquivos em memoria que SOBREVIVEM ao "reboot" do
// simulador. sim::fsMountFails simula a particao que nao monta.
#include <Arduino.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sim {
extern std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;
extern bool fsMountFails;
extern unsigned fsWrites;
}

enum SeekMode { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };

class File {
 public:
  File() {}
  File(std::shared_ptr<std::vector<uint8_t>> data, bool canWrite)
      : data_(data), canWrite_(canWrite) {}
  explicit operator bool() const { return (bool)data_; }

  size_t size() const { return data_ ? data_->size() : 0; }
  bool seek(uint32_t pos, SeekMode mode = SeekSet) {
    if (!data_ || mode != SeekSet || pos > data_->size()) return false;
    pos_ = pos;
    return true;
  }
  size_t read(uint8_t *buf, size_t n) {
    if (!data_) return 0;
    size_t k = std::min(n, data_->size() - pos_);
    std::memcpy(buf, data_->data() + pos_, k);
    pos_ += k;
    return k;
  }
  size_t write(const uint8_t *buf, size_t n) {
    if (!data_ || !canWrite_) return 0;
    if (pos_ + n > data_->size()) data_->resize(pos_ + n);
    std::memcpy(data_->data() + pos_, buf, n);
    pos_ += n;
    sim::fsWrites++;
    return n;
  }
  void close() { data_.reset(); }

 private:
  std::shared_ptr<std::vector<uint8_t>> data_;
  bool canWrite_ = false;
  size_t pos_ = 0;
};

class HostLittleFS {
 public:
  bool begin(bool formatOnFail = false, const char * = "/littlefs", uint8_t = 10,
             const char * = "spiffs") {
    (void)formatOnFail;
    return !sim::fsMountFails;
  }
  File open(const char *path, const char *mode = "r") {
    std::string m(mode);
    auto it = sim::files.find(path);
    if (m == "w") {
      auto data = std::make_shared<std::vector<uint8_t>>();
      sim::files[path] = data;
      return File(data, true);
    }
    if (it == sim::files.end()) return File();
    return File(it->second, m == "r+");
  }
  bool exists(const char *path) { return sim::files.count(path) > 0; }
};
extern HostLittleFS LittleFS;
