#include "Storage.h"
#include "config.h"
#include <Preferences.h>

void Storage::begin() {
  // Nada a inicializar globalmente; cada método abre seu próprio namespace
  // Preferences por operação, evitando handles abertos simultaneamente.
}

AppConfig Storage::loadAppConfig() {
  Preferences prefs;
  AppConfig cfg; // já vem com os valores padrão de config.h

  if (!prefs.begin(NVS_NAMESPACE_APP, /*readOnly=*/true)) {
    // Namespace ainda não existe (primeira execução): devolve os padrões.
    return cfg;
  }

  cfg.weightMinG        = prefs.getFloat("wMin", cfg.weightMinG);
  cfg.weightMaxG        = prefs.getFloat("wMax", cfg.weightMaxG);
  cfg.stabilizeMs        = prefs.getUInt("stabMs", cfg.stabilizeMs);
  cfg.vacateConfirmMs    = prefs.getUInt("vacMs", cfg.vacateConfirmMs);
  cfg.calibrationFactor = prefs.getFloat("calib", cfg.calibrationFactor);
  cfg.tareOffset         = prefs.getLong("tare", cfg.tareOffset);

  prefs.end();
  return cfg;
}

void Storage::saveAppConfig(const AppConfig &cfg) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE_APP, /*readOnly=*/false)) return;

  prefs.putFloat("wMin", cfg.weightMinG);
  prefs.putFloat("wMax", cfg.weightMaxG);
  prefs.putUInt("stabMs", cfg.stabilizeMs);
  prefs.putUInt("vacMs", cfg.vacateConfirmMs);
  prefs.putFloat("calib", cfg.calibrationFactor);
  prefs.putLong("tare", cfg.tareOffset);

  prefs.end();
}

WifiCredentials Storage::loadWifiCredentials() {
  WifiCredentials creds;
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE_WIFI, /*readOnly=*/true)) {
    creds.valid = false;
    return creds;
  }

  creds.ssid     = prefs.getString("ssid", "");
  creds.password = prefs.getString("pass", "");
  creds.valid    = creds.ssid.length() > 0;

  prefs.end();
  return creds;
}

void Storage::saveWifiCredentials(const String &ssid, const String &password) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE_WIFI, /*readOnly=*/false)) return;

  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);

  prefs.end();
}

void Storage::clearWifiCredentials() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE_WIFI, /*readOnly=*/false)) return;
  prefs.clear();
  prefs.end();
}
