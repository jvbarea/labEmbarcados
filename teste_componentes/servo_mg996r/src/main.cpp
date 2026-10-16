// Teste de componente - Servo TowerPro 180 graus posicional em ESP32-WROOM-32
//
// Serve para MG995 e MG996R sem alteracao: mesmos 3 fios, 50 Hz, faixa de ~500-2500us,
// 180 graus com potenciometro interno. Muda so a velocidade de referencia (ver MS_POR_60_REF).
//
// Menu por texto no monitor serial a 115200. Digite '?' para a ajuda.
// O servo comeca DESLIGADO (sem pulso): nada se move ate o comando 'on'.
//
// ATENCAO: alimentar o servo com fonte externa 5-6 V (>= 2 A), GND comum com a ESP32.
// Ver README.md desta pasta.

#include <Arduino.h>
#include <ESP32Servo.h>

#ifndef MONITOR_SPEED
#define MONITOR_SPEED 115200
#endif

// ---------------------------------------------------------------- parametros

// Modelo em teste - so afeta os textos e a velocidade de referencia impressa no comando 't'.
// Os tres sao equivalentes para este teste: 3 fios, 50 Hz, ~500-2500us, 180 graus posicionais.
// MG995:  ~200 ms/60 graus a 4,8 V | torque ~8,5 kgf.cm
// MG996R: ~170 ms/60 graus a 4,8 V | torque ~9,4 kgf.cm
// MG946R: ~180 ms/60 graus a 4,8 V | torque ~10,5 kgf.cm  (datasheets variam por lote)
#define MODELO "MG946R"
static const int MS_POR_60_REF = 180;

static const int PINO_SERVO = 18;  // pinagem sugerida em docs/07-comunicacao-app-esp32.md

// Janela absoluta do hardware. So o teste de limites ('l') chega perto destes valores.
static const int US_MIN_ABS = 500;
static const int US_MAX_ABS = 2500;
static const int US_CENTRO = 1500;

// Janela de trabalho, conservadora ate o teste de limites dizer o contrario.
static const int US_MIN_PADRAO = 1000;
static const int US_MAX_PADRAO = 2000;

static const int PASSO_RAMPA_US = 5;       // ~1 grau por passo
static const uint32_t SLEW_MS = 15;        // intervalo entre passos da rampa
static const uint32_t IDLE_DETACH_MS = 30000;  // desliga sozinho depois de parado
static const int PASSO_LIMITE_US = 10;     // granularidade do teste de limites

// ------------------------------------------------------------------- estado

Servo servo;

static bool ligado = false;
static int minUs = US_MIN_PADRAO;
static int maxUs = US_MAX_PADRAO;
static int atualUs = US_CENTRO;
static uint32_t ultimaAtividade = 0;
static bool avisouAutoDesliga = false;

static char linha[64];
static size_t linhaLen = 0;

// ------------------------------------------------------------------- saidas

static void ok(const String &msg) { Serial.println("[ok] " + msg); }
static void info(const String &msg) { Serial.println("[info] " + msg); }
static void erro(const String &msg) { Serial.println("[erro] " + msg); }
static void dado(const String &msg) { Serial.println("[dado] " + msg); }  // copiar para resultados.md

// ---------------------------------------------------------------- conversao

static int angParaUs(int ang) {
  return map(constrain(ang, 0, 180), 0, 180, minUs, maxUs);
}

static int usParaAng(int us) {
  return map(constrain(us, minUs, maxUs), minUs, maxUs, 0, 180);
}

static String posicaoAtual() {
  return "pulso=" + String(atualUs) + "us ang=" + String(usParaAng(atualUs));
}

// ------------------------------------------------------- ligar / desligar

static void desligarServo(const char *motivo) {
  if (!ligado) return;
  servo.detach();
  ligado = false;
  ok(String("servo DESLIGADO (sem pulso) - ") + motivo);
}

static void ligarServo() {
  if (ligado) {
    info("servo ja estava ligado, " + posicaoAtual());
    return;
  }
  // Atencao: o servo salta para o ultimo setpoint assim que recebe pulso.
  servo.setPeriodHertz(50);
  servo.attach(PINO_SERVO, US_MIN_ABS, US_MAX_ABS);
  servo.writeMicroseconds(atualUs);
  ligado = true;
  ultimaAtividade = millis();
  avisouAutoDesliga = false;
  ok("servo LIGADO no GPIO " + String(PINO_SERVO) + ", " + posicaoAtual());
}

static void paradaEmergencia() {
  if (ligado) {
    servo.detach();
    ligado = false;
  }
  erro("PARADA DE EMERGENCIA - pulsos cortados. Use 'on' para religar.");
}

// ------------------------------------------- espera que aceita aborto por 'x'

// Retorna true se o operador pediu parada durante a espera.
static bool esperaComAborto(uint32_t ms) {
  uint32_t inicio = millis();
  while (millis() - inicio < ms) {
    while (Serial.available()) {
      int c = Serial.read();
      if (c == 'x' || c == 'X') {
        paradaEmergencia();
        return true;
      }
    }
    delay(1);
  }
  return false;
}

// Bloqueia ate receber uma linha. Devolve-a em minusculas, sem espacos nas pontas.
static String lerLinhaBloqueante() {
  String s;
  while (true) {
    while (Serial.available()) {
      int c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (s.length() == 0) return "";  // Enter vazio
        s.trim();
        s.toLowerCase();
        return s;
      }
      if (s.length() < 32) s += (char)c;
    }
    delay(2);
  }
}

// ------------------------------------------------------------- movimentacao

// Escreve o pulso respeitando apenas a janela absoluta (uso interno do teste de limites).
static void escreverBruto(int us) {
  atualUs = constrain(us, US_MIN_ABS, US_MAX_ABS);
  servo.writeMicroseconds(atualUs);
  ultimaAtividade = millis();
  avisouAutoDesliga = false;
}

// Vai ate o alvo em rampa. Retorna false se abortado ou se o servo estiver desligado.
static bool moverPara(int alvoUs, uint32_t passoMs = SLEW_MS) {
  if (!ligado) {
    erro("servo desligado - use 'on' antes de mover");
    return false;
  }
  alvoUs = constrain(alvoUs, minUs, maxUs);
  while (atualUs != alvoUs) {
    int falta = alvoUs - atualUs;
    int passo = (abs(falta) < PASSO_RAMPA_US) ? falta
                                              : (falta > 0 ? PASSO_RAMPA_US : -PASSO_RAMPA_US);
    escreverBruto(atualUs + passo);
    if (esperaComAborto(passoMs)) return false;
  }
  return true;
}

// Salto direto, sem rampa - usado onde a velocidade do proprio servo e o que interessa.
static bool saltarPara(int alvoUs) {
  if (!ligado) {
    erro("servo desligado - use 'on' antes de mover");
    return false;
  }
  escreverBruto(constrain(alvoUs, minUs, maxUs));
  return true;
}

// ------------------------------------------------------------------ rotinas

static void rotinaSweep() {
  if (!ligado) {
    erro("servo desligado - use 'on' antes de mover");
    return;
  }
  info("sweep 0 -> 180 -> 0 em rampa. Ouca estalos e sinta trechos duros. 'x' aborta.");
  if (!moverPara(minUs)) return;
  if (esperaComAborto(400)) return;
  if (!moverPara(maxUs)) return;
  if (esperaComAborto(400)) return;
  if (!moverPara(minUs)) return;
  ok("sweep concluido, " + posicaoAtual());
}

// Procura os batentes mecanicos subindo/descendo 10 us por vez, sob controle do operador.
static void rotinaLimites() {
  if (!ligado) {
    erro("servo desligado - use 'on' antes de mover");
    return;
  }
  info("TESTE DE LIMITES - Enter avanca " + String(PASSO_LIMITE_US) +
       "us, 'stop' marca o limite, 'x' aborta.");
  info("Digite 'stop' assim que ouvir o servo forcar ou ver que parou de andar.");

  escreverBruto(US_CENTRO);
  int achadoMax = US_MAX_PADRAO;
  int achadoMin = US_MIN_PADRAO;

  // Subindo, em busca do batente de cima.
  info("subindo a partir de " + String(US_CENTRO) + "us...");
  while (true) {
    String cmd = lerLinhaBloqueante();
    if (cmd == "x") {
      paradaEmergencia();
      return;
    }
    if (cmd == "stop") {
      achadoMax = atualUs - PASSO_LIMITE_US;  // um passo de margem
      dado("limite superior bruto=" + String(atualUs) + "us, com margem=" + String(achadoMax) + "us");
      break;
    }
    if (atualUs + PASSO_LIMITE_US > US_MAX_ABS) {
      achadoMax = US_MAX_ABS - PASSO_LIMITE_US;
      info("chegou ao teto absoluto de " + String(US_MAX_ABS) + "us sem batente aparente");
      break;
    }
    escreverBruto(atualUs + PASSO_LIMITE_US);
    Serial.println("      " + String(atualUs) + "us");
  }

  info("voltando ao centro...");
  escreverBruto(US_CENTRO);
  if (esperaComAborto(800)) return;

  // Descendo, em busca do batente de baixo.
  info("descendo a partir de " + String(US_CENTRO) + "us...");
  while (true) {
    String cmd = lerLinhaBloqueante();
    if (cmd == "x") {
      paradaEmergencia();
      return;
    }
    if (cmd == "stop") {
      achadoMin = atualUs + PASSO_LIMITE_US;
      dado("limite inferior bruto=" + String(atualUs) + "us, com margem=" + String(achadoMin) + "us");
      break;
    }
    if (atualUs - PASSO_LIMITE_US < US_MIN_ABS) {
      achadoMin = US_MIN_ABS + PASSO_LIMITE_US;
      info("chegou ao piso absoluto de " + String(US_MIN_ABS) + "us sem batente aparente");
      break;
    }
    escreverBruto(atualUs - PASSO_LIMITE_US);
    Serial.println("      " + String(atualUs) + "us");
  }

  escreverBruto(US_CENTRO);
  dado("janela util encontrada: " + String(achadoMin) + " a " + String(achadoMax) + " us");
  info("para gravar esta janela: w " + String(achadoMin) + " " + String(achadoMax));
}

// Cronometro operado pelo operador: o servo nao tem realimentacao para saber que chegou.
static void rotinaTempo() {
  if (!ligado) {
    erro("servo desligado - use 'on' antes de mover");
    return;
  }
  info("TEMPO DE EXCURSAO - cronometro manual, some ~0,2 s de tempo de reacao.");

  for (int volta = 0; volta < 2; volta++) {
    int partida = (volta == 0) ? minUs : maxUs;
    int chegada = (volta == 0) ? maxUs : minUs;
    const char *nome = (volta == 0) ? "min->max" : "max->min";

    info(String("posicionando em ") + String(partida) + "us...");
    if (!moverPara(partida)) return;
    if (esperaComAborto(700)) return;

    info("Enter para disparar o movimento " + String(nome));
    if (lerLinhaBloqueante() == "x") {
      paradaEmergencia();
      return;
    }

    uint32_t t0 = millis();
    if (!saltarPara(chegada)) return;
    info("Enter no instante em que o eixo parar");
    if (lerLinhaBloqueante() == "x") {
      paradaEmergencia();
      return;
    }
    uint32_t dt = millis() - t0;

    int graus = abs(usParaAng(chegada) - usParaAng(partida));
    dado(String(nome) + ": " + String(dt) + " ms para " + String(graus) + " graus (" +
         String(dt / (float)graus * 60.0f, 0) + " ms/60graus)");
  }
  info("datasheet do " MODELO ": ~" + String(MS_POR_60_REF) +
       " ms/60 graus a 4,8 V (sem carga). Muito acima disso = fonte fraca ou mecanismo duro.");
}

static void rotinaRepetibilidade() {
  if (!ligado) {
    erro("servo desligado - use 'on' antes de mover");
    return;
  }
  const int ciclos = 10;
  info("REPETIBILIDADE - " + String(ciclos) + " ciclos 60 <-> 120 graus.");
  info("Marque a posicao do horn agora (fita/caneta) e compare no fim. 'x' aborta.");

  if (!moverPara(angParaUs(60))) return;
  if (esperaComAborto(800)) return;

  uint32_t t0 = millis();
  for (int i = 1; i <= ciclos; i++) {
    if (!saltarPara(angParaUs(120))) return;
    if (esperaComAborto(600)) return;
    if (!saltarPara(angParaUs(60))) return;
    if (esperaComAborto(600)) return;
    Serial.println("      ciclo " + String(i) + "/" + String(ciclos));
  }
  uint32_t dt = millis() - t0;

  dado("repetibilidade: " + String(ciclos) + " ciclos em " + String(dt) + " ms, de volta a 60 graus");
  info("Confira: o horn voltou exatamente a marca? O servo esquentou? Houve reset da ESP32 no log?");
}

static void rotinaSegura() {
  if (!ligado) {
    erro("servo desligado - use 'on' antes de mover");
    return;
  }
  info("HOLD - 10 s parado em 90 graus. Observe tremor, zumbido e a corrente na fonte.");
  if (!moverPara(angParaUs(90))) return;
  for (int s = 10; s > 0; s--) {
    Serial.println("      " + String(s) + "s");
    if (esperaComAborto(1000)) return;
  }
  ok("hold concluido, " + posicaoAtual());
  info("Agora 'off' e repita a observacao: sem pulso o eixo fica livre ou continua travado?");
}

// -------------------------------------------------------------------- ajuda

static void ajuda() {
  Serial.println();
  Serial.println("=== Teste servo " MODELO " - ESP32 GPIO " + String(PINO_SERVO) + " ===");
  Serial.println("  ?           esta ajuda");
  Serial.println("  on / off    liga / desliga os pulsos (off = teste da decisao D6)");
  Serial.println("  a <graus>   vai para o angulo 0..180 em rampa");
  Serial.println("  u <us>      escreve o pulso direto, dentro da janela");
  Serial.println("  c           centro (" + String(US_CENTRO) + "us)");
  Serial.println("  s           sweep 0 -> 180 -> 0");
  Serial.println("  l           teste de limites (acha os batentes, passo a passo)");
  Serial.println("  w <min> <max>  grava a janela util em us");
  Serial.println("  t           tempo de excursao (cronometro manual)");
  Serial.println("  r           repetibilidade, 10 ciclos 60 <-> 120 graus");
  Serial.println("  j           segura 90 graus por 10 s");
  Serial.println("  x           PARADA DE EMERGENCIA (vale durante as rotinas)");
  Serial.println("  janela atual: " + String(minUs) + " a " + String(maxUs) + " us");
  Serial.println("  auto-desliga depois de " + String(IDLE_DETACH_MS / 1000) + " s parado");
  Serial.println();
}

// ------------------------------------------------------------- interpretador

static void executar(char *buf) {
  String s(buf);
  s.trim();
  if (s.length() == 0) return;

  String cmd = s;
  String arg = "";
  int esp = s.indexOf(' ');
  if (esp > 0) {
    cmd = s.substring(0, esp);
    arg = s.substring(esp + 1);
    arg.trim();
  }
  cmd.toLowerCase();

  if (cmd == "?" || cmd == "h" || cmd == "help") {
    ajuda();
  } else if (cmd == "on") {
    ligarServo();
  } else if (cmd == "off") {
    desligarServo("comando 'off'");
  } else if (cmd == "x") {
    paradaEmergencia();
  } else if (cmd == "a") {
    int ang = arg.toInt();
    if (arg.length() == 0 || ang < 0 || ang > 180) {
      erro("uso: a <graus 0..180>");
      return;
    }
    if (moverPara(angParaUs(ang))) ok(posicaoAtual());
  } else if (cmd == "u") {
    int us = arg.toInt();
    if (arg.length() == 0) {
      erro("uso: u <microssegundos>");
      return;
    }
    if (us < minUs || us > maxUs) {
      erro("fora da janela " + String(minUs) + ".." + String(maxUs) +
           "us - amplie com 'w' apos rodar 'l'");
      return;
    }
    if (moverPara(us)) ok(posicaoAtual());
  } else if (cmd == "c") {
    if (moverPara(US_CENTRO)) ok(posicaoAtual());
  } else if (cmd == "s") {
    rotinaSweep();
  } else if (cmd == "l") {
    rotinaLimites();
  } else if (cmd == "w") {
    int esp2 = arg.indexOf(' ');
    if (esp2 <= 0) {
      erro("uso: w <min_us> <max_us>");
      return;
    }
    int novoMin = arg.substring(0, esp2).toInt();
    int novoMax = arg.substring(esp2 + 1).toInt();
    if (novoMin < US_MIN_ABS || novoMax > US_MAX_ABS || novoMin + 100 > novoMax) {
      erro("janela invalida - permitido " + String(US_MIN_ABS) + ".." + String(US_MAX_ABS) +
           "us e min ao menos 100us abaixo do max");
      return;
    }
    minUs = novoMin;
    maxUs = novoMax;
    atualUs = constrain(atualUs, minUs, maxUs);
    if (ligado) servo.writeMicroseconds(atualUs);
    ok("janela = " + String(minUs) + ".." + String(maxUs) + "us, " + posicaoAtual());
  } else if (cmd == "t") {
    rotinaTempo();
  } else if (cmd == "r") {
    rotinaRepetibilidade();
  } else if (cmd == "j") {
    rotinaSegura();
  } else {
    erro("comando '" + cmd + "' desconhecido - digite ? para a ajuda");
  }
}

// ---------------------------------------------------------------- setup/loop

void setup() {
  Serial.begin(MONITOR_SPEED);
  delay(300);

  ESP32PWM::allocateTimer(0);  // um timer basta para um servo

  Serial.println();
  Serial.println("boot ok - " MODELO " DESLIGADO (nenhum pulso no GPIO " + String(PINO_SERVO) + ")");
  Serial.println("confira a fonte externa 5-6V e o GND comum antes de digitar 'on'");
  ajuda();
}

void loop() {
  while (Serial.available()) {
    int c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (linhaLen > 0) {
        linha[linhaLen] = '\0';
        executar(linha);
        linhaLen = 0;
      }
    } else if (linhaLen < sizeof(linha) - 1) {
      linha[linhaLen++] = (char)c;
    }
  }

  if (ligado) {
    uint32_t parado = millis() - ultimaAtividade;
    if (!avisouAutoDesliga && parado > IDLE_DETACH_MS - 5000) {
      info("sem comando ha um tempo - desligando os pulsos em 5 s");
      avisouAutoDesliga = true;
    }
    if (parado > IDLE_DETACH_MS) {
      desligarServo("auto-desliga por inatividade");
    }
  }

  delay(5);
}
