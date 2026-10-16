# Firmware do alimentador — BLE + servo + balança

Fase 1 do [doc 07](../../../docs/07-comunicacao-app-esp32.md): **só BLE**, sem
Wi-Fi e sem HTTP.

A balança pode ser a **real** (HX711 com as 4 células) ou **simulada**, conforme
`USE_HX711` em `src/config.h`. As duas ficam atrás da interface
`IWeightSource`, então a máquina de estados não sabe qual está em uso — foi por
isso que toda a lógica pôde ser testada antes de existir célula de carga.

Pasta irmã de [`../files/`](../files/), que é o código herdado e ficou intocado.
As correções aplicadas aqui estão documentadas em [REVISAO.md](REVISAO.md).

## Hardware

| Item | Ligação |
|---|---|
| Servo MG946R (ou MG995/MG996R) | sinal no **GPIO 18**, alimentação por fonte externa 5–6 V, **GND comum** com a ESP32 |
| Chaves de fim de curso | não usadas por ora (`USE_LIMIT_SWITCHES 0` em `src/config.h`) |
| XFW-HX711 + 4 células | `DT` → **GPIO 26**, `SCK` → **GPIO 25**, `VCC` → **3V3** (nunca 5 V) |

A ligação do servo é a mesma validada no teste de componente em
[`../../servo_mg996r/`](../../servo_mg996r/).

## Rodar

```powershell
cd teste_componentes\teste_esp\firmware_ble
pio run -t upload
pio device monitor
```

No monitor, envio de linha em **NL**. Digite `?` para o menu.

## Comandos pelo Serial

| Comando | O que faz |
|---|---|
| `w <gramas>` | peso na balança simulada — só com `USE_HX711 0` |
| `tara` | zera a balança real, com a plataforma vazia |
| `cal <gramas>` | calibra a balança real com a massa conhecida no centro |
| `st` | imprime o status agora |
| `open` / `close` | abre / fecha a porta manualmente |
| `reset` | rearma depois de uma falha |
| `min <g>` / `max <g>` | ajusta a janela de peso (persiste em NVS) |
| `sensor off` / `sensor on` | simula sensor mudo, para testar o estado seguro |
| `ev` | histórico de eventos |
| `cal on` / `cal off` | entra / sai do modo de calibração da porta |
| `pos <graus>` | move o servo para o ângulo (só em `cal on`) |
| `setclosed` / `setopen` | grava a posição atual como porta fechada / aberta |

O peso não salta para o valor pedido: ele **converge em rampa** ao longo de
`MOCK_RAMP_MS` (600 ms), para que a detecção de estabilidade da FSM seja
exercitada de verdade em vez de ser pulada.

## Roteiro de teste

```
w 4200        peso dentro da janela -> estabiliza -> porta abre
st            confirma door=open
w 0           plataforma vazia -> confirma saida (leaveMs) -> porta fecha
              e so entao grava a visita, com duracao e peso de pico
w 12000       peso fora da janela -> porta continua fechada
w 0
sensor off    -> evento fault, a porta fica onde esta
sensor on     -> volta a decidir
ev            lista as visitas acima, com seq, ts, durS e door
```

A visita só aparece no `ev` **depois** que o peso volta a zero: o protocolo v2
grava o evento no fim da visita, não no instante da decisão.

## Calibrar a porta

A porta é descrita em **graus**: `0` = fechada, `90` = aberta (o mecanismo usa
essas duas posições). A conversão para microssegundos usa `SERVO_US_AT_0DEG` e
`SERVO_US_AT_180DEG` em `src/config.h`, hoje 500 e 2500 µs — a faixa nominal da
família MG99x.

> **Antes de montar no mecanismo**, confirme que o *seu* servo aceita 500 e
> 2500 µs. Rode o comando `l` do teste em [`../../servo_mg996r/`](../../servo_mg996r/)
> com o horn livre: se ele parar de andar antes de chegar aos extremos, ajuste
> `SERVO_US_AT_0DEG` / `SERVO_US_AT_180DEG` para os valores encontrados. Sem
> isso o servo força o batente nas pontas e esquenta.

Com o mecanismo montado, o ajuste fino é feito sem trocar de firmware:

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

O dispositivo anuncia como **`Alimentador`**. MTU pedido: 185, para o JSON de
status caber numa notificação só.

O firmware fala o **protocolo Feed Cat v2**, especificado em
[`plano/01-app.md`](../../../plano/01-app.md) §7 — o mesmo que o app implementa
em `app/src/device/protocol.ts`. Escopo da **Etapa 2** do plano:

| Mensagem | Situação |
|---|---|
| `hello`, `cfg`, `cfgGet`/`cfgDump`, `cmd` (`close`, `refill`, `tare`) | implementado |
| `st` contínuo, `ev`, `sync`/`syncEnd` | implementado |
| `cats`, `mode`, `profile` | implementado (e os gatos já valem para decidir) |
| `cmd calibrate` | `ack ok:false, err:"no_scale"` — depende do HX711 |
| `grant`, `revoke` | `ack ok:false, err:"not_supported"` — Etapa 3 |

### App → ESP

```json
{"t":"hello","id":1,"v":2,"now":1789574400}
{"t":"cats","id":2,"cats":[{"k":1,"minG":2900,"maxG":3500,"rule":"allow"}]}
{"t":"cfg","id":3,"confMs":2000,"leaveMs":4000,"maxOpenS":900}
{"t":"cmd","id":4,"op":"refill","timeoutS":300}
{"t":"cmd","id":5,"op":"close"}
{"t":"cfgGet","id":6}
{"t":"sync","id":7,"since":120}
{"t":"mode","id":8,"mode":"learn"}
```

### ESP → app

```json
{"t":"ack","id":1,"ok":true}
{"t":"st","wG":3200,"cat":1,"door":"open","fcC":0,"fcO":1,"grant":null,"mode":"auto","cfgV":2}
{"t":"ev","seq":2,"ts":1789758972,"kind":"visit","cat":1,"wG":3200,"durS":5,"door":"opened","why":"rule"}
{"t":"cfgDump","cats":[...],"confMs":2000,"leaveMs":4000,"maxOpenS":300,"mode":"auto","cfgV":2}
{"t":"syncEnd","id":7,"last":5}
```

O `st` sai a cada segundo enquanto houver cliente conectado.

### Extensões fora do v2 (bancada)

Como não temos células de carga, duas mensagens a mais controlam a balança
simulada. Elas usam a chave **`c`**, enquanto o v2 usa **`op`** — por isso
convivem sem ambiguidade, e o app nunca as manda:

```json
{"t":"cmd","id":9,"c":"mock","wG":4200}
{"t":"cmd","id":10,"c":"sensor","ok":false}
```

### Detalhes que custaram para acertar

- **Relógio.** A ESP32 não tem RTC. O `ts` dos eventos só vira epoch depois que
  o app manda `hello` com `now`; antes disso vale 0. É o que o `seq` 1 (`boot`)
  mostra no histórico.
- **`seq` em NVS.** O contador nunca reinicia, nem em reflash — é por ele que o
  app sincroniza o histórico com `sync since`.
- **Uma visita é gerada no FIM**, quando a balança volta a zero, com a duração
  e o peso de pico. Não no instante da decisão.
- **`door` só aceita 5 valores** no app (`closed`, `opening`, `open`, `closing`,
  `fault`); qualquer outro faz o decodificador descartar a mensagem inteira. Por
  isso o estado `calibrating`, que é só de bancada, sai como `fault`.
- **MTU.** A maior notificação medida foi 108 bytes, dentro do limite de ~180.
  O `profile` é a exceção: se passar de 90 bytes, é omitido do `cfgDump` e o
  firmware avisa no log. Fragmentação fica para depois.

### Testar o protocolo sem o app

[`tools/v2_handshake.py`](tools/v2_handshake.py) imita a sequência que o app faz
ao conectar e verifica 15 pontos:

```powershell
python tools/v2_handshake.py
```

### Como conectar

> **BLE não aparece nas configurações de Bluetooth** do celular nem do Windows,
> e não existe "parear". Só um app que fale GATT enxerga o dispositivo.

**Pelo PC** — [`tools/ble_console.py`](tools/ble_console.py):

```powershell
pip install bleak
python tools\ble_console.py          # console interativo
python tools\ble_console.py --demo   # roteiro automático, só para conferir
```

No console você digita atalhos e ele traduz para o JSON do protocolo:

```
> w 4200          coloca 4200 g na balança
> open / close    abre / fecha
> status
> events
> cfg 3000 8000   janela de peso
> door 0 90       posições da porta, em graus
> sensor off
> quiet           esconde o status de 1 em 1 s
> q
```

JSON cru também passa direto: `{"t":"cmd","id":1,"c":"open"}`.

**Pelo celular** — nRF Connect, abaixo.

### Testar com o nRF Connect

1. Instale o **nRF Connect for Mobile** (Nordic) no celular.
2. Scan → conecte em `Alimentador`.
3. Abra o serviço `6e400001…`, na característica TX (`…0003`) toque no ícone de
   **notificações** (três setas) para se inscrever.
4. Na característica RX (`…0002`), envie como **texto**:
   `{"t":"cmd","id":1,"c":"mock","wG":4200}`
5. Deve chegar o `ack`, depois o evento `access_granted`, e o servo se move.

## Estrutura

| Arquivo | Papel |
|---|---|
| `src/config.h` | pinos, tempos, UUIDs, limites do servo |
| `src/IWeightSource.h` | interface da balança — permite trocar mock por HX711 |
| `src/IWeightSource.h` | interface: troca mock por HX711 sem mexer na lógica |
| `src/MockWeightSource.*` | balança simulada, com rampa e simulação de falha |
| `src/Hx711WeightSource.*` | balança real: filtro, estabilidade, tara e calibração |
| `src/DoorActuator.*` | porta com servo **posicional**, rampa, sem `delay()` |
| `src/AccessStateMachine.*` | decide liberar ou bloquear o acesso |
| `src/BleControl.*` | NUS; o callback só enfileira, quem age é o `loop()` |
| `src/Storage.*` | configuração em NVS |
| `src/EventLog.*` | log circular em RAM (RF11) |
| `src/main.cpp` | fiação dos módulos, menu serial e tradução do JSON |

## Notas

- Na primeira execução aparece `nvs_open failed: NOT_FOUND` no log. É esperado:
  o namespace da NVS ainda não existe e o `Storage` devolve os padrões. Some
  depois do primeiro `min`/`max`/`cfg`.
- `board_build.partitions = huge_app.csv` no `platformio.ini`: só o stack BLE já
  ocupava 89% da partição padrão de 1,3 MB.
- As posições da porta (0° e 90°) e a janela de peso vivem em **NVS**, não no
  código. Reflash não as apaga — se um teste antigo deixou valores estranhos,
  é por isso. Veja os valores em vigor com `?`.
