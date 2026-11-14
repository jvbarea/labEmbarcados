#include "HttpApi.h"
#include "config.h"

namespace {
const char *doorStateName(DoorState s) {
  switch (s) {
    case DoorState::CLOSED:  return "closed";
    case DoorState::OPENING: return "opening";
    case DoorState::OPEN:    return "open";
    case DoorState::CLOSING: return "closing";
    case DoorState::FAULT:   return "fault";
    default: return "unknown";
  }
}

const char *accessStateName(AccessState s) {
  switch (s) {
    case AccessState::IDLE:            return "idle";
    case AccessState::STABILIZING:     return "stabilizing";
    case AccessState::EVALUATING:      return "evaluating";
    case AccessState::ACCESS_OPEN:     return "access_open";
    case AccessState::ACCESS_BLOCKED:  return "access_blocked";
    case AccessState::VACATE_CONFIRM:  return "vacate_confirm";
    case AccessState::SAFE_FAULT:      return "safe_fault";
    default: return "unknown";
  }
}
}  // namespace

void HttpApi::begin(Storage *storage, WeightSensor *sensor, DoorActuator *door,
                     AccessStateMachine *stateMachine, EventLog *log) {
  storage_ = storage;
  sensor_ = sensor;
  door_ = door;
  stateMachine_ = stateMachine;
  log_ = log;

  server_.on("/api/status", HTTP_GET, [this]() { handleGetStatus(); });
  server_.on("/api/config", HTTP_GET, [this]() { handleGetConfig(); });
  server_.on("/api/config", HTTP_POST, [this]() { handlePostConfig(); });
  server_.on("/api/door", HTTP_POST, [this]() { handlePostDoor(); });
  server_.on("/api/calibrate", HTTP_POST, [this]() { handlePostCalibrate(); });
  server_.on("/api/events", HTTP_GET, [this]() { handleGetEvents(); });
  server_.onNotFound([this]() { handleNotFound(); });

  server_.begin();
  Serial.println("[HTTP] Servidor iniciado.");
}

void HttpApi::handleClient() {
  server_.handleClient();
}

void HttpApi::sendJson(int code, const String &json) {
  server_.send(code, "application/json", json);
}

void HttpApi::sendError(int code, const String &message) {
  StaticJsonDocument<128> doc;
  doc["error"] = message;
  String out;
  serializeJson(doc, out);
  sendJson(code, out);
}

void HttpApi::handleGetStatus() {
  StaticJsonDocument<256> doc;
  doc["weight_g"]     = sensor_->filteredWeightG();
  doc["stable"]       = sensor_->isStable();
  doc["sensor_ready"] = sensor_->isReady();
  doc["door_state"]   = doorStateName(door_->state());
  doc["access_state"] = accessStateName(stateMachine_->state());
  doc["manual_override"] = stateMachine_->manualOverride();

  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

void HttpApi::handleGetConfig() {
  AppConfig cfg = storage_->loadAppConfig();

  StaticJsonDocument<256> doc;
  doc["weight_min_g"]       = cfg.weightMinG;
  doc["weight_max_g"]       = cfg.weightMaxG;
  doc["stabilize_ms"]       = cfg.stabilizeMs;
  doc["vacate_confirm_ms"]  = cfg.vacateConfirmMs;
  doc["calibration_factor"] = cfg.calibrationFactor;

  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

void HttpApi::handlePostConfig() {
  if (!server_.hasArg("plain")) {
    sendError(400, "corpo JSON ausente");
    return;
  }

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, server_.arg("plain"));
  if (err) {
    sendError(400, "JSON inválido");
    return;
  }

  // RF05: atualiza somente os campos genéricos enviados; os demais mantêm
  // o valor atualmente persistido.
  AppConfig cfg = storage_->loadAppConfig();
  if (doc.containsKey("weight_min_g"))      cfg.weightMinG      = doc["weight_min_g"].as<float>();
  if (doc.containsKey("weight_max_g"))      cfg.weightMaxG      = doc["weight_max_g"].as<float>();
  if (doc.containsKey("stabilize_ms"))      cfg.stabilizeMs      = doc["stabilize_ms"].as<uint32_t>();
  if (doc.containsKey("vacate_confirm_ms")) cfg.vacateConfirmMs  = doc["vacate_confirm_ms"].as<uint32_t>();

  if (cfg.weightMinG > cfg.weightMaxG) {
    sendError(400, "weight_min_g não pode ser maior que weight_max_g");
    return;
  }

  storage_->saveAppConfig(cfg);       // RF10: persiste em flash
  stateMachine_->applyConfig(cfg);    // aplica imediatamente, sem precisar reiniciar

  sendJson(200, "{\"result\":\"ok\"}");
}

void HttpApi::handlePostDoor() {
  if (!server_.hasArg("plain")) {
    sendError(400, "corpo JSON ausente, esperado {\"action\":\"open|close\"}");
    return;
  }

  StaticJsonDocument<64> doc;
  DeserializationError err = deserializeJson(doc, server_.arg("plain"));
  if (err || !doc.containsKey("action")) {
    sendError(400, "JSON inválido, esperado {\"action\":\"open|close\"}");
    return;
  }

  String action = doc["action"].as<String>();
  if (action == "open") {
    stateMachine_->manualOpen();
  } else if (action == "close") {
    stateMachine_->manualClose();
  } else {
    sendError(400, "action deve ser 'open' ou 'close'");
    return;
  }

  sendJson(200, "{\"result\":\"ok\"}");
}

void HttpApi::handlePostCalibrate() {
  if (!server_.hasArg("plain")) {
    sendError(400, "corpo JSON ausente, esperado {\"action\":\"tare\"} ou {\"action\":\"calibrate\",\"known_mass_g\":...}");
    return;
  }

  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, server_.arg("plain"));
  if (err || !doc.containsKey("action")) {
    sendError(400, "JSON inválido");
    return;
  }

  String action = doc["action"].as<String>();
  if (action == "tare") {
    sensor_->tare();
  } else if (action == "calibrate") {
    if (!doc.containsKey("known_mass_g")) {
      sendError(400, "campo known_mass_g é obrigatório para calibrate");
      return;
    }
    sensor_->calibrateWithKnownMass(doc["known_mass_g"].as<float>());
  } else {
    sendError(400, "action deve ser 'tare' ou 'calibrate'");
    return;
  }

  // RF10: persiste o novo offset/fator de calibração.
  AppConfig cfg = storage_->loadAppConfig();
  cfg.tareOffset         = sensor_->tareOffset();
  cfg.calibrationFactor = sensor_->calibrationFactor();
  storage_->saveAppConfig(cfg);

  if (log_) log_->add(EventType::CALIBRATION_DONE, sensor_->filteredWeightG());

  sendJson(200, "{\"result\":\"ok\"}");
}

void HttpApi::handleGetEvents() {
  String json = log_ ? log_->toJson() : "[]";
  sendJson(200, json);
}

void HttpApi::handleNotFound() {
  sendError(404, "rota não encontrada");
}
