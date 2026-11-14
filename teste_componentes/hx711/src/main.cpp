// Teste de componente - modulo XFW-HX711 + 4 celulas de carga de meia-ponte
//
// Menu por texto no monitor serial a 115200. Digite '?' para a ajuda.
// O diagnostico de fiacao ('d') roda sozinho no boot.
//
// ATENCAO: modulo em 3,3 V, NUNCA em 5 V. O nivel logico do DT acompanha o VCC,
// e 5 V num GPIO da ESP32 e o problema E1 do docs/02. Ver README.md desta pasta.

#include <Arduino.h>
#include <HX711.h>
#include <Preferences.h>

#ifndef MONITOR_SPEED
#define MONITOR_SPEED 115200
#endif

// ---------------------------------------------------------------- parametros

// Pinagem do docs/07 secao 4. O codigo herdado usava 4 e 5, e o GPIO 5 e pino
// de strapping - atrapalha o boot.
static const int PIN_DT  = 26;
static const int PIN_SCK = 25;

// O HX711 entrega 24 bits com sinal: +-8388607 e o fundo de escala.
static const long SATURACAO = 8300000L;

static const int DIAG_SAMPLES  = 20;
static const int TARE_SAMPLES  = 20;
static const int CALIB_SAMPLES = 20;
static const int RUIDO_SAMPLES = 100;

static const uint32_t DERIVA_INTERVALO_MS = 10000;
static const uint32_t DERIVA_TOTAL_MS     = 5UL * 60UL * 1000UL;

static const char *NVS_NS = "hx711-cal";

// ------------------------------------------------------------------- estado

HX711 hx;

static long  offset_ = 0;      // contagens com a plataforma vazia
static float escala_ = 0.0f;   // contagens por grama; 0 = ainda nao calibrado

// ------------------------------------------------------------------- saidas

static void ok(const String &m)   { Serial.println("[ok] " + m); }
static void info(const String &m) { Serial.println("[info] " + m); }
static void erro(const String &m) { Serial.println("[erro] " + m); }
static void dado(const String &m) { Serial.println("[dado] " + m); }  // copiar p/ resultados.md

// ------------------------------------------------------------------ leitura

// Espera uma amostra ficar disponivel. false se o modulo nao responder.
static bool esperaPronto(uint32_t timeoutMs = 1000) {
  uint32_t t0 = millis();
  while (!hx.is_ready()) {
    if (millis() - t0 > timeoutMs) return false;
    delay(1);
  }
  return true;
}

// Media de n leituras brutas. Devolve false se o modulo emudecer no meio.
static bool mediaBruta(int n, double &media, long &menor, long &maior) {
  double soma = 0;
  menor = LONG_MAX;
  maior = LONG_MIN;

  for (int i = 0; i < n; i++) {
    if (!esperaPronto()) return false;
    long v = hx.read();
    soma += (double)v;
    if (v < menor) menor = v;
    if (v > maior) maior = v;
  }
  media = soma / n;
  return true;
}

static float paraGramas(double bruto) {
  if (escala_ == 0.0f) return 0.0f;
  return (float)((bruto - (double)offset_) / (double)escala_);
}

// Qualquer tecla interrompe os modos continuos.
static bool interrompido() {
  if (!Serial.available()) return false;
  while (Serial.available()) Serial.read();
  return true;
}

// O terminal manda "r\r\n": o \r dispara o comando e o \n fica sobrando no
// buffer. Sem limpar, o modo continuo enxerga esse \n como "tecla apertada" e
// para na primeira iteracao. Chamar antes de qualquer espera por tecla.
static void limpaEntrada() {
  delay(30);  // tempo para o resto da quebra de linha chegar
  while (Serial.available()) Serial.read();
}

// --------------------------------------------------------------- persistencia

static void carregaCalibracao() {
  Preferences p;
  if (!p.begin(NVS_NS, /*readOnly=*/true)) return;
  offset_ = p.getLong("off", 0);
  escala_ = p.getFloat("scl", 0.0f);
  p.end();
}

static void salvaCalibracao() {
  Preferences p;
  if (!p.begin(NVS_NS, /*readOnly=*/false)) { erro("nao consegui abrir a NVS"); return; }
  p.putLong("off", offset_);
  p.putFloat("scl", escala_);
  p.end();
  ok("calibracao gravada na NVS");
}

static void limpaCalibracao() {
  Preferences p;
  if (p.begin(NVS_NS, /*readOnly=*/false)) { p.clear(); p.end(); }
  offset_ = 0;
  escala_ = 0.0f;
  ok("calibracao apagada - refaca 't' e 'c <gramas>'");
}

// ------------------------------------------------------------- diagnostico

static void diagnostico() {
  Serial.println();
  Serial.println("=== DIAGNOSTICO DE FIACAO ===");

  // 1) O modulo responde?
  if (!esperaPronto(2000)) {
    erro("o DT nunca ficou baixo em 2 s - o modulo nao esta respondendo");
    Serial.println("      Confira, nesta ordem:");
    Serial.println("      1. VCC do modulo em 3,3 V (NAO 5 V) vindo do pino 3V3 da ESP32");
    Serial.println("      2. GND do modulo no GND da ESP32 - se as fontes forem diferentes,");
    Serial.println("         os dois GND precisam estar unidos");
    Serial.printf("      3. DT no GPIO %d e SCK no GPIO %d (nao trocados)\n", PIN_DT, PIN_SCK);
    return;
  }
  ok("modulo responde - DT e SCK estao certos e ha alimentacao");

  // 2) Como esta a leitura parada?
  double media;
  long menor, maior;
  if (!mediaBruta(DIAG_SAMPLES, media, menor, maior)) {
    erro("o modulo parou de responder no meio da leitura - contato intermitente");
    return;
  }
  long faixa = maior - menor;
  dado("repouso: media=" + String(media, 0) + " min=" + String(menor) +
       " max=" + String(maior) + " faixa=" + String(faixa));

  if (fabs(media) > SATURACAO) {
    erro("ADC SATURADO (" + String(media, 0) + ") - a ponte esta muito desbalanceada");
    Serial.println("      Quase sempre e a ligacao das celulas:");
    Serial.println("      - um fio preto ou branco do anel sem contato");
    Serial.println("      - E+/E- e A+/A- trocados entre si");
    Serial.println("      - uma celula com fio rompido (meca a resistencia: os dois extremos");
    Serial.println("        devem dar o dobro de cada extremo ate o fio do meio)");
    return;
  }
  ok("leitura dentro da faixa do ADC, sem saturacao");

  if (faixa > 2000) {
    info("ruido alto parado (faixa " + String(faixa) + " contagens).");
    Serial.println("      Causas comuns: modulo na mesma fonte do servo, fios longos");
    Serial.println("      sem trancar, ou plataforma balancando. Rode 'n' para medir.");
  } else {
    ok("ruido de repouso aceitavel");
  }

  // 3) Responde a carga?
  Serial.println();
  limpaEntrada();
  info("Agora APERTE UMA DAS CELULAS com o dedo e, segurando, de Enter.");
  while (!Serial.available()) delay(10);
  while (Serial.available()) Serial.read();

  double comCarga;
  if (!mediaBruta(DIAG_SAMPLES, comCarga, menor, maior)) {
    erro("o modulo parou de responder - contato intermitente");
    return;
  }
  double delta = comCarga - media;
  dado("com carga: media=" + String(comCarga, 0) + " delta=" + String(delta, 0));

  if (fabs(delta) < 100) {
    erro("a leitura quase nao mudou (delta " + String(delta, 0) + ")");
    Serial.println("      O sinal nao esta chegando. Confira A+ e A-:");
    Serial.println("      eles saem dos fios VERMELHOS de duas celulas em DIAGONAL,");
    Serial.println("      e E+/E- dos vermelhos da outra diagonal.");
    return;
  }

  if (delta < 0) {
    info("sinal INVERTIDO: apertar fez a leitura cair.");
    Serial.println("      Troque A+ com A- no modulo. (Ou deixe assim: a calibracao");
    Serial.println("      com 'c' gera um fator negativo e o peso sai certo do mesmo jeito.)");
  } else {
    ok("responde a carga no sentido certo (delta +" + String(delta, 0) + ")");
  }

  Serial.println();
  info("Aperte cada uma das QUATRO celulas, uma de cada vez, com 'r' ligado.");
  Serial.println("      Todas devem mover a leitura. Se so uma mover, o anel de fios");
  Serial.println("      preto/branco esta aberto em alguma emenda.");
  Serial.println("=== fim do diagnostico ===");
  Serial.println();
}

// ------------------------------------------------------------------ rotinas

// Meio segundo: rapido o suficiente para acompanhar, devagar o suficiente para
// ler enquanto se aperta uma celula de cada vez.
static const uint32_t LEITURA_INTERVALO_MS = 500;

// Espera o intervalo sem travar: qualquer tecla interrompe no meio.
static bool esperaOuInterrompe(uint32_t ms) {
  uint32_t alvo = millis() + ms;
  while (millis() < alvo) {
    if (interrompido()) return true;
    delay(10);
  }
  return false;
}

static void leituraContinua(bool emGramas) {
  if (emGramas && escala_ == 0.0f) {
    erro("ainda nao calibrado - rode 't' e depois 'c <gramas>'");
    return;
  }

  limpaEntrada();

  // A primeira leitura vira a referencia. O delta e o que interessa quando se
  // aperta celula por celula: o sinal diz se aquela celula soma ou subtrai.
  if (!esperaPronto()) { erro("modulo mudo"); return; }
  long base = hx.read();

  info(String(emGramas ? "leitura em gramas" : "leitura bruta (contagens)") +
       " a cada 0,5 s - qualquer tecla para parar");
  info("referencia = " + String(base) + " (o delta e medido a partir dela)");

  while (true) {
    if (esperaOuInterrompe(LEITURA_INTERVALO_MS)) break;
    if (!esperaPronto()) { erro("modulo mudo"); return; }

    long v = hx.read();
    long d = v - base;
    if (emGramas) {
      Serial.printf("      %9.1f g    delta %+9.1f g\n",
                    paraGramas(v), (double)d / (double)escala_);
    } else {
      Serial.printf("      %10ld    delta %+10ld\n", v, d);
    }
  }
  ok("parado");
}

static void tara() {
  info("tarando com " + String(TARE_SAMPLES) + " leituras - nao encoste na plataforma");
  double media;
  long menor, maior;
  if (!mediaBruta(TARE_SAMPLES, media, menor, maior)) { erro("modulo mudo"); return; }
  offset_ = (long)llround(media);
  dado("offset (zero) = " + String(offset_) + "  [faixa " + String(maior - menor) + "]");
}

static void calibra(float massaG) {
  if (massaG <= 0) { erro("uso: c <gramas>  (ex.: c 1000)"); return; }
  if (offset_ == 0) info("atencao: a tara ainda nao foi feita; rode 't' com a plataforma vazia");

  info("calibrando com " + String(massaG, 0) + " g em cima - nao encoste");
  double media;
  long menor, maior;
  if (!mediaBruta(CALIB_SAMPLES, media, menor, maior)) { erro("modulo mudo"); return; }

  double delta = media - (double)offset_;
  if (fabs(delta) < 100) {
    erro("a leitura nao mudou com o peso em cima - confira se a massa esta na plataforma");
    return;
  }

  escala_ = (float)(delta / (double)massaG);
  dado("escala = " + String(escala_, 3) + " contagens/grama  (delta " + String(delta, 0) + ")");
  ok("conferindo: " + String(paraGramas(media), 1) + " g (esperado " + String(massaG, 0) + " g)");
  info("se estiver bom, rode 'save' para gravar na NVS");
}

/*
 * Teste de cantos (balanceamento).
 *
 * Com UM HX711 lendo a ponte combinada, as quatro celulas sao somadas em
 * hardware antes do ADC: nao da para saber quanto cada uma contribuiu, e por
 * isso NAO existe correcao de canto em software. Este teste e diagnostico - a
 * correcao e mecanica (nivelar o prato, pre-carga igual, calcar o canto baixo).
 *
 * Importa porque o gato nao pisa no centro: se a leitura muda conforme a
 * posicao, a identificacao por peso fica comprometida.
 */
static void testeCantos(float massaG) {
  if (massaG <= 0) { erro("uso: b <gramas>  (ex.: b 1000)"); return; }

  const char *posicoes[5] = {
    "no CENTRO do prato",
    "sobre a celula 1",
    "sobre a celula 2",
    "sobre a celula 3",
    "sobre a celula 4",
  };
  const char *nomes[5] = { "centro", "celula 1", "celula 2", "celula 3", "celula 4" };

  const bool emGramas = (escala_ != 0.0f);
  double leitura[5];

  info("TESTE DE CANTOS com " + String(massaG, 0) + " g - 5 posicoes.");
  if (!emGramas) info("sem calibracao ainda: os valores saem em contagens");

  for (int i = 0; i < 5; i++) {
    limpaEntrada();
    Serial.printf("      [%d/5] coloque a massa %s e de Enter\n", i + 1, posicoes[i]);
    while (!Serial.available()) delay(10);
    while (Serial.available()) Serial.read();

    double media;
    long menor, maior;
    if (!mediaBruta(CALIB_SAMPLES, media, menor, maior)) { erro("modulo mudo"); return; }

    leitura[i] = emGramas ? (double)paraGramas(media) : (media - (double)offset_);
    Serial.printf("            %s: %.1f%s\n", nomes[i], leitura[i], emGramas ? " g" : "");
  }

  // Espalhamento entre as QUATRO posicoes de canto; o centro e a referencia.
  double menorC = leitura[1], maiorC = leitura[1];
  int iMenor = 1, iMaior = 1;
  for (int i = 2; i < 5; i++) {
    if (leitura[i] < menorC) { menorC = leitura[i]; iMenor = i; }
    if (leitura[i] > maiorC) { maiorC = leitura[i]; iMaior = i; }
  }

  double espalhamento = maiorC - menorC;
  double centro = leitura[0];
  double pct = (fabs(centro) > 1.0) ? (espalhamento / fabs(centro)) * 100.0 : 0.0;

  Serial.println();
  dado("cantos: centro=" + String(centro, 1) +
       "  min=" + String(menorC, 1) + " (" + nomes[iMenor] + ")" +
       "  max=" + String(maiorC, 1) + " (" + nomes[iMaior] + ")");
  dado("espalhamento = " + String(espalhamento, 1) + (emGramas ? " g" : " contagens") +
       "  =  " + String(pct, 1) + "% do centro");

  if (!emGramas) {
    info("calibre com 'c <gramas>' e repita, para ver o espalhamento em gramas");
    return;
  }

  if (pct < 2.0) {
    ok("prato bem balanceado - nada a ajustar");
  } else if (pct < 5.0) {
    info("balanceamento aceitavel, mas da para melhorar.");
    Serial.printf("      %s le mais baixo e %s mais alto.\n", nomes[iMenor], nomes[iMaior]);
  } else {
    erro("prato DESNIVELADO (" + String(pct, 1) + "%) - corrija antes de seguir");
    Serial.printf("      %s le %.1f g a menos que %s.\n",
                  nomes[iMenor], espalhamento, nomes[iMaior]);
    Serial.println("      Nao da para corrigir em software: as 4 celulas sao somadas");
    Serial.println("      antes do ADC. Confira, nesta ordem:");
    Serial.println("      1. as quatro celulas encostam e carregam peso (sem folga)");
    Serial.println("      2. parafusos com aperto parecido, sem empenar o prato");
    Serial.println("      3. prato rigido - se ele flexiona, a carga nao se reparte");
    Serial.println("      4. calce o canto que le menos");
  }

  info("compare com a janela de +-500 g que o app usa por gato.");
}

static void mediaRuido() {
  info("medindo ruido com " + String(RUIDO_SAMPLES) + " leituras - nao encoste");

  double soma = 0, soma2 = 0;
  long menor = LONG_MAX, maior = LONG_MIN;
  for (int i = 0; i < RUIDO_SAMPLES; i++) {
    if (!esperaPronto()) { erro("modulo mudo"); return; }
    long v = hx.read();
    soma  += (double)v;
    soma2 += (double)v * (double)v;
    if (v < menor) menor = v;
    if (v > maior) maior = v;
  }

  double media = soma / RUIDO_SAMPLES;
  double var   = (soma2 / RUIDO_SAMPLES) - (media * media);
  double desvio = var > 0 ? sqrt(var) : 0;
  long faixa = maior - menor;

  dado("ruido: desvio=" + String(desvio, 1) + " faixa=" + String(faixa) + " contagens");
  if (escala_ != 0.0f) {
    dado("ruido: desvio=" + String(fabs(desvio / escala_), 1) +
         " g   faixa=" + String(fabs(faixa / escala_), 1) + " g");
    info("a tolerancia de estabilidade do firmware precisa ser maior que a faixa em gramas");
  } else {
    info("calibre com 'c' para ver o ruido em gramas");
  }
}

static void taxaAmostragem() {
  info("contando leituras por 5 s...");
  uint32_t t0 = millis();
  int n = 0;
  while (millis() - t0 < 5000) {
    if (!esperaPronto(500)) { erro("modulo mudo"); return; }
    hx.read();
    n++;
  }
  float hz = n / 5.0f;
  dado("taxa = " + String(hz, 1) + " Hz (" + String(n) + " leituras em 5 s)");
  if (hz < 15)      info("~10 SPS: o padrao do modulo, com o pino RATE em GND");
  else if (hz < 95) info("~80 SPS: o modulo esta jumpeado para a taxa rapida (mais ruido)");
  else              info("taxa acima do esperado - conferir");
}

static void deriva() {
  if (escala_ == 0.0f) info("sem calibracao: os numeros saem em contagens");
  limpaEntrada();
  info("deriva: uma leitura a cada 10 s por 5 min. Qualquer tecla para antes.");
  info("Nao encoste na plataforma, e anote a temperatura da sala.");

  uint32_t t0 = millis();
  double primeira = 0;
  bool temPrimeira = false;

  while (millis() - t0 < DERIVA_TOTAL_MS) {
    if (interrompido()) { ok("deriva interrompida"); return; }

    double media;
    long menor, maior;
    if (!mediaBruta(5, media, menor, maior)) { erro("modulo mudo"); return; }
    if (!temPrimeira) { primeira = media; temPrimeira = true; }

    uint32_t min = (millis() - t0) / 60000;
    uint32_t seg = ((millis() - t0) / 1000) % 60;
    double desvio = media - primeira;

    if (escala_ != 0.0f) {
      Serial.printf("      %02lu:%02lu  %8.1f g   (deriva %+.1f g)\n",
                    (unsigned long)min, (unsigned long)seg,
                    paraGramas(media), desvio / escala_);
    } else {
      Serial.printf("      %02lu:%02lu  %10.0f   (deriva %+.0f)\n",
                    (unsigned long)min, (unsigned long)seg, media, desvio);
    }

    uint32_t alvo = millis() + DERIVA_INTERVALO_MS;
    while (millis() < alvo) {
      if (interrompido()) { ok("deriva interrompida"); return; }
      delay(50);
    }
  }
  ok("deriva concluida - anote o valor final em resultados.md");
}

// -------------------------------------------------------------------- ajuda

static void ajuda() {
  Serial.println();
  Serial.printf("=== Teste HX711 - DT no GPIO %d, SCK no GPIO %d ===\n", PIN_DT, PIN_SCK);
  Serial.println("  ?            esta ajuda");
  Serial.println("  d            diagnostico de fiacao (rode primeiro)");
  Serial.println("  r            leitura bruta a cada 0,5 s, com delta");
  Serial.println("  g            leitura em gramas a cada 0,5 s, com delta");
  Serial.println("  t            tara: define o zero com a plataforma vazia");
  Serial.println("  c <gramas>   calibra com a massa conhecida em cima (ex.: c 1000)");
  Serial.println("  b <gramas>   teste de cantos: mesma massa em 5 posicoes");
  Serial.println("  n            mede o ruido (100 leituras)");
  Serial.println("  s            mede a taxa de amostragem em Hz");
  Serial.println("  k            deriva ao longo de 5 min");
  Serial.println("  save         grava offset e escala na NVS");
  Serial.println("  reset        apaga a calibracao gravada");
  Serial.println("  (qualquer tecla interrompe r, g e k)");
  if (escala_ != 0.0f) {
    Serial.printf("  calibrado: offset=%ld  escala=%.3f contagens/g\n", offset_, escala_);
  } else {
    Serial.printf("  NAO calibrado: offset=%ld  (rode 't' e depois 'c <gramas>')\n", offset_);
  }
  Serial.println("  alimentacao: 3,3 V no VCC. Nunca 5 V - ver README.md");
  Serial.println();
}

// ------------------------------------------------------------- interpretador

static char linha[48];
static size_t linhaLen = 0;

static void executar(char *buf) {
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

  if (cmd == "?" || cmd == "h")  { ajuda(); return; }
  if (cmd == "d")     { diagnostico(); return; }
  if (cmd == "r")     { leituraContinua(false); return; }
  if (cmd == "g")     { leituraContinua(true); return; }
  if (cmd == "t")     { tara(); return; }
  if (cmd == "c")     { calibra(arg.toFloat()); return; }
  if (cmd == "b")     { testeCantos(arg.toFloat()); return; }
  if (cmd == "n")     { mediaRuido(); return; }
  if (cmd == "s")     { taxaAmostragem(); return; }
  if (cmd == "k")     { deriva(); return; }
  if (cmd == "save")  { salvaCalibracao(); return; }
  if (cmd == "reset") { limpaCalibracao(); return; }

  erro("comando '" + cmd + "' desconhecido - digite ?");
}

// ---------------------------------------------------------------- setup/loop

void setup() {
  Serial.begin(MONITOR_SPEED);
  delay(300);

  Serial.println();
  Serial.println("=== boot ===");

  hx.begin(PIN_DT, PIN_SCK);  // ganho 128 no canal A, que e o padrao
  carregaCalibracao();

  ajuda();
  diagnostico();  // a primeira coisa que interessa e saber se a fiacao esta certa
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
  delay(5);
}
