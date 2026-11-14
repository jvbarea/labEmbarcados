/*
 * Simulador do firmware no PC.
 *
 * Compila os MESMOS arquivos de src/ (AppProtocol, AccessStateMachine,
 * EventLog, BleControl, DoorActuator, Storage, MockWeightSource) contra
 * substitutos do Arduino em stubs/, e faz o papel do celular: conecta pelo
 * "radio", manda as mensagens do protocolo v2 (em pedacos quando passam de 180
 * bytes, como o app), remonta as notificacoes e confere as respostas.
 *
 * O relogio e falso e anda 1 ms por volta do loop, entao 60 s de comedouro
 * rodam em menos de 1 s. NVS e LittleFS sobrevivem ao "reboot" (o teste destroi
 * e recria os objetos do firmware), o que permite testar a persistencia.
 *
 * Os cenarios espelham os testes do simulador do app
 * (app/__tests__/mockLink.test.ts) e acrescentam o que so existe no firmware:
 * RNF04, falha e rearme da porta, anel de 256 eventos, hora corrigida no hello,
 * pedacos com MTU 23, gravacao so quando a configuracao muda.
 *
 * Rodar: ver README.md desta pasta.
 */
#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <ESP32Servo.h>
#include <LittleFS.h>
#include <Preferences.h>

#include "config.h"
#include "AccessStateMachine.h"
#include "AppProtocol.h"
#include "BleControl.h"
#include "DoorActuator.h"
#include "EventLog.h"
#include "Hx711WeightSource.h"
#include "MockWeightSource.h"
#include "Storage.h"

#include <set>

// ------------------------------------------------------------- verificacoes

static int g_checks = 0;
static int g_fails = 0;

#define CHECK(cond, ...)                                  \
  do {                                                    \
    g_checks++;                                           \
    if (!(cond)) {                                        \
      g_fails++;                                          \
      std::printf("    FALHA (linha %d): ", __LINE__);   \
      std::printf(__VA_ARGS__);                           \
      std::printf("\n");                                  \
    }                                                     \
  } while (0)

static void titulo(const char *t) { std::printf("\n== %s\n", t); }

// --------------------------------------------------------------- comedouro

struct Device {
  Storage storage;
  EventLog log;
  MockWeightSource scale;
  DoorActuator door;
  AccessStateMachine fsm;
  BleControl ble;
  AppProtocol proto;
  BleMessage msg;
  bool freezeDoor = false;  // para de chamar door.update(): simula o loop travado

  // Mesma ordem do setup() de main.cpp (sem o HX711).
  void setup() {
    log.begin();
    AppConfig cfg = storage.loadAppConfig();
    scale.begin();
    door.begin(cfg.doorClosedDeg, cfg.doorOpenDeg, cfg.servoUs0, cfg.servoUs90);
    fsm.begin(&scale, &door, &log, cfg);
    ble.begin();
    proto.begin(&ble, &scale, &scale, nullptr, &door, &fsm, &log, &storage);
    log.addBoot();
  }

  // Mesma ordem do loop() de main.cpp.
  void loopOnce() {
    ble.update();
    scale.update();
    fsm.update();
    if (!freezeDoor) door.update();
    while (ble.popMessage(msg)) proto.handleMessage(msg.data, msg.len);
    proto.tick();
  }
};

static Device *dev = nullptr;

// ------------------------------------------------------------------ celular

static const uint32_t EPOCH0 = 1761400000;  // hora do "celular" no primeiro boot
static uint64_t g_elapsedBeforeBootUs = 0;

static uint32_t phoneNow() {
  return EPOCH0 + (uint32_t)((g_elapsedBeforeBootUs + sim::nowUs) / 1000000ULL);
}

struct Phone {
  std::vector<std::string> msgs;  // mensagens completas recebidas, na ordem
  size_t seen = 0;
  std::string partial;
  int next = 0;
  int total = 0;
  int nextId = 1;
  unsigned dropped = 0;

  void reset() {
    msgs.clear();
    seen = 0;
    partial.clear();
    next = total = 0;
  }

  void pump() {
    while (seen < sim::radio.notifications.size()) feed(sim::radio.notifications[seen++]);
  }

  // Mesmo algoritmo do app (app/src/device/framing.ts).
  void feed(const std::string &n) {
    if (n.empty()) return;
    if (n[0] != '#') {
      partial.clear();
      next = 0;
      msgs.push_back(n);
      return;
    }
    int i = 0, t = 0;
    const size_t colon = n.find(':');
    if (colon == std::string::npos || std::sscanf(n.c_str(), "#%d/%d:", &i, &t) != 2) {
      dropped++;
      next = 0;
      return;
    }
    if (i == 1) {
      partial.clear();
      total = t;
      next = 1;
    }
    if (next == 0 || i != next || t != total) {
      dropped++;
      next = 0;
      return;
    }
    partial += n.substr(colon + 1);
    next++;
    if (i == t) {
      msgs.push_back(partial);
      partial.clear();
      next = 0;
    }
  }

  // Escrita com resposta, como o app: um pedaco por vez, com o tempo de ida e
  // volta de um intervalo de conexao entre eles (o loop do firmware roda nesse meio).
  void sendRaw(const std::string &js, size_t chunk = 180);

  int send(JsonDocument doc, size_t chunk = 180) {
    int id;
    if (doc["id"].is<int>()) {
      id = doc["id"];
    } else {
      id = nextId++;
      doc["id"] = id;
    }
    String s;
    serializeJson(doc, s);
    sendRaw(s.str(), chunk);
    return id;
  }
};

static Phone phone;

// -------------------------------------------------------------- utilitarios

static void run(uint64_t ms) {
  for (uint64_t i = 0; i < ms; i++) {
    dev->loopOnce();
    sim::advance(1);
    phone.pump();
  }
}

static const uint64_t WRITE_RTT_MS = 8;  // ida e volta de uma escrita com resposta

void Phone::sendRaw(const std::string &js, size_t chunk) {
  if (js.size() <= chunk) {
    sim::bleWrite(js);
    run(WRITE_RTT_MS);
    return;
  }
  const size_t body = chunk - 7;
  const size_t n = (js.size() + body - 1) / body;
  for (size_t i = 0; i < n; i++) {
    sim::bleWrite("#" + std::to_string(i + 1) + "/" + std::to_string(n) + ":" +
                  js.substr(i * body, body));
    run(WRITE_RTT_MS);
  }
}

static JsonDocument J(const char *json) {
  JsonDocument d;
  if (deserializeJson(d, json)) std::printf("    [teste] JSON invalido no proprio teste: %s\n", json);
  return d;
}

static bool parse(const std::string &s, JsonDocument &d) {
  return !deserializeJson(d, s.c_str(), s.size());
}

static bool is(const JsonDocument &d, const char *t) {
  return std::strcmp(d["t"] | "", t) == 0;
}

static bool lastOf(const char *t, JsonDocument &out, size_t from = 0) {
  for (size_t i = phone.msgs.size(); i-- > from;) {
    JsonDocument d;
    if (parse(phone.msgs[i], d) && is(d, t)) {
      out = d;
      return true;
    }
  }
  return false;
}

static JsonDocument lastStatus() {
  JsonDocument st;
  lastOf("st", st);
  return st;
}

static std::string door() {
  JsonDocument st = lastStatus();
  return std::string(st["door"] | "?");
}

static int findAck(int id, JsonDocument *out = nullptr) {
  for (size_t i = 0; i < phone.msgs.size(); i++) {
    JsonDocument d;
    if (parse(phone.msgs[i], d) && is(d, "ack") && d["id"] == id) {
      if (out) *out = d;
      return (int)i;
    }
  }
  return -1;
}

// Manda e espera o ack, como o deviceStore do app.
static JsonDocument request(JsonDocument doc, size_t chunk = 180) {
  const int id = phone.send(doc, chunk);
  JsonDocument ack;
  for (int t = 0; t < 3000; t++) {
    if (findAck(id, &ack) >= 0) return ack;
    run(1);
  }
  ack["t"] = "ack";
  ack["id"] = id;
  ack["ok"] = false;
  ack["err"] = "SEM_RESPOSTA";
  return ack;
}

static bool ackOk(const JsonDocument &a) { return a["ok"] == true; }
static std::string ackErr(const JsonDocument &a) { return std::string(a["err"] | ""); }

static std::vector<JsonDocument> eventsSince(size_t from, const char *kind = nullptr) {
  std::vector<JsonDocument> out;
  for (size_t i = from; i < phone.msgs.size(); i++) {
    JsonDocument d;
    if (!parse(phone.msgs[i], d) || !is(d, "ev")) continue;
    if (kind && std::strcmp(d["kind"] | "", kind) != 0) continue;
    out.push_back(d);
  }
  return out;
}

struct SyncResult {
  bool ackFirst = false;
  bool ended = false;
  uint32_t last = 0;
  std::vector<JsonDocument> evs;  // sem repetidos, na ordem de chegada
};

static SyncResult syncSince(uint32_t since) {
  const size_t m0 = phone.msgs.size();
  JsonDocument d;
  d["t"] = "sync";
  d["since"] = since;
  const int id = phone.send(d);

  SyncResult r;
  int ackIdx = -1, endIdx = -1, firstEv = -1;
  for (int t = 0; t < 60000 && endIdx < 0; t++) {
    run(1);
    for (size_t i = m0; i < phone.msgs.size() && endIdx < 0; i++) {
      JsonDocument m;
      if (parse(phone.msgs[i], m) && is(m, "syncEnd") && m["id"] == id) endIdx = (int)i;
    }
  }
  std::set<uint32_t> vistos;
  for (size_t i = m0; i < phone.msgs.size(); i++) {
    JsonDocument m;
    if (!parse(phone.msgs[i], m)) continue;
    if (is(m, "ack") && m["id"] == id && ackIdx < 0) ackIdx = (int)i;
    if (is(m, "ev") && (endIdx < 0 || (int)i < endIdx)) {
      if (firstEv < 0) firstEv = (int)i;
      if (vistos.insert(m["seq"].as<uint32_t>()).second) r.evs.push_back(m);
    }
    if (is(m, "syncEnd") && m["id"] == id) r.last = m["last"].as<uint32_t>();
  }
  r.ended = endIdx >= 0;
  r.ackFirst = ackIdx >= 0 && (firstEv < 0 || ackIdx < firstEv);
  return r;
}

static void weight(float g) { dev->scale.setTargetWeightG(g); }

// ---------------------------------------------------------- ciclo de vida

static void factoryReset() {
  sim::nvs.clear();
  sim::files.clear();
  sim::fsMountFails = false;
}

// Liga (ou religa) a placa. NVS e arquivos ficam; RAM, relogio e radio zeram.
static void boot() {
  if (dev) {
    g_elapsedBeforeBootUs += sim::nowUs;
    delete dev;
  }
  sim::nowUs = 0;
  sim::radio = sim::Radio{};
  sim::servoAttached = false;
  sim::oversizedNotifications = 0;
  phone.reset();
  dev = new Device();
  dev->setup();
  run(600);  // homing da porta assenta
}

static void connect(uint16_t mtu = 185) {
  sim::bleConnect(mtu);
  run(5);
}

static JsonDocument hello() {
  JsonDocument d;
  d["t"] = "hello";
  d["v"] = 2;
  d["now"] = phoneNow();
  return request(d);
}

static const char *CATS =
    R"({"t":"cats","cats":[{"k":1,"minG":2700,"maxG":3700,"rule":"allow"},)"
    R"({"k":2,"minG":6000,"maxG":7000,"rule":"deny"}]})";

// Placa nova, conectada, com hello, os dois gatos e os tempos padrao.
static void readyDevice(uint16_t mtu = 185) {
  factoryReset();
  boot();
  connect(mtu);
  hello();
  request(J(CATS));
  request(J(R"({"t":"cfg","confMs":2000,"leaveMs":4000,"maxOpenS":300})"));
  run(200);
}

// Um gato sobe, fica onSecs segundos e desce; espera a visita fechar.
static void visit(float g, uint64_t onMs = 4000) {
  weight(g);
  run(onMs);
  weight(0);
  run(6000);
}

// ------------------------------------------------------------------ cenarios

static void testHandshake() {
  titulo("hello, status completo e o boot com hora corrigida");
  factoryReset();
  boot();
  connect();

  JsonDocument ack = hello();
  CHECK(ackOk(ack), "ack do hello");
  run(100);
  JsonDocument st = lastStatus();
  CHECK(st["wG"].is<int>() && st["cat"].is<int>() && st["fcC"].is<int>() && st["fcO"].is<int>() &&
            st["cfgV"].is<int>() && st["grant"].isNull() && st["mode"] == "auto",
        "campos do st");
  CHECK(door() == "closed", "porta fechada depois do homing (%s)", door().c_str());
  CHECK(!sim::servoAttached, "servo solto com a porta fechada");

  SyncResult s = syncSince(0);
  CHECK(s.ended && s.ackFirst, "sync: ack antes dos eventos e syncEnd no fim");
  CHECK(s.evs.size() == 1 && s.evs[0]["kind"] == "boot", "um evento de boot (%zu)", s.evs.size());
  if (!s.evs.empty()) {
    const uint32_t ts = s.evs[0]["ts"].as<uint32_t>();
    CHECK(ts >= EPOCH0 && ts <= phoneNow(), "ts do boot corrigido pelo hello (%u)", ts);
  }
  CHECK(s.last == 1, "syncEnd.last = 1 (%u)", s.last);
}

static void testCatsAndCfgV() {
  titulo("cats e cfg: grava so quando muda (cfgV e escritas na NVS)");
  factoryReset();
  boot();
  connect();
  hello();
  run(100);
  const int v0 = lastStatus()["cfgV"] | -1;

  CHECK(ackOk(request(J(CATS))), "ack do cats");
  run(150);
  const int v1 = lastStatus()["cfgV"] | -1;
  CHECK(v1 == v0 + 1, "cfgV subiu uma vez (%d -> %d)", v0, v1);

  const unsigned w0 = sim::nvsWrites;
  CHECK(ackOk(request(J(CATS))), "ack do cats repetido");
  CHECK(ackOk(request(J(R"({"t":"cfg","confMs":2000,"leaveMs":4000,"maxOpenS":300})"))),
        "ack do cfg igual ao padrao");
  CHECK(ackOk(request(J(R"({"t":"mode","mode":"auto"})"))), "ack do mode igual");
  run(150);
  CHECK(sim::nvsWrites == w0, "nada gravado na NVS (%u escritas)", sim::nvsWrites - w0);
  CHECK((lastStatus()["cfgV"] | -1) == v1, "cfgV parado");

  CHECK(ackErr(request(J(R"({"t":"cfg","confMs":100})"))) == "bad_range", "confMs fora da faixa");
  CHECK(ackErr(request(J(R"({"t":"cats","cats":[{"k":3,"minG":1,"maxG":2,"rule":"allow"}]})"))) ==
            "unknown_cat",
        "gato k=3");
  CHECK(ackErr(request(J(R"({"t":"cats","cats":[{"k":1,"minG":5,"maxG":2,"rule":"allow"}]})"))) ==
            "bad_range",
        "faixa invertida");
  CHECK(ackErr(request(J(R"({"t":"mode","mode":"xyz"})"))) == "bad_range", "modo invalido");
}

static void testAllowedVisit() {
  titulo("gato liberado: abre, registra a visita e fecha");
  readyDevice();
  const size_t m0 = phone.msgs.size();

  weight(3200);
  run(3000);
  JsonDocument st = lastStatus();
  CHECK(st["cat"] == 1, "gato 1 identificado (%d)", st["cat"].as<int>());
  CHECK(door() == "opening" || door() == "open", "porta abrindo (%s)", door().c_str());
  run(2000);
  CHECK(door() == "open" && lastStatus()["fcO"] == 1, "porta aberta (%s)", door().c_str());
  CHECK(sim::servoAttached, "servo segurando a porta aberta (DOOR_HOLD_OPEN)");

  weight(0);
  run(6000);
  auto vs = eventsSince(m0, "visit");
  CHECK(vs.size() == 1, "uma visita (%zu)", vs.size());
  if (!vs.empty()) {
    const JsonDocument &v = vs.back();
    CHECK(v["cat"] == 1 && v["door"] == "opened" && v["why"] == "rule", "visita cat 1 opened rule");
    CHECK(v["durS"].as<int>() >= 4 && v["durS"].as<int>() <= 6, "durS sem a espera de saida (%d)",
          v["durS"].as<int>());
    CHECK(std::abs(v["wG"].as<int>() - 3200) <= 5, "wG da visita (%d)", v["wG"].as<int>());
    CHECK(v["ts"].as<uint32_t>() >= EPOCH0, "visita com hora");
  }
  run(2500);
  CHECK(door() == "closed", "porta fechou depois da visita (%s)", door().c_str());
  CHECK(!sim::servoAttached, "servo solto depois de fechar");
}

static void testDeniedAndUnknown() {
  titulo("gato em dieta e peso desconhecido: porta nao abre");
  readyDevice();
  size_t m0 = phone.msgs.size();
  weight(6500);
  run(4000);
  CHECK(door() == "closed", "porta fechada para o gato 2 (%s)", door().c_str());
  CHECK(lastStatus()["cat"] == 2, "gato 2 identificado");
  weight(0);
  run(6000);
  auto vs = eventsSince(m0, "visit");
  CHECK(vs.size() == 1 && vs[0]["cat"] == 2 && vs[0]["door"] == "denied" && vs[0]["why"] == "rule",
        "visita negada do gato 2");

  m0 = phone.msgs.size();
  visit(1200);
  vs = eventsSince(m0, "visit");
  CHECK(vs.size() == 1 && vs[0]["cat"] == 0 && vs[0]["why"] == "unknown" && vs[0]["door"] == "denied",
        "visitante desconhecido");

  m0 = phone.msgs.size();
  weight(3200);
  run(800);  // menos que confMs
  weight(0);
  run(6000);
  CHECK(eventsSince(m0, "visit").empty(), "encostar e sair antes da decisao nao gera visita");
  CHECK(door() == "closed", "e a porta nem abriu");
}

static void testGrantVisits() {
  titulo("liberacao por 1 visita: abre uma vez e volta a negar");
  readyDevice();
  CHECK(ackOk(request(J(R"({"t":"grant","cat":2,"visits":1})"))), "ack do grant");
  run(150);
  JsonDocument st = lastStatus();
  CHECK(st["grant"]["cat"] == 2 && st["grant"]["visits"] == 1, "st.grant = {cat 2, visits 1}");

  size_t m0 = phone.msgs.size();
  weight(6500);
  run(4500);
  CHECK(door() == "open", "abriu para o gato 2 (%s)", door().c_str());
  CHECK(lastStatus()["grant"].isNull(), "liberacao consumida na decisao");
  weight(0);
  run(8000);
  auto vs = eventsSince(m0, "visit");
  CHECK(vs.size() == 1 && vs[0]["why"] == "grant" && vs[0]["door"] == "opened", "visita why=grant");

  m0 = phone.msgs.size();
  weight(6500);
  run(4000);
  CHECK(door() == "closed", "na visita seguinte nega de novo (%s)", door().c_str());
  weight(0);
  run(6000);
}

static void testGrantUntil() {
  titulo("liberacao por prazo: vale ate untilS e expira sozinha");
  readyDevice();
  JsonDocument g;
  g["t"] = "grant";
  g["cat"] = 2;
  g["untilS"] = phoneNow() + 60;
  CHECK(ackOk(request(g)), "ack do grant por prazo");
  run(150);
  CHECK(lastStatus()["grant"]["untilS"].as<uint32_t>() > EPOCH0, "st.grant.untilS");

  const size_t m0 = phone.msgs.size();
  visit(6500, 5000);
  auto vs = eventsSince(m0, "visit");
  CHECK(vs.size() == 1 && vs[0]["why"] == "grant", "abriu por causa da liberacao");
  CHECK(!lastStatus()["grant"].isNull(), "liberacao por prazo nao e consumida pela visita");
  run(52000);
  CHECK(lastStatus()["grant"].isNull(), "expirou depois do prazo");

  CHECK(ackOk(request(J(R"({"t":"grant","cat":2,"visits":2})"))), "nova liberacao");
  CHECK(ackOk(request(J(R"({"t":"revoke","cat":2})"))), "ack do revoke");
  run(150);
  CHECK(lastStatus()["grant"].isNull(), "revoke limpa");

  CHECK(ackErr(request(J(R"({"t":"grant","cat":3,"visits":1})"))) == "unknown_cat", "gato 3");
  request(J(R"({"t":"cats","cats":[{"k":1,"minG":2700,"maxG":3700,"rule":"allow"}]})"));
  CHECK(ackErr(request(J(R"({"t":"grant","cat":2,"visits":1})"))) == "unknown_cat",
        "gato 2 sem cadastro");
}

static void testNoClock() {
  titulo("liberacao por prazo antes do hello: no_clock");
  factoryReset();
  boot();
  connect();
  request(J(CATS));
  JsonDocument g;
  g["t"] = "grant";
  g["cat"] = 2;
  g["untilS"] = phoneNow() + 60;
  CHECK(ackErr(request(g)) == "no_clock", "sem hora nao da para prazo");
  CHECK(ackOk(request(J(R"({"t":"grant","cat":2,"visits":1})"))), "por visitas funciona sem hora");
}

static void testLearnMode() {
  titulo("modo adaptacao: abre para qualquer gato e marca learn");
  readyDevice();
  CHECK(ackOk(request(J(R"({"t":"mode","mode":"learn"})"))), "ack do mode learn");
  run(2000);
  CHECK(lastStatus()["mode"] == "learn", "st.mode = learn");
  if (LEARN_DOOR_STAYS_OPEN) {
    CHECK(door() == "open", "porta aberta o modo inteiro (%s)", door().c_str());
  } else {
    CHECK(door() == "closed", "porta fechada entre visitas (servo segurando a tampa) (%s)",
          door().c_str());
  }

  const size_t m0 = phone.msgs.size();
  weight(6500);  // o gato em dieta tambem come
  run(4500);
  CHECK(door() == "open", "abriu para o gato 2 (%s)", door().c_str());
  weight(0);
  run(6000);
  auto vs = eventsSince(m0, "visit");
  CHECK(vs.size() == 1 && vs[0]["cat"] == 2 && vs[0]["door"] == "opened" && vs[0]["why"] == "learn" &&
            vs[0]["learn"] == 1,
        "visita learn do gato 2");
  run(2500);
  CHECK(door() == (LEARN_DOOR_STAYS_OPEN ? "open" : "closed"), "depois da visita (%s)",
        door().c_str());

  CHECK(ackOk(request(J(R"({"t":"mode","mode":"auto"})"))), "ack do mode auto");
  run(2500);
  CHECK(lastStatus()["mode"] == "auto" && door() == "closed", "volta ao automatico fechada");
}

static void testRefillAndRnf04() {
  titulo("reabastecer: visitas continuam, porta so fecha no prazo e nunca com gato em cima");
  readyDevice();
  CHECK(lastStatus()["refillS"].isNull(), "sem refillS fora do reabastecer");
  CHECK(ackOk(request(J(R"({"t":"cmd","op":"refill","timeoutS":30})"))), "ack do refill");
  run(3000);
  CHECK(door() == "open", "porta aberta para reabastecer (%s)", door().c_str());
  {
    const int falta = lastStatus()["refillS"] | -1;
    CHECK(falta >= 26 && falta <= 28, "st.refillS conta o tempo que falta (%d)", falta);
  }

  size_t m0 = phone.msgs.size();
  visit(6500);
  auto vs = eventsSince(m0, "visit");
  CHECK(vs.size() == 1 && vs[0]["door"] == "denied", "visita do gato 2 registrada como negada");
  CHECK(door() == "open", "porta continua aberta durante o reabastecer (%s)", door().c_str());

  // Gato em cima quando o prazo acaba: espera ele sair (RNF04).
  weight(6500);
  run(20000);
  CHECK(door() == "open", "prazo acabou com gato em cima: nao fecha (%s)", door().c_str());
  CHECK(!eventsSince(m0, "refill").empty(), "evento refill no fim do prazo");
  weight(0);
  run(3000);
  CHECK(door() == "open", "esperando leaveMs com a plataforma vazia (%s)", door().c_str());
  run(4000);
  CHECK(door() == "closing" || door() == "closed", "fechou depois que ele saiu (%s)", door().c_str());
  run(2500);

  // "Fechar agora" com gato em cima: ack na hora, fecha quando sair.
  request(J(R"({"t":"cmd","op":"refill","timeoutS":300})"));
  run(3000);
  weight(6500);
  run(3000);
  CHECK(ackOk(request(J(R"({"t":"cmd","op":"close"})"))), "ack do close com gato em cima");
  run(3000);
  CHECK(door() == "open", "fechamento pendente (%s)", door().c_str());
  weight(0);
  run(7000);
  CHECK(door() == "closing" || door() == "closed", "fechou depois de vazia (%s)", door().c_str());
}

static void testDoorFault() {
  titulo("porta em falha: registra uma vez, recusa refill, close rearma");
  readyDevice();
  const size_t m0 = phone.msgs.size();
  request(J(R"({"t":"cmd","op":"refill","timeoutS":300})"));
  dev->freezeDoor = true;  // a porta nao chega: passa do DOOR_MOVE_TIMEOUT_MS
  run(DOOR_MOVE_TIMEOUT_MS + 500);
  dev->freezeDoor = false;
  run(500);
  CHECK(door() == "fault", "porta em falha (%s)", door().c_str());
  run(3000);
  auto fs = eventsSince(m0, "fault");
  CHECK(fs.size() == 1 && fs[0]["err"] == "door_timeout", "um evento de falha door_timeout (%zu)",
        fs.size());

  CHECK(ackErr(request(J(R"({"t":"cmd","op":"refill","timeoutS":300})"))) == "door_fault",
        "refill recusado em falha");

  // Visita durante a falha: registrada, sem abrir.
  const size_t m1 = phone.msgs.size();
  visit(3200);
  auto vs = eventsSince(m1, "visit");
  CHECK(vs.size() == 1 && vs[0]["door"] == "denied", "visita anotada durante a falha");

  CHECK(ackOk(request(J(R"({"t":"cmd","op":"close"})"))), "ack do close (rearme)");
  run(3000);
  CHECK(door() == "closed", "rearmada e fechada (%s)", door().c_str());
  CHECK(eventsSince(m0, "fault").size() == 1, "nao repetiu o evento de falha");
}

static void testSensorFault() {
  titulo("sensor mudo: falha registrada uma vez e recuperacao");
  readyDevice();
  const size_t m0 = phone.msgs.size();
  CHECK(ackOk(request(J(R"({"t":"cmd","c":"sensor","ok":false})"))), "ack do sensor off");
  run(3000);
  auto fs = eventsSince(m0, "fault");
  CHECK(fs.size() == 1 && fs[0]["err"] == "sensor_fault", "evento sensor_fault (%zu)", fs.size());
  CHECK(dev->fsm.state() == AccessState::SAFE_FAULT, "estado seguro");
  request(J(R"({"t":"cmd","c":"sensor","ok":true})"));
  run(500);
  CHECK(dev->fsm.state() == AccessState::IDLE, "voltou ao normal");
  visit(3200);
  CHECK(eventsSince(m0, "visit").size() == 1, "e volta a registrar visitas");
}

static void testSyncAndRing() {
  titulo("sync completo e o anel de 256 eventos");
  readyDevice();
  for (int i = 0; i < 300; i++) dev->log.addRefill(1);
  const uint32_t last = dev->log.lastSeq();
  SyncResult s = syncSince(0);
  CHECK(s.ended && s.ackFirst, "ack, eventos, syncEnd");
  CHECK(s.evs.size() == EVENT_LOG_CAPACITY, "%u eventos (o anel guarda %d)", (unsigned)s.evs.size(),
        EVENT_LOG_CAPACITY);
  bool contiguos = !s.evs.empty();
  for (size_t i = 1; i < s.evs.size(); i++) {
    if (s.evs[i]["seq"].as<uint32_t>() != s.evs[i - 1]["seq"].as<uint32_t>() + 1) contiguos = false;
  }
  CHECK(contiguos && !s.evs.empty() && s.evs.back()["seq"].as<uint32_t>() == last,
        "seqs contiguos terminando no ultimo");
  CHECK(s.last == last, "syncEnd.last = %u (%u)", last, s.last);

  SyncResult s2 = syncSince(last - 5);
  CHECK(s2.evs.size() == 5, "sync parcial manda so o que falta (%zu)", s2.evs.size());
  SyncResult s3 = syncSince(last + 100);
  CHECK(s3.ended && s3.evs.empty(), "since maior que o ultimo: nada, mas termina");
  CHECK(sim::oversizedNotifications == 0, "nenhuma notificacao passou do MTU");
}

static void testPersistenceAndTsFix() {
  titulo("reboot: configuracao, perfil e historico ficam; seq continua; hora corrigida");
  readyDevice();
  const char *perfil =
      "[{\"id\":1,\"name\":\"Mimi\",\"emoji\":\"\xF0\x9F\x90\xB1\",\"color\":\"laranja\"},"
      "{\"id\":2,\"name\":\"Bolota\",\"emoji\":\"\xF0\x9F\x90\x88\",\"color\":\"azul\"}]";
  JsonDocument p;
  p["t"] = "profile";
  p["blob"] = perfil;
  CHECK(ackOk(request(p)), "ack do profile");
  request(J(R"({"t":"mode","mode":"learn"})"));
  visit(3200);
  const uint32_t lastAntes = dev->log.lastSeq();
  const uint32_t nAntes = (uint32_t)dev->log.count();

  boot();  // queda de energia
  // Antes de o celular conectar: um evento nasce sem hora.
  dev->log.addRefill(7);
  connect();
  hello();
  request(J(R"({"t":"cfgGet"})"));
  JsonDocument dump;
  CHECK(lastOf("cfgDump", dump), "cfgDump recebido");
  CHECK(dump["cats"].size() == 2 && dump["cats"][1]["rule"] == "deny", "gatos voltaram");
  CHECK(dump["mode"] == "learn", "modo adaptacao sobreviveu ao reboot");
  CHECK(std::strcmp(dump["profile"] | "", perfil) == 0, "perfil com emoji voltou igual");
  CHECK(dump["confMs"] == 2000 && dump["leaveMs"] == 4000, "tempos voltaram");

  SyncResult s = syncSince(0);
  CHECK(s.evs.size() == nAntes + 2, "eventos de antes + boot + refill (%zu)", s.evs.size());
  bool comHora = true;
  for (auto &e : s.evs) {
    if (e["ts"].as<uint32_t>() < EPOCH0) comHora = false;
  }
  CHECK(s.evs.size() >= 2 && s.evs[s.evs.size() - 2]["kind"] == "boot" &&
            s.evs[s.evs.size() - 2]["seq"].as<uint32_t>() == lastAntes + 1,
        "boot novo com seq %u", lastAntes + 1);
  CHECK(comHora, "todos com hora (os deste boot corrigidos no hello)");
  request(J(R"({"t":"mode","mode":"auto"})"));
}

static void testFragmentsMtu23() {
  titulo("MTU 23: tudo em pedacos de 20 bytes, nos dois sentidos");
  readyDevice(23);
  const char *perfil =
      "[{\"id\":1,\"name\":\"Jo\xC3\xA3o Gato\",\"emoji\":\"\xF0\x9F\x90\x88\xE2\x80\x8D\xE2\xAC\x9B\","
      "\"color\":\"roxo\"},{\"id\":2,\"name\":\"Bolota\",\"emoji\":\"\xF0\x9F\x90\xAF\",\"color\":\"azul\"}]";
  JsonDocument p;
  p["t"] = "profile";
  p["blob"] = perfil;
  CHECK(ackOk(request(p, 20)), "profile chegou remontado de pedacos de 20 bytes");
  request(J(R"({"t":"cfgGet"})"), 20);
  run(2000);
  JsonDocument dump;
  CHECK(lastOf("cfgDump", dump), "cfgDump remontado");
  CHECK(std::strcmp(dump["profile"] | "", perfil) == 0, "perfil identico (emoji cortado no meio)");
  CHECK(sim::oversizedNotifications == 0, "nenhuma notificacao passou de 20 bytes (%u)",
        sim::oversizedNotifications);
  CHECK(phone.dropped == 0, "nenhum pedaco perdido (%u)", phone.dropped);
  const size_t m0 = phone.msgs.size();
  visit(3200);
  CHECK(eventsSince(m0, "visit").size() == 1, "visita normal com MTU minimo");
}

static void testBadInput() {
  titulo("entrada ruim: JSON quebrado, tipo desconhecido, pedacos fora de ordem");
  readyDevice();
  const size_t m0 = phone.msgs.size();
  phone.sendRaw("{quebrado");
  run(50);
  JsonDocument a;
  CHECK(findAck(0, &a) >= 0 && ackErr(a) == "bad_json", "bad_json com id 0");
  CHECK(ackErr(request(J(R"({"t":"xyz","id":90077})"))) == "bad_type", "bad_type");
  CHECK(ackErr(request(J(R"({"t":"cmd","op":"voar"})"))) == "bad_op", "bad_op");

  sim::bleWrite("#2/2:{\"t\":\"cfgGet\",\"id\":90501}");  // sem o primeiro pedaco
  sim::bleWrite("#1/3:{\"t\":\"cfgGet\",");
  sim::bleWrite("#3/3:\"id\":90502}");  // faltou o 2
  run(100);
  CHECK(findAck(90501) < 0 && findAck(90502) < 0, "pedacos incompletos descartados");
  CHECK(ackOk(request(J(R"({"t":"cfgGet"})"))), "a mensagem seguinte funciona");

  // Rajada sem esperar resposta (WRITE_NR): cabe na fila de entrada.
  for (int i = 0; i < BLE_RX_QUEUE_LEN; i++) {
    sim::bleWrite("{\"t\":\"cmd\",\"c\":\"status\",\"id\":" + std::to_string(91000 + i) + "}");
  }
  run(1500);  // 16 acks + 16 status saindo a cada BLE_TX_GAP_MS
  int respondidas = 0;
  for (int i = 0; i < BLE_RX_QUEUE_LEN; i++) respondidas += findAck(91000 + i) >= 0 ? 1 : 0;
  CHECK(respondidas == BLE_RX_QUEUE_LEN, "rajada de %d escritas: %d respondidas", BLE_RX_QUEUE_LEN,
        respondidas);
  (void)m0;
}

static void testStatusCadence() {
  titulo("status a cada 500 ms e na hora quando muda");
  readyDevice();
  run(1000);
  size_t m0 = phone.msgs.size();
  run(5000);
  int n = 0;
  for (size_t i = m0; i < phone.msgs.size(); i++) {
    JsonDocument d;
    if (parse(phone.msgs[i], d) && is(d, "st")) n++;
  }
  CHECK(n >= 9 && n <= 11, "%d status em 5 s parado", n);

  const int id = phone.send(J(R"({"t":"cmd","op":"refill","timeoutS":60})"));
  int ackAt = -1, stAt = -1;
  for (int t = 0; t < 400 && stAt < 0; t++) {
    run(1);
    if (ackAt < 0 && findAck(id) >= 0) ackAt = t;
    if (ackAt >= 0) {
      JsonDocument st = lastStatus();
      if (st["door"] == "opening") stAt = t;
    }
  }
  CHECK(stAt >= 0 && stAt - ackAt <= 150, "status 'opening' %d ms depois do ack", stAt - ackAt);
}

static void testEvclearAndBench() {
  titulo("evclear mantem o seq; sem gatos vale a janela de bancada");
  factoryReset();
  boot();
  connect();
  hello();
  size_t m0 = phone.msgs.size();
  visit(4200);  // sem gatos cadastrados: janela 3000..8000
  auto vs = eventsSince(m0, "visit");
  CHECK(vs.size() == 1 && vs[0]["door"] == "opened", "janela de bancada abre para 4200 g");
  m0 = phone.msgs.size();
  visit(12000);
  vs = eventsSince(m0, "visit");
  CHECK(vs.size() == 1 && vs[0]["door"] == "denied", "e nega 12 kg");

  // evtrim: o teste de bancada apaga so o que ele mesmo criou.
  const uint32_t antesDoTeste = dev->log.lastSeq();
  const size_t guardados = dev->log.count();
  dev->log.addRefill(1);
  dev->log.addRefill(2);
  JsonDocument trim;
  trim["t"] = "cmd";
  trim["c"] = "evtrim";
  trim["after"] = antesDoTeste;
  CHECK(ackOk(request(trim)), "ack do evtrim");
  SyncResult st = syncSince(0);
  CHECK(st.evs.size() == guardados && st.last == antesDoTeste + 2,
        "evtrim tirou so os 2 do teste (%zu de %zu), last %u", st.evs.size(), guardados, st.last);

  const uint32_t last = dev->log.lastSeq();
  CHECK(ackOk(request(J(R"({"t":"cmd","c":"evclear"})"))), "ack do evclear");
  SyncResult s = syncSince(0);
  CHECK(s.ended && s.evs.empty() && s.last == last, "historico vazio, last continua %u (%u)", last,
        s.last);
  boot();
  CHECK(dev->log.lastSeq() == last + 1, "depois do reboot o seq segue (%u)", dev->log.lastSeq());
}

static void testNoFilesystem() {
  titulo("LittleFS que nao monta: historico so na RAM, sem travar");
  factoryReset();
  sim::fsMountFails = true;
  boot();
  connect();
  hello();
  CHECK(!dev->log.persistent(), "sabe que nao esta gravando");
  SyncResult s = syncSince(0);
  CHECK(s.ended && s.evs.size() == 1, "boot na RAM sincroniza (%zu)", s.evs.size());
  sim::fsMountFails = false;
}

int main(int argc, char **argv) {
  sim::quietSerial = !(argc > 1 && std::strcmp(argv[1], "-v") == 0);

  testHandshake();
  testCatsAndCfgV();
  testAllowedVisit();
  testDeniedAndUnknown();
  testGrantVisits();
  testGrantUntil();
  testNoClock();
  testLearnMode();
  testRefillAndRnf04();
  testDoorFault();
  testSensorFault();
  testSyncAndRing();
  testPersistenceAndTsFix();
  testFragmentsMtu23();
  testBadInput();
  testStatusCadence();
  testEvclearAndBench();
  testNoFilesystem();

  std::printf("\n%d verificacoes, %d falha(s)\n", g_checks, g_fails);
  std::printf(g_fails == 0 ? "TUDO PASSOU\n" : "HA FALHAS\n");
  return g_fails == 0 ? 0 : 1;
}
