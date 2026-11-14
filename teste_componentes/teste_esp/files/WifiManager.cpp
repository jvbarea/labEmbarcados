#include "WifiManager.h"
#include "config.h"

void WifiManager::begin(Storage *storage) {
  storage_ = storage;
  WiFi.mode(WIFI_MODE_APSTA); // STA para a rede doméstica + mantém rádio disponível para BLE/AP se necessário
}

bool WifiManager::waitForConnection(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(200);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool WifiManager::connectFromStoredCredentials() {
  if (!storage_) return false;

  WifiCredentials creds = storage_->loadWifiCredentials();
  if (!creds.valid) {
    Serial.println("[WiFi] Nenhuma credencial salva na memória flash.");
    return false;
  }

  Serial.printf("[WiFi] Conectando à rede salva: %s\n", creds.ssid.c_str());
  WiFi.begin(creds.ssid.c_str(), creds.password.c_str());

  bool ok = waitForConnection(WIFI_CONNECT_TIMEOUT_MS);
  if (ok) {
    Serial.printf("[WiFi] Conectado. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WiFi] Falha ao conectar com as credenciais salvas.");
  }
  return ok;
}

bool WifiManager::connectAndSave(const String &ssid, const String &password) {
  if (ssid.length() == 0) return false;

  Serial.printf("[WiFi] Recebida nova credencial via BLE. SSID: %s\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());

  bool ok = waitForConnection(WIFI_CONNECT_TIMEOUT_MS);
  if (ok && storage_) {
    storage_->saveWifiCredentials(ssid, password); // RF10: persiste apenas se a conexão foi validada
    Serial.println("[WiFi] Credenciais validadas e salvas na memória flash.");
  } else {
    Serial.println("[WiFi] Não foi possível conectar; credenciais não foram salvas.");
  }
  return ok;
}

void WifiManager::update() {
  if (isConnected()) return;

  unsigned long now = millis();
  if (now - lastReconnectAttemptMs_ < RECONNECT_INTERVAL_MS) return;
  lastReconnectAttemptMs_ = now;

  WifiCredentials creds = storage_ ? storage_->loadWifiCredentials() : WifiCredentials{};
  if (creds.valid) {
    Serial.println("[WiFi] Conexão perdida, tentando reconectar em segundo plano...");
    WiFi.begin(creds.ssid.c_str(), creds.password.c_str());
  }
}
