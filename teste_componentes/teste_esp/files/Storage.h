#pragma once
/*
 * Storage.h
 * Camada de persistência em memória não volátil (NVS via Preferences).
 * Guarda: credenciais Wi-Fi e configuração operacional do alimentador
 * (janela de peso, temporizadores, fator de calibração e tara).
 * Atende RF05, RF07, RF09, RF10.
 */
#include <Arduino.h>
#include "config.h"

struct AppConfig {
  float weightMinG      = DEFAULT_WEIGHT_MIN_G;
  float weightMaxG      = DEFAULT_WEIGHT_MAX_G;
  uint32_t stabilizeMs      = DEFAULT_STABILIZE_MS;
  uint32_t vacateConfirmMs  = DEFAULT_VACATE_CONFIRM_MS;
  float calibrationFactor = HX711_CALIBRATION_DEFAULT; // contagens/grama
  long  tareOffset        = 0;                          // offset bruto do HX711
};

struct WifiCredentials {
  String ssid;
  String password;
  bool valid = false;
};

class Storage {
 public:
  void begin();

  // Configuração operacional
  AppConfig loadAppConfig();
  void saveAppConfig(const AppConfig &cfg);

  // Credenciais Wi-Fi
  WifiCredentials loadWifiCredentials();
  void saveWifiCredentials(const String &ssid, const String &password);
  void clearWifiCredentials();
};
