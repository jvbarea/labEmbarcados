/*
 * Alimentador de Animais - firmware de teste
 * BLE (protocolo Feed Cat v2) + servo posicional + balanca mockada
 *
 * Fase 1 do doc 07 / Etapa 2 do plano: so BLE, sem Wi-Fi e sem HTTP. A balanca
 * e simulada, entao da para exercitar a maquina de estados inteira e ver a
 * porta abrir de verdade, sem celula de carga.
 *
 * O protocolo vive em AppProtocol.cpp; aqui fica so a fiacao dos modulos e o
 * menu serial de bancada.
 */
#include <Arduino.h>

#include "config.h"
#include "Storage.h"
#include "EventLog.h"
#include "IWeightSource.h"
#include "MockWeightSource.h"
#include "Hx711WeightSource.h"
#include "DoorActuator.h"
#include "AccessStateMachine.h"
#include "BleControl.h"
#include "AppProtocol.h"

#ifndef MONITOR_SPEED
#define MONITOR_SPEED 115200
#endif

Storage storage;
EventLog eventLog;

/*
 * A balanca: real ou simulada, conforme USE_HX711 em config.h. Tudo o que esta
 * acima dela - AccessStateMachine, AppProtocol, o app - fala com a interface
 * IWeightSource e nao sabe qual das duas esta por tras.
 */
#if USE_HX711
Hx711WeightSource balanca;
Hx711WeightSource *hxPtr = &balanca;
MockWeightSource  *mockPtr = nullptr;
#else
MockWeightSource balanca;
Hx711WeightSource *hxPtr = nullptr;
MockWeightSource  *mockPtr = &balanca;
#endif
IWeightSource *scale = &balanca;

DoorActuator door;
AccessStateMachine accessFsm;  // nao chamar de 'access': colide com access() do unistd.h
BleControl ble;
AppProtocol proto;

static char linha[96];
static size_t linhaLen = 0;

// --------------------------------------------------------- comandos Serial

static void ajuda() {
  const AppConfig &c = accessFsm.config();
  Serial.println();
  Serial.printf("=== Alimentador - BLE v2 + servo + balanca %s ===\n",
                hxPtr ? "REAL (HX711)" : "simulada");
  Serial.println("  ?              esta ajuda");
  if (mockPtr) {
    Serial.println("  w <gramas>     define o peso na balanca simulada (ex.: w 4200)");
  } else {
    Serial.println("  tara           zera a balanca (plataforma vazia)");
    Serial.println("  cal <gramas>   calibra com a massa conhecida no centro");
  }
  Serial.println("  st             imprime o status agora");
  Serial.println("  open / close   reabastecer / fechar");
  Serial.println("  reset          rearma depois de uma falha");
  Serial.println("  min <g>        limite inferior da janela de bancada");
  Serial.println("  max <g>        limite superior da janela de bancada");
  if (mockPtr) Serial.println("  sensor off|on  simula sensor mudo / recuperado");
  Serial.println("  ev             ultimos eventos");
  Serial.println("  --- calibracao da porta ---");
  Serial.println("  cal on|off     entra/sai do modo calibracao (a FSM para)");
  Serial.println("  pos <graus>    move o servo para o angulo (so em cal on)");
  Serial.println("  setclosed      grava a posicao atual como PORTA FECHADA");
  Serial.println("  setopen        grava a posicao atual como PORTA ABERTA");
  Serial.printf("  janela de bancada: %.0f a %.0f g | confMs %lu | leaveMs %lu\n",
                c.benchMinG, c.benchMaxG,
                (unsigned long)c.confMs, (unsigned long)c.leaveMs);
  Serial.printf("  porta: fechada=%d graus (%d us), aberta=%d graus (%d us), GPIO %d\n",
                c.doorClosedDeg, doorDegToUs(c.doorClosedDeg),
                c.doorOpenDeg, doorDegToUs(c.doorOpenDeg), PIN_SERVO);
  Serial.printf("  gatos cadastrados: %u | cfgV %lu | modo %s\n",
                (unsigned)c.catCount, (unsigned long)c.cfgV,
                c.learnMode ? "learn" : "auto");
  Serial.println();
}

static void imprimeEventos() {
  FeederEvent itens[8];
  size_t n = eventLog.since(0, itens, 8);
  if (n == 0) { Serial.println("[ev] nenhum evento ainda"); return; }
  for (size_t i = 0; i < n; i++) {
    const FeederEvent &e = itens[i];
    Serial.printf("[ev] seq=%lu ts=%lu %s", (unsigned long)e.seq,
                  (unsigned long)e.ts, evKindName(e.kind));
    if (e.kind == EvKind::VISIT) {
      Serial.printf(" cat=%u wG=%ld durS=%lu %s", (unsigned)e.cat, (long)e.wG,
                    (unsigned long)e.durS, e.opened ? "opened" : "denied");
      const char *why = evWhyName(e.why);
      if (why) Serial.printf(" why=%s", why);
    } else if (e.kind == EvKind::REFILL) {
      Serial.printf(" durS=%lu", (unsigned long)e.durS);
    } else if (e.kind == EvKind::FAULT) {
      const char *err = evErrName(e.err);
      if (err) Serial.printf(" err=%s", err);
    }
    Serial.println();
  }
}

static void handleSerial(char *buf) {
  String s(buf);
  s.trim();
  if (s.length() == 0) return;

  String cmd = s, arg = "";
  int esp = s.indexOf(' ');
  if (esp > 0) {
    cmd = s.substring(0, esp);
    arg = s.substring(esp + 1);
    arg.trim();
  }
  cmd.toLowerCase();

  if (cmd == "?" || cmd == "h") { ajuda(); return; }
  if (cmd == "st")    { Serial.println("[st] " + proto.statusJson()); return; }
  if (cmd == "open")  { accessFsm.manualOpen();  Serial.println("[ok] reabastecer"); return; }
  if (cmd == "close") { accessFsm.manualClose(); Serial.println("[ok] fechar"); return; }
  if (cmd == "reset") { accessFsm.reset();       Serial.println("[ok] rearmado"); return; }
  if (cmd == "ev")    { imprimeEventos(); return; }

  if (cmd == "w") {
    if (arg.length() == 0) { Serial.println("[erro] uso: w <gramas>"); return; }
    float g = arg.toFloat();
    if (!mockPtr) {
      Serial.println("[erro] balanca REAL em uso - 'w' so vale no modo simulado");
      Serial.println("       para zerar use 'tara'; para calibrar, 'cal <gramas>'");
      return;
    }
    mockPtr->setTargetWeightG(g);
    Serial.printf("[ok] balanca indo para %.0f g\n", g);
    return;
  }

  if (cmd == "min" || cmd == "max") {
    if (arg.length() == 0) { Serial.println("[erro] falta o valor em gramas"); return; }
    AppConfig cfg = accessFsm.config();
    if (cmd == "min") cfg.benchMinG = arg.toFloat();
    else              cfg.benchMaxG = arg.toFloat();
    if (cfg.benchMinG > cfg.benchMaxG) { Serial.println("[erro] min maior que max"); return; }
    storage.saveAppConfig(cfg);
    accessFsm.applyConfig(cfg);
    Serial.printf("[ok] janela de bancada: %.0f a %.0f g\n", cfg.benchMinG, cfg.benchMaxG);
    return;
  }

  if (cmd == "cal") {
    if (arg == "on") {
      door.enterCalibration();
      Serial.printf("[ok] CALIBRACAO ligada - servo em %d graus. Use 'pos <graus>'.\n",
                    door.currentDeg());
    } else if (arg == "off") {
      door.exitCalibration();
      Serial.println("[ok] calibracao encerrada - voltando para a posicao de fechada");
    } else {
      Serial.println("[erro] uso: cal on | cal off");
    }
    return;
  }

  if (cmd == "pos") {
    if (!door.isCalibrating()) { Serial.println("[erro] entre em 'cal on' antes"); return; }
    if (arg.length() == 0)     { Serial.println("[erro] uso: pos <graus 0..180>"); return; }
    int deg = constrain(arg.toInt(), 0, 180);
    door.jogToDeg(deg);
    Serial.printf("[ok] indo para %d graus (%d us)\n", deg, doorDegToUs(deg));
    return;
  }

  if (cmd == "setclosed" || cmd == "setopen") {
    if (!door.isCalibrating()) { Serial.println("[erro] entre em 'cal on' antes"); return; }
    AppConfig cfg = accessFsm.config();
    int deg = door.currentDeg();
    if (cmd == "setclosed") cfg.doorClosedDeg = deg;
    else                    cfg.doorOpenDeg   = deg;
    storage.saveAppConfig(cfg);
    accessFsm.applyConfig(cfg);
    Serial.printf("[ok] porta %s = %d graus (gravado em NVS). fechada=%d, aberta=%d\n",
                  cmd == "setclosed" ? "FECHADA" : "ABERTA", deg,
                  cfg.doorClosedDeg, cfg.doorOpenDeg);
    return;
  }

  if (cmd == "tara") {
    if (!hxPtr) { Serial.println("[erro] so com a balanca real"); return; }
    if (door.isMoving()) { Serial.println("[erro] espere a porta parar"); return; }
    Serial.println("[info] tarando (~2 s) - nao encoste na plataforma");
    if (!hxPtr->tare()) { Serial.println("[erro] modulo mudo"); return; }
    AppConfig cfg = accessFsm.config();
    cfg.tareOffset = hxPtr->tareOffset();
    storage.saveAppConfig(cfg);
    accessFsm.applyConfig(cfg);
    Serial.printf("[ok] offset = %ld (gravado na NVS)\n", hxPtr->tareOffset());
    return;
  }

  if (cmd == "cal") {
    if (!hxPtr) { Serial.println("[erro] so com a balanca real"); return; }
    if (arg.length() == 0) { Serial.println("[erro] uso: cal <gramas>"); return; }
    if (door.isMoving()) { Serial.println("[erro] espere a porta parar"); return; }
    Serial.println("[info] calibrando (~2 s) - massa conhecida no centro");
    if (!hxPtr->calibrate(arg.toFloat())) {
      Serial.println("[erro] falhou - a massa esta na plataforma?");
      return;
    }
    AppConfig cfg = accessFsm.config();
    cfg.calibFactor = hxPtr->calibFactor();
    cfg.tareOffset  = hxPtr->tareOffset();
    storage.saveAppConfig(cfg);
    accessFsm.applyConfig(cfg);
    Serial.printf("[ok] fator = %.3f contagens/g (gravado na NVS)\n", hxPtr->calibFactor());
    return;
  }

  if (cmd == "sensor") {
    bool ok = !(arg == "off" || arg == "0" || arg == "false");
    if (!mockPtr) {
      Serial.println("[erro] balanca REAL - desligue o modulo para testar a falha");
      return;
    }
    mockPtr->setHealthy(ok);
    Serial.printf("[ok] sensor %s\n", ok ? "saudavel" : "mudo");
    return;
  }

  Serial.printf("[erro] comando '%s' desconhecido - digite ?\n", cmd.c_str());
}

// ---------------------------------------------------------------- setup/loop

void setup() {
  Serial.begin(MONITOR_SPEED);
  delay(300);
  Serial.println("\n=== boot ===");

  eventLog.begin();
  AppConfig cfg = storage.loadAppConfig();

#if USE_HX711
  // Aproveita a calibracao que o teste de bancada gravou, se este firmware
  // ainda nao tiver a propria.
  if (storage.importarCalibracaoDaBancada(cfg)) {
    Serial.println("[balanca] calibracao importada do teste de bancada");
  }
  balanca.begin(cfg.calibFactor, cfg.tareOffset);
  if (!balanca.isCalibrated()) {
    Serial.println("[balanca] SEM CALIBRACAO - o peso vai sair 0.");
    Serial.println("[balanca] Calibre pelo app, ou rode o teste em teste_componentes/hx711.");
  }
#else
  balanca.begin();
#endif

  door.begin(cfg.doorClosedDeg, cfg.doorOpenDeg);
  accessFsm.begin(scale, &door, &eventLog, cfg);
  ble.begin();
  proto.begin(&ble, scale, mockPtr, hxPtr, &door, &accessFsm, &eventLog, &storage);

  eventLog.addBoot();
  ajuda();
}

void loop() {
  scale->update();
  accessFsm.update();
  door.update();

  BleMessage msg;
  while (ble.popMessage(msg)) proto.handleJson(msg.data);

  while (Serial.available()) {
    int c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (linhaLen > 0) {
        linha[linhaLen] = '\0';
        handleSerial(linha);
        linhaLen = 0;
      }
    } else if (linhaLen < sizeof(linha) - 1) {
      linha[linhaLen++] = (char)c;
    }
  }

  proto.tick();
}
