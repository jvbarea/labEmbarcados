#include "Storage.h"
#include <Preferences.h>

AppConfig Storage::loadAppConfig() {
  AppConfig cfg;  // ja nasce com os padroes de config.h
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE_APP, /*readOnly=*/true)) return cfg;

  cfg.confMs        = prefs.getUInt("confMs", cfg.confMs);
  cfg.leaveMs       = prefs.getUInt("leaveMs", cfg.leaveMs);
  cfg.maxOpenS      = prefs.getUInt("maxOpenS", cfg.maxOpenS);
  cfg.doorClosedDeg = prefs.getInt("dClose", cfg.doorClosedDeg);
  cfg.doorOpenDeg   = prefs.getInt("dOpen", cfg.doorOpenDeg);
  cfg.servoUs0      = prefs.getInt("sUs0", cfg.servoUs0);
  cfg.servoUs90     = prefs.getInt("sUs90", cfg.servoUs90);
  cfg.learnMode     = prefs.getBool("learn", cfg.learnMode);
  cfg.cfgV          = prefs.getUInt("cfgV", cfg.cfgV);
  cfg.benchMinG     = prefs.getFloat("wMin", cfg.benchMinG);
  cfg.benchMaxG     = prefs.getFloat("wMax", cfg.benchMaxG);
  cfg.calibFactor   = prefs.getFloat("calib", cfg.calibFactor);
  cfg.tareOffset    = prefs.getLong("tare", cfg.tareOffset);

  // Os gatos vao num blob unico: sao poucos e de tamanho fixo.
  size_t lidos = prefs.getBytes("cats", cfg.cats, sizeof(cfg.cats));
  cfg.catCount = prefs.getUChar("catN", 0);
  if (lidos != sizeof(cfg.cats) || cfg.catCount > MAX_CATS) cfg.catCount = 0;

  prefs.end();
  return cfg;
}

void Storage::saveAppConfig(AppConfig &cfg) {
  cfg.cfgV++;  // o app usa isso para saber que a configuracao mudou

  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE_APP, /*readOnly=*/false)) return;

  prefs.putUInt("confMs", cfg.confMs);
  prefs.putUInt("leaveMs", cfg.leaveMs);
  prefs.putUInt("maxOpenS", cfg.maxOpenS);
  prefs.putInt("dClose", cfg.doorClosedDeg);
  prefs.putInt("dOpen", cfg.doorOpenDeg);
  prefs.putInt("sUs0", cfg.servoUs0);
  prefs.putInt("sUs90", cfg.servoUs90);
  prefs.putBool("learn", cfg.learnMode);
  prefs.putUInt("cfgV", cfg.cfgV);
  prefs.putFloat("wMin", cfg.benchMinG);
  prefs.putFloat("wMax", cfg.benchMaxG);
  prefs.putFloat("calib", cfg.calibFactor);
  prefs.putLong("tare", cfg.tareOffset);
  prefs.putBytes("cats", cfg.cats, sizeof(cfg.cats));
  prefs.putUChar("catN", cfg.catCount);

  prefs.end();
}

String Storage::loadProfile() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE_APP, /*readOnly=*/true)) return String();
  String blob = prefs.getString("profile", "");
  prefs.end();
  return blob;
}

void Storage::saveProfile(const String &blob) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE_APP, /*readOnly=*/false)) return;
  prefs.putString("profile", blob);
  prefs.end();
}

bool Storage::importarCalibracaoDaBancada(AppConfig &cfg) {
  if (cfg.calibFactor != 0.0f) return false;  // ja tem a propria

  Preferences prefs;
  if (!prefs.begin("hx711-cal", /*readOnly=*/true)) return false;

  float fator  = prefs.getFloat("scl", 0.0f);
  long  offset = prefs.getLong("off", 0);
  prefs.end();

  if (fator == 0.0f) return false;

  cfg.calibFactor = fator;
  cfg.tareOffset  = offset;
  saveAppConfig(cfg);
  return true;
}
