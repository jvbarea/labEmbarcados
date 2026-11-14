#pragma once
/*
 * WifiManager.h
 * Conexão à rede Wi-Fi doméstica usando credenciais persistidas em NVS.
 * Não depende do BLE estar ativo: se já existem credenciais salvas, conecta
 * sozinho a cada boot, sem precisar recebê-las de novo pelo aplicativo.
 * Atende RF09 e RF10 (persistência de configuração).
 */
#include <Arduino.h>
#include <WiFi.h>
#include "Storage.h"

class WifiManager {
 public:
  void begin(Storage *storage);

  // Tenta conectar usando as credenciais salvas (não-bloqueante além do
  // timeout definido em WIFI_CONNECT_TIMEOUT_MS). Retorna true se conectou.
  bool connectFromStoredCredentials();

  // Recebe novas credenciais (tipicamente vindas do BLE), salva na NVS e
  // tenta conectar imediatamente.
  bool connectAndSave(const String &ssid, const String &password);

  bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
  String ipAddress() const { return isConnected() ? WiFi.localIP().toString() : String(""); }

  // Deve ser chamado periodicamente para detectar quedas de conexão e,
  // se houver credenciais salvas, tentar reconectar em segundo plano.
  void update();

 private:
  Storage *storage_ = nullptr;
  unsigned long lastReconnectAttemptMs_ = 0;
  static const unsigned long RECONNECT_INTERVAL_MS = 10000;

  bool waitForConnection(unsigned long timeoutMs);
};
