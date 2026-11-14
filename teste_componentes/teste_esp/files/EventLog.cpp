#include "EventLog.h"
#include "config.h"

void EventLog::add(EventType type, float weightG) {
  buffer_[head_] = { type, weightG, millis() };
  head_ = (head_ + 1) % EVENT_LOG_CAPACITY;
  if (count_ < EVENT_LOG_CAPACITY) count_++;
}

static const char *eventTypeName(EventType t) {
  switch (t) {
    case EventType::ACCESS_GRANTED:      return "access_granted";
    case EventType::ACCESS_BLOCKED:      return "access_blocked";
    case EventType::DOOR_OPENED_MANUAL:  return "door_opened_manual";
    case EventType::DOOR_CLOSED_MANUAL:  return "door_closed_manual";
    case EventType::CALIBRATION_DONE:    return "calibration_done";
    case EventType::FAULT_SAFE_STATE:    return "fault_safe_state";
    default:                              return "unknown";
  }
}

String EventLog::toJson(size_t maxItems) const {
  size_t n = min(maxItems, count_);
  String json = "[";

  // Percorre do mais antigo (dentro do limite pedido) para o mais recente.
  size_t start = (head_ + EVENT_LOG_CAPACITY - n) % EVENT_LOG_CAPACITY;
  for (size_t i = 0; i < n; i++) {
    size_t idx = (start + i) % EVENT_LOG_CAPACITY;
    const FeederEvent &e = buffer_[idx];
    if (i > 0) json += ",";
    json += "{\"type\":\"";
    json += eventTypeName(e.type);
    json += "\",\"weight_g\":";
    json += String(e.weightG, 1);
    json += ",\"t_ms\":";
    json += String(e.timestampMs);
    json += "}";
  }
  json += "]";
  return json;
}

void EventLog::clear() {
  count_ = 0;
  head_ = 0;
}
