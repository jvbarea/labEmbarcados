/*
 * Alimentador de Animais - firmware
 * BLE (protocolo Feed Cat v2) + servo posicional + balanca (HX711 ou simulada)
 *
 * Fase 1 do doc 07: so BLE, sem Wi-Fi e sem HTTP. Com 'pio run -e mock' a
 * balanca e simulada, para exercitar a maquina de estados inteira e ver a porta
 * abrir de verdade sem celula de carga.
 *
 * O protocolo vive em AppProtocol.cpp e as regras em AccessStateMachine.cpp;
 * aqui fica so a fiacao dos modulos e o menu serial de bancada.
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

// Comando 'peso': imprime a balanca a cada 0,5 s ate digitar 'peso' de novo.
static bool mostrandoPeso = false;
static unsigned long ultimoPesoMs = 0;

static void imprimePeso() {
  if (hxPtr) {
    Serial.printf("[peso] %8.0f g | cru %9ld | fator %.3f | tara %ld | %s | %s\n",
                  scale->weightG(), hxPtr->rawCounts(), hxPtr->calibFactor(),
                  hxPtr->tareOffset(), scale->isStable() ? "estavel" : "variando",
                  scale->isHealthy() ? "sensor ok" : "SENSOR MUDO");
  } else {
    Serial.printf("[peso] %8.0f g (simulada) | %s\n", scale->weightG(),
                  scale->isStable() ? "estavel" : "variando");
  }
}

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
    Serial.println("  calpeso <gramas>  calibra com a massa conhecida no centro");
  }
  Serial.println("  st             imprime o status agora");
  Serial.println("  peso           liga/desliga a balanca na tela a cada 0,5 s (g, cru, fator, tara)");
  Serial.println("  open           reabastecer (fecha sozinha depois de maxOpenS)");
  Serial.println("  close          fechar (com a porta em falha, rearma)");
  Serial.println("  reset          rearma tudo depois de uma falha");
  Serial.println("  min <g>        limite inferior da janela de bancada (so sem gatos)");
  Serial.println("  max <g>        limite superior da janela de bancada (so sem gatos)");
  if (mockPtr) Serial.println("  sensor off|on  simula sensor mudo / recuperado");
  Serial.println("  ev             ultimos 10 eventos");
  Serial.println("  evclear        apaga o historico da flash (o seq continua)");
  Serial.println("  --- calibracao do servo e da porta ---");
  Serial.println("  cal on|off     entra/sai do modo calibracao (a FSM para)");
  Serial.println("  servo          mostra a calibracao do servo (vale fora de cal on)");
  Serial.println("  Dentro de 'cal on':");
  Serial.printf("  us <valor>     vai para o pulso em us (%d..%d), em rampa\n", SERVO_US_MIN, SERVO_US_MAX);
  Serial.println("  + / -          anda 10 us a partir do alvo (ou '+25', '-5')");
  Serial.println("  set0 / set90   grava o pulso atual como 0 / 90 graus FISICOS");
  Serial.println("  pos <graus>    vai ao angulo pela calibracao (confira no transferidor)");
  Serial.println("  setclosed      grava o angulo atual como PORTA FECHADA");
  Serial.println("  setopen        grava o angulo atual como PORTA ABERTA");
  Serial.printf("  janela de bancada: %.0f a %.0f g | confMs %lu | leaveMs %lu\n",
                c.benchMinG, c.benchMaxG,
                (unsigned long)c.confMs, (unsigned long)c.leaveMs);
  Serial.printf("  servo: 0 graus = %d us, 90 graus = %d us%s\n", door.servoUs0(), door.servoUs90(),
                (c.servoUs0 == DEFAULT_SERVO_US_AT_0DEG && c.servoUs90 == DEFAULT_SERVO_US_AT_90DEG)
                    ? "  (PADRAO NOMINAL, ainda nao medido)" : "");
  Serial.printf("  porta: fechada=%d graus (%d us), aberta=%d graus (%d us), GPIO %d\n",
                c.doorClosedDeg, door.degToUs(c.doorClosedDeg),
                c.doorOpenDeg, door.degToUs(c.doorOpenDeg), PIN_SERVO);
  Serial.printf("  gatos cadastrados: %u | cfgV %lu | modo %s%s\n",
                (unsigned)c.catCount, (unsigned long)c.cfgV,
                c.learnMode ? "learn" : "auto",
                c.learnMode ? (LEARN_DOOR_STAYS_OPEN ? " (porta aberta o modo todo)"
                                                     : " (porta abre a cada visita)")
                            : "");
  Serial.printf("  historico: %u evento(s), ultimo seq %lu, %s\n",
                (unsigned)eventLog.count(), (unsigned long)eventLog.lastSeq(),
                eventLog.persistent() ? "gravado no LittleFS" : "SO NA RAM (LittleFS falhou)");
  Serial.println();
}

static void imprimeEventos() {
  FeederEvent itens[10];
  size_t n = eventLog.latest(itens, 10);
  if (n == 0) { Serial.println("[ev] nenhum evento guardado"); return; }
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

// Os comandos de pulso so valem dentro de 'cal on': fora dele o servo esta
// desenergizado e quem manda no eixo e a maquina de estados.
static bool exigeCalibracao() {
  if (door.isCalibrating()) return true;
  Serial.println("[erro] entre em 'cal on' antes");
  return false;
}

// Acima da faixa nominal o servo pode chegar ao fim de curso interno e passar a
// forcar a engrenagem. Nao e proibido (a porta precisa chegar a trava fisica), mas
// a cada passo o operador e lembrado de parar ao primeiro sinal de esforco.
static void avisaForaDaFaixaNominal(int us) {
  if (us <= SERVO_US_NOMINAL_MAX) return;
  Serial.printf("[aviso] %d us esta acima da faixa nominal (%d). Pare ao primeiro zumbido grave\n"
                "        ou quando o eixo deixar de andar; depois do contato com a trava, no maximo\n"
                "        mais 10 a 20 us (o torque cresce com a diferenca entre o pulso e a posicao).\n",
                us, SERVO_US_NOMINAL_MAX);
}

// toInt() devolve 0 para texto que nao e numero, e 0 vira "500 us" depois do
// limite: 'us abc' empurraria o eixo ate o fim do curso. Por isso valida antes.
static bool ehNumeroInteiro(const String &t) {
  if (t.length() == 0) return false;
  for (size_t i = 0; i < t.length(); i++) {
    if (!isDigit(t.charAt(i))) return false;
  }
  return true;
}

static void imprimeCalibracaoServo() {
  const int us0 = door.servoUs0();
  const int us90 = door.servoUs90();
  const int us180 = us0 + 2 * (us90 - us0);
  Serial.printf("[dado] servo: 0 graus = %d us | 90 graus = %d us | 180 graus (extrapolado) = %d us\n",
                us0, us90, us180);
  if (us180 < SERVO_US_MIN || us180 > SERVO_US_MAX) {
    Serial.printf("[info] o 180 extrapolado cai fora de %d..%d us e seria cortado nesse limite\n",
                  SERVO_US_MIN, SERVO_US_MAX);
  }
  Serial.println("[info] os batentes reais do servo ficam antes ou depois disso - so o transferidor diz");
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
  if (cmd == "st") {
    Serial.println("[st] " + proto.statusJson());
    Serial.printf("[st] fsm=%s reabastecer=%s fechar_pendente=%s ble=%s mtu=%u\n",
                  accessStateName(accessFsm.state()),
                  accessFsm.refillActive() ? "sim" : "nao",
                  accessFsm.closePending() ? "sim" : "nao",
                  ble.isConnected() ? "conectado" : "sem cliente", (unsigned)ble.mtu());
    return;
  }
  if (cmd == "open") {
    if (accessFsm.startRefill(0)) Serial.println("[ok] reabastecer");
    else Serial.println("[erro] porta em falha ou em calibracao - 'close' rearma");
    return;
  }
  if (cmd == "close") { accessFsm.requestClose(); Serial.println("[ok] fechar"); return; }
  if (cmd == "reset") { accessFsm.reset();        Serial.println("[ok] rearmado"); return; }
  if (cmd == "ev")    { imprimeEventos(); return; }
  if (cmd == "peso") {
    mostrandoPeso = !mostrandoPeso;
    Serial.println(mostrandoPeso ? "[ok] peso a cada 0,5 s - digite 'peso' de novo para parar"
                                 : "[ok] parou");
    return;
  }
  if (cmd == "evclear") { eventLog.clear(); return; }

  if (cmd == "w") {
    if (arg.length() == 0) { Serial.println("[erro] uso: w <gramas>"); return; }
    float g = arg.toFloat();
    if (!mockPtr) {
      Serial.println("[erro] balanca REAL em uso - 'w' so vale no modo simulado");
      Serial.println("       para zerar use 'tara'; para calibrar, 'calpeso <gramas>'");
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
      Serial.printf("[ok] CALIBRACAO ligada - eixo em %d us (%d graus pela calibracao em vigor)\n",
                    door.currentPulseUs(), door.currentDeg());
      Serial.println("[info] va ao meio com 'us 1500' e ande de 10 em 10 us com '+' e '-'.");
      Serial.println("[info] PARE ao primeiro sinal de esforco: zumbido grave ou eixo que nao anda mais.");
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
    const bool partida = door.jogToDeg(deg);
    Serial.printf("[ok] indo para %d graus (%d us)\n", deg, door.degToUs(deg));
    if (partida) {
      Serial.printf("[info] partida suave: %d graus devagar, pausa de %d ms, depois o resto\n",
                    DOOR_KICK_DEG, DOOR_KICK_HOLD_MS);
    }
    return;
  }

  if (cmd == "servo") { imprimeCalibracaoServo(); return; }

  if (cmd == "us") {
    if (!exigeCalibracao()) return;
    if (!ehNumeroInteiro(arg)) { Serial.println("[erro] uso: us <microssegundos>  (ex.: us 1500)"); return; }
    const int pedido = arg.toInt();
    const int us = constrain(pedido, SERVO_US_MIN, SERVO_US_MAX);
    door.jogToUs(us);
    Serial.printf("[ok] indo para %d us%s\n", us,
                  us != pedido ? "  (limitado a faixa de seguranca)" : "");
    avisaForaDaFaixaNominal(us);
    return;
  }

  // '+' e '-' andam a partir do ALVO. Aceita '+', '+25', '+ 25'.
  if (cmd.charAt(0) == '+' || cmd.charAt(0) == '-') {
    if (!exigeCalibracao()) return;
    const bool desce = (cmd.charAt(0) == '-');
    const String num = (cmd.length() > 1) ? cmd.substring(1) : arg;
    int passo = SERVO_NUDGE_US;
    if (num.length() > 0) {
      if (!ehNumeroInteiro(num) || num.toInt() <= 0) {
        Serial.println("[erro] uso: '+' ou '-' (10 us), ou '+25' / '-5'");
        return;
      }
      passo = num.toInt();
    }
    door.nudge(desce ? -passo : passo);
    Serial.printf("[ok] alvo = %d us (%d graus pela calibracao)\n",
                  door.targetPulseUs(), door.usToDeg(door.targetPulseUs()));
    avisaForaDaFaixaNominal(door.targetPulseUs());
    return;
  }

  // Grava o pulso em que o eixo ESTA, entao so vale com o eixo parado: no meio
  // da rampa o pulso escrito ainda nao e onde o eixo chegou.
  if (cmd == "set0" || cmd == "set90") {
    if (!exigeCalibracao()) return;
    if (!door.atTarget()) {
      Serial.println("[erro] o eixo ainda esta andando - espere parar e repita");
      return;
    }
    const bool zero = (cmd == "set0");
    const int us = door.currentPulseUs();

    AppConfig cfg = accessFsm.config();
    const int novo0  = zero ? us : cfg.servoUs0;
    const int novo90 = zero ? cfg.servoUs90 : us;

    if (!door.setServoCalibration(novo0, novo90)) {
      Serial.printf("[erro] recusado: 0 graus = %d us e 90 graus = %d us ficariam a menos de %d us\n",
                    novo0, novo90, SERVO_CAL_MIN_SPAN_US);
      Serial.println("       um do outro. Grave o outro ponto primeiro, ou ande mais antes de gravar.");
      return;
    }

    cfg.servoUs0 = novo0;
    cfg.servoUs90 = novo90;
    storage.saveAppConfig(cfg);
    accessFsm.applyConfig(cfg);
    Serial.printf("[ok] %d graus FISICOS = %d us (gravado na NVS)\n", zero ? 0 : 90, us);
    imprimeCalibracaoServo();
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

  // 'calpeso' e nao 'cal': 'cal on|off' e o modo de calibracao da porta. Com os
  // dois no mesmo nome, o da porta capturava tudo e este nunca executava.
  if (cmd == "calpeso") {
    if (!hxPtr) { Serial.println("[erro] so com a balanca real"); return; }
    if (arg.length() == 0) { Serial.println("[erro] uso: calpeso <gramas>"); return; }
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

  door.begin(cfg.doorClosedDeg, cfg.doorOpenDeg, cfg.servoUs0, cfg.servoUs90);
  accessFsm.begin(scale, &door, &eventLog, cfg);
  ble.begin();
  proto.begin(&ble, scale, mockPtr, hxPtr, &door, &accessFsm, &eventLog, &storage);

  eventLog.addBoot();
  ajuda();
  // A pilha BLE come boa parte da RAM; abaixo de ~30 KB livres o firmware fica
  // sujeito a falhas de alocacao no meio de uma mensagem grande.
  Serial.printf("[mem] heap livre depois do boot: %u bytes\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
  ble.update();  // primeiro: percebe conexao nova ou queda antes de enfileirar
  scale->update();
  accessFsm.update();
  door.update();

  static BleMessage msg;  // ~520 bytes: fora da pilha do loop
  while (ble.popMessage(msg)) proto.handleMessage(msg.data, msg.len);

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

  if (mostrandoPeso && millis() - ultimoPesoMs >= 500) {
    ultimoPesoMs = millis();
    imprimePeso();
  }
}
