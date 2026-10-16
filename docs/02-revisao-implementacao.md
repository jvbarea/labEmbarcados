# 02 — Revisão da implementação anterior

Revisão de código do repositório `lab-digi-ii` (somente leitura, nada foi alterado lá).
Caminhos relativos a `lab-digi-ii/projeto/`.

Severidade: 🔴 crítico (quebra funcionalidade) · 🟠 importante (falha intermitente / risco) · 🟡 menor (qualidade)

---

## Resumo

| # | Onde | Problema | Sev. |
|---|---|---|---|
| F1 | `semana3/Verilog/circuito_uc.v:41` | Laço combinacional no toggle da porta (`abrir = ~abrir`) | 🔴 |
| F2 | `semana3/Verilog/circuito_fd.v:59-66` | Protocolo serial sem enquadramento (qualquer byte perdido/extra desalinha tudo) | 🔴 |
| F3 | `circuito_fd.v:58` × `:69-70` × `circuito_tb.v:52-63` | Ordem min/max contraditória entre comentário, código e testbench | 🟠 |
| F4 | `comporta_uc.v:44-45` | Um glitch de 1 ciclo em `abrirComporta` dispara abertura completa | 🟠 |
| F5 | `circuito_fd.v:90` | Sem histerese / tempo de confirmação; porta fecha na hora que o peso sai da faixa | 🟠 |
| F6 | `comporta_uc.v:21-28` | Estados com códigos duplicados (`mudaPosicao2 = esperaFechar = mudaPosicao3 = 4'b0100`) | 🟡 |
| F7 | `servo/contador_updown.v:20-23` | Parâmetros `M`/`N` ignorados; saídas `fim`/`meio` nunca atribuídas | 🟡 |
| F8 | `circuito_tb.v:18-23` | Testbench não liga `abrir` nem `select_ucs` (X na simulação da UC da comporta) | 🟡 |
| E0 | ESP32 | **Firmware não versionado** | 🔴 |
| E1 | ESP32 + HX711 | HX711 em 5 V → DT com nível de 5 V no GPIO da ESP (não tolerante a 5 V) | 🟠 |
| E2 | ESP32 | HX711 em GPIO12 (pino de *strapping*) pode impedir o boot | 🟠 |
| E3 | ESP32 → FPGA | Se usar `Serial` (UART0), log de boot/debug vai parar na FPGA | 🟠 |
| A1 | `app/(tabs)/index.tsx` | HTTP em texto puro é bloqueado em builds *release* do Android | 🔴 |
| A2 | app | Rede "Feed Cat" sem internet → Android pode mandar o tráfego pelos dados móveis | 🔴 |
| A3 | `index.tsx:36-38` | Erro ao enviar Wi-Fi é engolido em silêncio | 🟠 |
| A4 | `index.tsx:10,31` | IP da ESP só em memória; perdido ao fechar o app; sem descoberta | 🟠 |
| A5 | `index.tsx:102` | Estado da porta é "chutado" pelo app (toggle local, sem leitura real) | 🟠 |
| A6 | `index.tsx:48-59` | Validação aceita `NaN` e gera números com erro de ponto flutuante | 🟠 |
| A7 | todos os `fetch` | Sem timeout; app pode ficar "pendurado" | 🟡 |
| A8 | app + ESP | Sem autenticação; senha fixa `12345678`; credenciais do Wi-Fi em HTTP aberto | 🟡 |
| A9 | app | Expo SDK 51 desatualizado; arquivos do template sobrando | 🟡 |

---

## FPGA

### F1 🔴 Laço combinacional no toggle da porta

```verilog
// circuito_uc.v:40-41
always @(*) begin
    abrir = (Eatual == inicial) ? 1'b0 : (Eatual == mudarPosicao) ? ~abrir : abrir;
```

`abrir` depende de si mesmo dentro de um bloco combinacional:

- nos estados `esperaDado`/`armazenaDado` vira um **latch** (`abrir = abrir`);
- no estado `mudarPosicao` vira `abrir = ~abrir`, isto é, um **oscilador em anel** durante um ciclo de clock.
  O valor que "sobra" quando o estado muda é imprevisível (depende de atrasos de roteamento).

Consequência: o comando `'#'` vindo do app (via ESP) **pode ou não** inverter a porta, e às vezes pode inverter
e desinverter. Do ponto de vista do usuário, parece que "a ESP não recebeu o comando".
É forte candidato a parte do problema relatado (ver [03](03-problema-comunicacao-app-esp.md)).

**Correção proposta:** guardar o estado da porta em um flip-flop e só alternar na borda:

```verilog
always @(posedge clock or posedge reset)
    if (reset)                     abrir <= 1'b0;
    else if (Eatual == mudarPosicao) abrir <= ~abrir;
```

Melhor ainda: trocar o *toggle* por comandos explícitos (`'A'` = abrir, `'F'` = fechar), para app e hardware nunca
ficarem dessincronizados.

### F2 🔴 Protocolo serial sem enquadramento

Os dígitos entram num registrador de deslocamento de 56 bits e a posição de cada campo depende **só da
quantidade de bytes já recebidos**. Não há byte de início, fim, tamanho ou checksum. Qualquer um destes casos
desalinha max/min/atual até a próxima reinicialização "sortuda":

- ESP usa `Serial.println(...)` → `\r\n` entram como dígitos (`0x0D - 0x30 = 0xDD`);
- ESP envia peso sem zero à esquerda (`"5"` em vez de `"05"`) ou com 3 dígitos (`"100"`);
- ESP reinicia no meio de uma transmissão, ou imprime log de boot (ver E3);
- ruído na linha gera um byte a mais.

Resultado prático: comparação com números absurdos → porta nunca abre (ou abre sempre).
**Visto de fora, parece que "o dado não chegou".**

**Correção proposta:** quadro de tamanho fixo com delimitadores e verificação, por exemplo:

```
<  M M  N N  A A  C  >
'<' max(2) min(2) atual(2) checksum(1) '>'
```

A UC da recepção zera o índice ao ver `'<'`, só grava os campos num registrador de "sombra" e só copia para os
registradores usados pelos comparadores quando chegar `'>'` com checksum válido. Isso também elimina o F4.

### F3 🟠 Ordem de min/max contraditória

| Fonte | Ordem dos campos |
|---|---|
| Comentário `circuito_fd.v:58` | comando, **min**, **max**, atual |
| Código `circuito_fd.v:69-70` | **max** = `[47:32]`, **min** = `[31:16]` |
| Testbench `circuito_tb.v:52-63` | comentário diz "min = 10, max = 20", mas envia `'1','0','2','0'` → grava max=10, min=20 |

Se o firmware da ESP seguiu o comentário, **min e max chegam trocados**; com min > max a faixa fica vazia e a
porta **nunca abre automaticamente**. Precisa ser conferido no firmware.

### F4 🟠 Glitch dispara abertura completa

Em `comporta_uc.v`, o sinal `abrirComporta` só é consultado nos estados `inicial` e `esperaFechar`.
Uma vez em `prepara`, a máquina percorre as posições 0→7 sem reconsiderar. Enquanto os 6 bytes estão entrando no
registrador (≈0,5 ms), os comparadores veem valores intermediários e `perteceAoIntervalo` pode pulsar por alguns
ciclos → a porta abre (≈140 ms) e fecha de novo. Isso aparece como "tremidas" da porta.

### F5 🟠 Sem histerese nem tempo de confirmação

`perteceAoIntervalo` é puramente combinacional (`circuito_fd.v:90`). O texto do app promete "a abertura pode levar
até 2 segundos para garantir que o pet certo está na balança", mas **isso não existe na FPGA** (talvez exista na
ESP — firmware desconhecido). Sem isso:

- peso oscilando perto do limite → porta abre/fecha repetidamente;
- gato tira duas patas da balança enquanto come → porta **fecha na cabeça do gato**.

**Proposta:** exigir peso na faixa por T_conf (ex.: 1–2 s) para abrir; exigir peso fora da faixa por T_saída
(ex.: 3–5 s) para fechar; usar faixa de histerese (ex.: ±100 g).

### F6 🟡 Códigos de estado duplicados

```verilog
parameter mudaPosicao2     = 4'b0100;
parameter esperaFechar     = 4'b0100;
parameter mudaPosicao3     = 4'b0100;
parameter esperaIntervalo2 = 4'b0011;   // igual a esperaIntervalo
```

Não quebra hoje (os duplicados não são usados no `case`), mas qualquer um que use `mudaPosicao2` achando que é outro
estado cria um bug difícil de ver. Remover.

### F7 🟡 `contador_updown`

- Instancia `contador_m #(.M(14), .N(4))` fixo, ignorando os parâmetros do módulo.
- `fim` e `meio` são `output reg` nunca atribuídos.
- `always @(posedge clock or posedge zera_as)` sem usar `zera_as` no corpo.

### F8 🟡 Testbench incompleto

`circuito_tb.v` não conecta `abrir` nem `select_ucs`. `abrir` fica em `z`, então `abrir | abrirComporta` vale `x`
e a `comporta_uc` não é realmente simulada. O `#(100000000000000*clockPeriod)` final também faz a simulação
praticamente não terminar.

---

## ESP32 (deduzido — firmware ausente)

### E0 🔴 Firmware não está no repositório

Sem o código da ESP não dá para **confirmar** a causa do problema relatado; só levantar hipóteses e testá-las.
No projeto novo, o firmware deve ficar versionado neste repositório desde o primeiro dia.

### E1 🟠 HX711 alimentado em 5 V

No diagrama de ligação, o VCC do HX711 vai ao pino **5V** da placa. O pino DT (DOUT) do módulo sai com nível de VCC,
ou seja, **5 V entrando num GPIO da ESP32**, que não é tolerante a 5 V. Pode funcionar "por um tempo" e degradar
o pino. O HX711 funciona de 2,6 V a 5,5 V: **alimentar em 3,3 V** resolve.

### E2 🟠 GPIO12 é pino de *strapping*

O diagrama liga DT/SCK nos GPIO12/GPIO13. O GPIO12 (MTDI) define a tensão da flash no boot: se estiver em nível
alto ao ligar, a ESP32 pode não inicializar. Se o DT (que fica em nível alto enquanto o HX711 não tem leitura
pronta) estiver nesse pino, a placa pode falhar no boot de forma intermitente. **Evitar GPIO 0, 2, 5, 12, 15**
para sensores; preferir, por exemplo, GPIO 16–19, 21–23, 25–27, 32–33.

### E3 🟠 UART compartilhada com USB/log

Se a ESP falava com a FPGA pela `Serial` (UART0, GPIO1/3), a mesma linha recebe o log de boot da ESP32 e qualquer
`Serial.print` de depuração. Somado ao F2, isso desalinha o registrador da FPGA a cada reset da ESP.
Usar `Serial2` (ex.: GPIO17 TX) só para a FPGA e **terra comum** entre as placas.

---

## App

### A1 🔴 HTTP em texto puro bloqueado em builds de produção (Android)

O app usa `http://192.168.4.1/...`. A partir do Android 9 (API 28), tráfego **cleartext** é bloqueado por padrão.
No Expo, o *Expo Go* e builds de desenvolvimento liberam, mas um **APK/AAB gerado com `eas build`**
(perfis `preview`/`production` do `eas.json`) **não libera**. O `app.json` não tem `expo-build-properties` com
`usesCleartextTraffic`. Nesse cenário **toda** requisição falha com `Network request failed` e a ESP nunca vê nada.

### A2 🔴 Wi-Fi sem internet → tráfego vai pelos dados móveis

A rede "Feed Cat" (AP da ESP) não tem internet. Muitos Android detectam isso e mantêm os **dados móveis como rede
padrão**; o `fetch` para `192.168.4.1` sai pela operadora e dá timeout. Apps profissionais de provisionamento
chamam `bindProcessToNetwork` para forçar o uso do Wi-Fi — o `fetch` do React Native não faz isso.
No iOS 14+ há ainda a permissão de "Rede Local" (`NSLocalNetworkUsageDescription`) e o ATS bloqueando HTTP.

### A3 🟠 Erro engolido

```ts
// index.tsx:36-38
} catch (error) {
  return;
}
```

Se o envio do Wi-Fi falha, o usuário não recebe nenhum aviso. Parece que "foi", mas não foi.

### A4 🟠 IP volátil

Depois de mandar o Wi-Fi, o app troca `esp32Ip` pelo IP devolvido — mas só na memória (`useState`).
Fechou o app, volta a ser `192.168.4.1`. Além disso, se a ESP muda de canal ao conectar no roteador (modo AP+STA),
o AP cai **antes** da resposta chegar, e o app nunca descobre o IP novo. A partir daí, todo comando vai para o IP
errado.

### A5 🟠 Estado da porta "de mentira"

O botão alterna `doorStatus` localmente quando o HTTP responde `ok`. Mas a ESP/FPGA trabalham com *toggle*
(`'#'`), a FPGA não devolve estado e o F1 torna o toggle não confiável. App e porta divergem com facilidade
(ex.: o gato abriu a porta automaticamente, o app ainda mostra "Abrir Porta").

### A6 🟠 Validação e conversão numérica

```ts
const sanitizedMinWeight = parseFloat(minimumWeight.replace(/[,]/g, '.')) * 1000;
```

- Texto inválido (`"abc"`, `"1.2.3"` parcial, vazio após espaço) gera `NaN`; todas as comparações com `NaN` dão
  `false`, então **passa por todas as validações** e o app envia `"NaN"`.
- Erro de ponto flutuante: testando todos os valores de 0,50 a 9,90 com 2 casas, **15 geram números não inteiros**,
  por exemplo `2.01 → "2009.9999999999998"`, `4.03 → "4030.0000000000005"`. Se a ESP converter truncando,
  2,01 kg vira 2009 g.
- Unidades inconsistentes: app manda gramas como *string*; a FPGA quer 2 dígitos em décimos de kg; a conversão fica
  escondida na ESP. Mensagem diz "máximo 10 kg" e a tela de ajuda diz "9,9 kg".

**Proposta:** o app envia **inteiro em gramas** (`Math.round(x * 1000)`), valida `Number.isFinite`, e o protocolo
define explicitamente unidade e faixa.

### A7 🟡 Sem timeout

`fetch` no React Native não tem timeout padrão. Usar `AbortController` com ~5 s e mostrar estado "enviando…".

### A8 🟡 Segurança

AP com senha fixa pública (`12345678`, escrita na própria tela de ajuda), nenhuma autenticação nos endpoints e
senha do Wi-Fi da casa trafegando em HTTP. Qualquer pessoa próxima pode abrir a porta ou trocar a configuração.
Para um protótipo de laboratório é aceitável, mas vale citar no relatório e, no BLE, usar *pairing/bonding*.

### A9 🟡 Manutenção

- Expo SDK 51 / RN 0.74 (2024). O Expo Go da loja só roda o SDK mais recente, então o projeto antigo **não abre** no
  Expo Go atual sem atualizar.
- Sobraram arquivos do template (`HelloWave`, `ParallaxScrollView`, `react-logo*.png`, `scripts/reset-project.js`,
  teste de `ThemedText`), `README.md` padrão.
- Estilo `container` definido e não usado; `label` idem.
