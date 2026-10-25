# Firmware do alimentador — BLE + servo + balança

Fase 1 do [doc 07](../../../docs/07-comunicacao-app-esp32.md): **só BLE**, sem
Wi-Fi e sem HTTP. Fala o protocolo Feed Cat v2 inteiro, o mesmo do app.

A balança pode ser a **real** (HX711 com as 4 células) ou **simulada**: são dois
ambientes do PlatformIO, `esp32dev` (real, o padrão) e `mock`. As duas ficam
atrás da interface `IWeightSource`, então a máquina de estados não sabe qual
está em uso — foi por isso que toda a lógica pôde ser testada antes de existir
célula de carga.

Pasta irmã de [`../files/`](../files/), que é o código herdado e ficou intocado.
As correções aplicadas aqui estão documentadas em [REVISAO.md](REVISAO.md).

## Hardware

| Item | Ligação |
|---|---|
| Servo MG946R (ou MG995/MG996R) | sinal no **GPIO 13** (D13, header esquerdo), alimentação por fonte externa 5–6 V, **GND comum** com a ESP32 |
| Chaves de fim de curso | não usadas por ora (`USE_LIMIT_SWITCHES 0` em `src/config.h`) |
| XFW-HX711 + 4 células | `DT` → **GPIO 26**, `SCK` → **GPIO 25**, `VCC` → **3V3** (nunca 5 V) |

A ligação do servo é a mesma validada no teste de componente em
[`../../servo_mg996r/`](../../servo_mg996r/).

## Rodar

Feche o monitor serial antes de gravar (senão: "Could not open COM6").

```powershell
cd teste_componentes\teste_esp\firmware_ble
pio run -t upload               # balanca REAL (HX711)
pio run -e mock -t upload       # balanca SIMULADA: 'w <gramas>' no serial
pio device monitor -b 115200
```

No monitor, envio de linha em **NL**. Digite `?` para o menu.

Trocar entre `esp32dev` e `mock` **não apaga** os gatos, os tempos, a calibração
nem o histórico: tudo isso mora na flash (NVS e LittleFS), não no programa.

## Comandos pelo Serial

| Comando | O que faz |
|---|---|
| `w <gramas>` | peso na balança simulada — só no ambiente `mock` |
| `tara` | zera a balança real, com a plataforma vazia |
| `calpeso <gramas>` | calibra a balança real com a massa conhecida no centro |
| `st` | status como o app recebe, mais o estado interno (máquina de estados, reabastecer, MTU) |
| `open` | reabastecer: abre e fecha sozinha depois de `maxOpenS` |
| `close` | fecha (com a porta em falha, rearma); com peso na plataforma espera esvaziar |
| `reset` | rearma tudo depois de uma falha |
| `min <g>` / `max <g>` | janela de peso de bancada — só vale **sem gatos cadastrados** |
| `sensor off` / `sensor on` | simula sensor mudo, para testar o estado seguro |
| `ev` | últimos 10 eventos do histórico |
| `evclear` | apaga o histórico da flash (o `seq` continua de onde estava) |
| `cal on` / `cal off` | entra / sai do modo de calibração do servo e da porta |
| `servo` | mostra a calibração do servo (vale também fora de `cal on`) |
| `us <valor>` | só em `cal on`: vai para o pulso em µs, em rampa |
| `+` / `-` | só em `cal on`: anda 10 µs a partir do alvo (`+25`, `-5` mudam o passo) |
| `set0` / `set90` | só em `cal on`: grava o pulso atual como 0° / 90° **físicos** |
| `pos <graus>` | só em `cal on`: vai ao ângulo pela calibração do servo |
| `setclosed` / `setopen` | só em `cal on`: grava o ângulo atual como porta fechada / aberta |

O peso simulado não salta para o valor pedido: ele **converge em rampa** ao longo
de `MOCK_RAMP_MS` (600 ms), para que a detecção de estabilidade da máquina de
estados seja exercitada de verdade.

## Roteiro de teste pelo serial (ambiente `mock`, sem gatos cadastrados)

```
w 4200        peso dentro da janela de bancada -> estabiliza -> porta abre
st            confirma door=open
w 0           plataforma vazia -> espera leaveMs -> porta fecha
              e so entao grava a visita, com a duracao e o peso
w 12000       peso fora da janela -> porta continua fechada
w 0
open          reabastecer: porta abre
w 6500        gato em cima...
close         ...o fechamento fica pendente (RNF04)
w 0           sai -> depois de leaveMs a porta fecha
sensor off    -> evento fault, a porta fica onde esta
sensor on     -> volta a decidir
ev            lista os eventos acima, com seq, ts, durS e door
```

## Regras do comedouro

São as mesmas do simulador do app ([`MockLink.ts`](../../../app/src/device/MockLink.ts)
e [`rules.ts`](../../../app/src/domain/rules.ts)), que funciona como especificação.
Moram em [`src/AccessStateMachine.cpp`](src/AccessStateMachine.cpp).

- **Visita.** Começa quando o peso passa de 300 g. A decisão sai depois de
  `confMs` com alguém em cima **e** a balança estável; só então a visita conta.
  Termina quando a plataforma fica vazia por `leaveMs`, e o evento é gravado
  nesse momento, com a duração até a saída e a maior leitura estável. Encostar e
  sair antes da decisão não gera evento.
- **Decisão.** Identifica o gato pela faixa de peso. Desconhecido: nega. Com
  liberação válida para ele: abre (e consome uma visita, se for por visitas).
  Senão vale a regra do cadastro (`allow`/`deny`). Sem nenhum gato cadastrado
  vale a janela de bancada (`min`/`max`).
- **Liberação** (`grant`). Por prazo (`untilS`, a hora do celular) ou por número
  de visitas. Fica **só na RAM**, de propósito: depois de uma queda de energia o
  gato em dieta volta a ser bloqueado ([docs/08](../../../docs/08-persistencia-dados.md) §4.1).
  Por prazo antes do `hello`: `no_clock`.
- **Modo adaptação** (`learn`). Abre para qualquer gato e marca a visita com
  `why: "learn"` e `learn: 1`. Com `DOOR_HOLD_OPEN 1` (o servo segura a tampa), a
  porta abre **a cada visita** e fecha quando o gato sai, em vez de ficar aberta
  por dias com o motor energizado. Ver `LEARN_DOOR_STAYS_OPEN` em `config.h`.
- **Reabastecer** (`refill`). Porta aberta até o prazo; as visitas continuam
  sendo registradas e a porta só fecha no fim. Com a porta em falha: `door_fault`.
- **RNF04.** A porta nunca fecha com peso na plataforma. Fechar com um gato em
  cima (pelo app, no fim do reabastecer ou ao sair da adaptação) fica pendente
  até a plataforma ficar vazia por `leaveMs`. O `ack` sai na hora.
- **Falha da porta.** Registrada uma vez (`door_timeout`); as visitas continuam
  sendo anotadas, sem mexer na porta. O `close` do app rearma.
- **Falha do sensor.** Estado seguro: a porta fica como está e a visita em curso
  é descartada (`sensor_fault`, uma vez por episódio).

## Calibrar o servo e a porta

A porta é descrita em **graus**: `0` = fechada, `90` = aberta. São duas
calibrações diferentes, nesta ordem:

1. **Do servo:** que pulso corresponde ao 0° e ao 90° *físicos* do eixo. Sem
   isso, "0°" e "90°" são só nomes.
2. **Da porta:** onde ela fecha e abre no mecanismo.

### 1. Servo, com o eixo livre

O firmware converte grau em pulso por uma reta entre dois pontos medidos:
`us(graus) = us0 + (us90 − us0) × graus / 90`. Os padrões (500 e 1500 µs) são a
faixa nominal da família MG99x e **nunca foram medidos no MG946R**. Se o servo
real parar antes de 500 µs, "0°" empurra o eixo contra o batente e o motor
esquenta.

Precisa de: horn no eixo e solto do mecanismo, fonte externa do servo ligada com
GND comum, e um transferidor ou esquadro com o corpo do servo alinhado a uma
linha.

```
cal on          energiza o eixo e para a maquina de estados
us 1500         vai ao meio, o ponto seguro
-               anda 10 us para baixo (repita, ou use '-25' para passos maiores)
                PARE ao primeiro sinal de esforco: zumbido grave, ou o eixo
                que nao anda mais
set0            grava o pulso atual como 0 graus FISICOS
+               sobe de 10 em 10 us ate o horn ficar a 90 graus no esquadro
set90           grava o pulso atual como 90 graus FISICOS
servo           confere o que ficou gravado
```

Depois **confira sem mexer na calibração**:

```
pos 0
pos 45          no transferidor deve cair em +-2 graus
pos 90
cal off         volta o eixo para a posicao de fechada
```

Se o 45° não cair em ±2°, o servo não é linear o bastante para dois pontos, e
a calibração não basta para a porta.

Se o eixo parar **antes** de chegar ao 0° físico, o 0° está além do batente do
servo. Não tem solução em software: tire o horn e reencaixe **um dente** no
estriado do eixo, depois repita.

Os dois pulsos ficam em **NVS** e sobrevivem a reboot e reflash. Reinicie a
placa e rode `?` ou `servo` para confirmar. Anote os valores em
[`../../servo_mg996r/resultados.md`](../../servo_mg996r/resultados.md).

### 2. Porta, com o mecanismo montado

O ajuste fino da porta é feito sem trocar de firmware:

```
cal on          servo energiza e a FSM para de decidir
pos 0           vai para 0 graus - veja se a porta fecha sem forcar
pos 5           ajuste fino, grau a grau, ate a posicao certa
setclosed       grava essa posicao como PORTA FECHADA
pos 90          agora a posicao aberta
pos 85          ajuste fino
setopen         grava como PORTA ABERTA
cal off         sai da calibracao e volta para a posicao de fechada
```

Os dois valores ficam em **NVS** e sobrevivem a reboot e reflash. O `?` mostra
os valores em vigor. Pelo BLE, o mesmo ajuste é
`{"t":"cfg","id":1,"closedDeg":0,"openDeg":90}`.

Em `cal on` a máquina de estados fica parada de propósito — sem isso ela
mandaria fechar a porta no meio da sua medição.

## Protocolo BLE

Nordic UART Service, reconhecido de cara pelo **nRF Connect**:

| | UUID |
|---|---|
| Serviço | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| RX (app → ESP, write) | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| TX (ESP → app, notify) | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |

O dispositivo anuncia como **`Alimentador`** e aceita **uma conexão por vez**:
com o app conectado, o PC não acha o comedouro (e vice-versa).

O firmware fala o **protocolo Feed Cat v2**, especificado em
[`plano/01-app.md`](../../../plano/01-app.md) §7 — o mesmo que o app implementa
em `app/src/device/protocol.ts`. **Todas as mensagens estão implementadas:**
`hello`, `profile`, `cfgGet`/`cfgDump`, `cats`, `grant`, `revoke`, `cmd`
(`refill`, `close`, `tare`, `calibrate`), `cfg`, `mode`, `sync`/`syncEnd`, `st`,
`ack` e `ev`.

### App → ESP

```json
{"t":"hello","id":1,"v":2,"now":1789574400}
{"t":"cats","id":2,"cats":[{"k":1,"minG":2700,"maxG":3700,"rule":"allow"},{"k":2,"minG":6000,"maxG":7000,"rule":"deny"}]}
{"t":"cfg","id":3,"confMs":2000,"leaveMs":4000,"maxOpenS":300}
{"t":"profile","id":4,"blob":"[{\"id\":1,\"name\":\"Mimi\",\"emoji\":\"🐱\",\"color\":\"laranja\"}]"}
{"t":"grant","id":5,"cat":2,"untilS":1789575300}
{"t":"grant","id":6,"cat":2,"visits":1}
{"t":"revoke","id":7,"cat":2}
{"t":"cmd","id":8,"op":"refill","timeoutS":300}
{"t":"cmd","id":9,"op":"close"}
{"t":"cmd","id":10,"op":"tare"}
{"t":"cmd","id":11,"op":"calibrate","refG":696}
{"t":"mode","id":12,"mode":"learn"}
{"t":"cfgGet","id":13}
{"t":"sync","id":14,"since":120}
```

### ESP → app

```json
{"t":"ack","id":1,"ok":true}
{"t":"ack","id":8,"ok":false,"err":"door_fault"}
{"t":"st","wG":6512,"cat":2,"door":"open","fcC":0,"fcO":1,"grant":{"cat":2,"visits":1},"mode":"auto","cfgV":12}
{"t":"ev","seq":41,"ts":1789758972,"kind":"visit","cat":1,"wG":3210,"durS":95,"door":"opened","why":"rule"}
{"t":"ev","seq":42,"ts":1789759100,"kind":"visit","cat":2,"wG":6480,"durS":30,"door":"opened","why":"learn","learn":1}
{"t":"cfgDump","cats":[...],"confMs":2000,"leaveMs":4000,"maxOpenS":300,"mode":"auto","cfgV":12,"profile":"[...]"}
{"t":"syncEnd","id":14,"last":45}
```

- O `st` sai a cada **500 ms** e **na hora** em que porta, gato, modo, liberação
  ou `cfgV` mudam.
- O `sync` responde o `ack` primeiro, depois manda **todos** os eventos com
  `seq > since`, um por vez sem travar o loop, e termina com `syncEnd`.
- `cats`, `cfg`, `profile` e `mode` com o mesmo conteúdo que já está gravado são
  confirmados **sem escrever na flash** e sem mudar o `cfgV`. O app reenvia tudo a
  cada conexão; sem isso cada conexão gastaria a flash.
- Códigos de erro em `err`: `door_fault`, `unknown_cat`, `no_clock`, `bad_range`,
  `bad_cats`, `bad_json`, `bad_type`, `bad_op`, `bad_cmd`, `door_moving`,
  `scale_mute`, `calib_failed`, `no_scale`, `missing_ref`, `too_big`,
  `real_scale`. Nos eventos de falha: `door_timeout`, `sensor_fault`.

### Mensagens maiores que um pacote

Uma notificação carrega no máximo MTU − 3 bytes (o firmware pede MTU 185), e o
`cfgDump` com os nomes e emoji dos gatos passa disso. Por isso toda escrita ou
notificação é:

- uma **mensagem inteira**: o JSON, que começa com `{`; ou
- um **pedaço**: cabeçalho `#i/n:` (i de 1 a n, n até 99) seguido de um trecho
  dos **bytes** da mensagem. O corte é por byte, então um emoji pode ficar
  dividido entre dois pedaços: quem recebe junta os bytes e só depois lê.

Pedaço fora de ordem ou faltando descarta a montagem. O firmware corta as
notificações pelo MTU que o celular negociou (no mínimo 23); o app escreve em
pacotes de até 180 bytes. Mesmo contrato em
[`src/BleControl.h`](src/BleControl.h), no app
([`framing.ts`](../../../app/src/device/framing.ts)) e no
[`tools/feedcat_ble.py`](tools/feedcat_ble.py).

### Extensões fora do v2 (bancada)

Usam a chave **`c`**, enquanto o v2 usa **`op`** — por isso convivem sem
ambiguidade, e o app nunca as manda:

```json
{"t":"cmd","id":20,"c":"mock","wG":4200}
{"t":"cmd","id":21,"c":"sensor","ok":false}
{"t":"cmd","id":22,"c":"status"}
{"t":"cmd","id":23,"c":"reset"}
{"t":"cmd","id":24,"c":"evclear"}
{"t":"cmd","id":25,"c":"evtrim","after":120}
{"t":"cfg","id":26,"closedDeg":0,"openDeg":90}
{"t":"cfg","id":27,"minG":3000,"maxG":8000}
```

`evtrim` apaga só os eventos com `seq` maior que `after`: o teste de bancada usa
para não deixar as visitas de teste no histórico que o app vai sincronizar.

### Histórico, relógio e `seq`

- **Histórico na flash.** Os últimos **256 eventos** ficam num arquivo do
  LittleFS ([docs/08](../../../docs/08-persistencia-dados.md) §4.2), e
  sobrevivem a queda de energia e a reflash. Com ~10 visitas por dia são ~25 dias
  sem conectar o celular antes de os mais antigos se perderem.
- **`seq` em NVS.** O contador nunca reinicia, nem em reflash nem com `evclear` —
  é por ele que o app sincroniza com `sync since`.
- **Relógio.** A ESP32 não tem RTC. A hora vem do `hello`. Eventos que nascem
  antes do primeiro `hello` de um boot (o `boot`, e o que acontecer antes de o
  celular conectar) têm o `ts` corrigido quando o `hello` chega, pelo tempo desde
  o boot. Os de um boot que nunca viu `hello` ficam com `ts = 0`.
- **Eventos ao vivo só depois do `hello`.** Antes disso o `ts` ainda é 0 e o app
  descartaria o evento. O que acontece sem ninguém conectado chega pelo `sync`.
- **`door` só aceita 5 valores** no app (`closed`, `opening`, `open`, `closing`,
  `fault`); qualquer outro faz o decodificador descartar a mensagem inteira. Por
  isso o estado `calibrating`, que é só de bancada, sai como `fault`.

## Testar sem o app

### 1. Simulador no PC, sem a placa

[`tools/host_sim/`](tools/host_sim/) compila os mesmos arquivos de `src/` para o
PC, com substitutos do Arduino, e faz o papel do celular. São 18 cenários e
mais de 120 verificações: visitas, liberações, adaptação, reabastecer, RNF04,
falha e rearme da porta, anel de 256 eventos, reboot com a configuração e o
histórico, MTU 23, entrada ruim e o ritmo do status. Roda em segundos:

```powershell
wsl bash tools/host_sim/run.sh
```

Precisa de `g++` (no WSL ou Linux) e de ter rodado `pio run` uma vez antes (para
baixar a ArduinoJson). Detalhes em [`tools/host_sim/README.md`](tools/host_sim/README.md).

### 2. Teste de bancada com a placa, pelo Bluetooth do PC

[`tools/v2_handshake.py`](tools/v2_handshake.py) faz o papel do app com a placa
de verdade e confere cada resposta:

```powershell
pip install bleak
pio run -e mock -t upload        # para os passos que injetam peso
python tools\v2_handshake.py
```

- A porta abre e fecha várias vezes: mãos longe do mecanismo.
- **Não tara nem calibra** a balança.
- Antes de começar tira um **retrato** do que está gravado (gatos, tempos, modo,
  perfil) e no fim devolve tudo como estava, inclusive apagando do histórico só os
  eventos que ele criou. O retrato fica salvo num arquivo; se o teste for
  interrompido, `python tools\v2_handshake.py --restaurar <arquivo>` devolve.
- Com a balança real (`esp32dev`), os passos que injetam peso são pulados.
- `--capturar msgs.jsonl` grava tudo o que chegou, uma mensagem por linha — serve
  para atualizar a fixture do app (`app/__tests__/fixtures/firmware-messages.jsonl`).
- Feche o app antes: o comedouro aceita uma conexão por vez.

### 3. Console BLE interativo

[`tools/ble_console.py`](tools/ble_console.py): digite atalhos e ele traduz para o
protocolo (com pedaços e `id`, como o app). `?` lista todos.

```
> hello           manda a hora do PC (faca primeiro)
> w 4200          coloca 4200 g na balanca simulada
> refill / close  abre para reabastecer / fecha
> grantv 2 1      libera o gato 2 por uma visita
> learn / auto    modo adaptacao / automatico
> sync            historico
> cfgget          o que esta gravado
> quiet           mostra/esconde o status de 500 ms
> q
```

> **BLE não aparece nas configurações de Bluetooth** do celular nem do Windows,
> e não existe "parear". Só um app que fale GATT enxerga o dispositivo.

### 4. nRF Connect, pelo celular

1. Instale o **nRF Connect for Mobile** (Nordic) no celular.
2. Scan → conecte em `Alimentador`.
3. Abra o serviço `6e400001…`, na característica TX (`…0003`) toque no ícone de
   **notificações** (três setas) para se inscrever.
4. Na característica RX (`…0002`), envie como **texto**:
   `{"t":"hello","id":1,"v":2,"now":1789574400}` e depois
   `{"t":"cmd","id":2,"c":"mock","wG":4200}` (ambiente `mock`).
5. Chegam o `ack` e o `st`; com a janela de bancada, a porta abre.

## Estrutura

| Arquivo | Papel |
|---|---|
| `src/config.h` | pinos, tempos, UUIDs, limites do servo, tamanhos do BLE e do histórico |
| `src/IWeightSource.h` | interface da balança: troca mock por HX711 sem mexer na lógica |
| `src/MockWeightSource.*` | balança simulada, com rampa e simulação de falha |
| `src/Hx711WeightSource.*` | balança real: filtro, estabilidade, tara e calibração |
| `src/DoorActuator.*` | porta com servo **posicional**, rampa, partida suave, sem `delay()` |
| `src/AccessStateMachine.*` | as regras: visitas, decisão, liberações, adaptação, reabastecer, RNF04 |
| `src/BleControl.*` | NUS, pedaços `#i/n:`, fila de envio; o callback só enfileira |
| `src/AppProtocol.*` | o protocolo v2: traduz mensagens em comandos e estado em mensagens |
| `src/Storage.*` | configuração e perfil em NVS |
| `src/EventLog.*` | histórico em anel no LittleFS, `seq` em NVS, hora corrigida no `hello` |
| `src/main.cpp` | fiação dos módulos e menu serial |
| `tools/host_sim/` | simulador no PC (sem placa) |
| `tools/v2_handshake.py` | teste de bancada pelo Bluetooth do PC |
| `tools/ble_console.py` | console BLE interativo |
| `tools/feedcat_ble.py` | conexão, pedaços e acks, usado pelos dois scripts acima |

## Notas

- Na primeira execução aparece `nvs_open failed: NOT_FOUND` no log. É esperado:
  o namespace da NVS ainda não existe e o `Storage` devolve os padrões.
- Na primeira execução deste firmware o LittleFS formata a partição de dados
  (menos de 1 s). O boot mostra quantos eventos achou e a memória livre.
- `board_build.partitions = huge_app.csv` no `platformio.ini`: só o stack BLE já
  ocupava 89% da partição padrão de 1,3 MB. A mesma tabela tem a partição de
  dados usada pelo histórico.
- **O servo segura a porta aberta.** A tampa não se sustenta sozinha na posição de
  aberta (ao soltar o servo ela caiu), então com `DOOR_HOLD_OPEN 1` em `config.h`
  ele fica energizado enquanto a porta está `open` e só solta depois de fechada,
  ou em falha. Isso é uma exceção deliberada ao RNF04. O custo é que o motor
  segura a carga o tempo todo: esquenta, treme um pouco, e a fonte precisa aguentar
  a corrente de sustentação. Confira a temperatura do servo e a tensão da fonte
  depois de alguns minutos aberta (o reabastecer pode durar até `maxOpenS`).
  `DOOR_HOLD_OPEN 0` volta a soltar ao abrir.
- As posições da porta (0° e 90°), a janela de bancada e a calibração vivem em
  **NVS**, não no código. Reflash não as apaga — se um teste antigo deixou
  valores estranhos, é por isso. Veja os valores em vigor com `?`.
