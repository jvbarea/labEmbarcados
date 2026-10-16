# Revisão do firmware em `teste_esp/files/`

Revisão do código herdado, feita antes de reaproveitá-lo. O original continua
em [`../files/`](../files/) sem nenhuma alteração; as correções vivem neste
projeto (`firmware_ble/`).

A arquitetura do original é boa e foi mantida quase inteira: camadas separadas,
`AccessStateMachine` recebendo dependências por ponteiro sem tocar em hardware.
Foi justamente isso que permitiu trocar a balança por um mock sem reescrever a
lógica de decisão.

## Bloqueantes

### 1. A máquina de estados travava em `SAFE_FAULT`

`AccessStateMachine.cpp:56` usava `sensor_->isReady()` como indicador de saúde
do sensor. Só que `isReady()` devolvia `hx_.is_ready()`, que na biblioteca do
bogde é apenas `digitalRead(DOUT) == LOW` — o flag de "tem amostra nova
disponível", não de "o sensor está vivo". Entre conversões, a 10 Hz, ele é
**falso na maior parte do tempo**. Como `update()` roda milhares de vezes por
segundo, quase toda iteração caía no ramo de falha e chamava
`enterState(SAFE_FAULT)`. A lógica automática nunca chegava a executar.

**Correção:** `IWeightSource::isHealthy()` passou a ser "faz mais de
`WEIGHT_HEALTH_TIMEOUT_MS` que não chega amostra válida", que é o que a FSM
realmente precisa saber.

### 2. O servo estava modelado como 360° de rotação contínua

`config.h` definia `SERVO_US_STOP 1500`, `OPEN_DIR 1000`, `CLOSE_DIR 2000`, e o
`DoorActuator` girava o motor até a chave de fim de curso atuar. Com o servo
posicional que temos (MG946R), escrever 1000 µs manda o eixo para 0° e **para
lá** — a chave nunca é atingida por movimento contínuo, o timeout estoura e a
porta vai para `FAULT` em todo ciclo.

**Correção:** `SERVO_US_DOOR_CLOSED` / `SERVO_US_DOOR_OPEN` são posições, o
movimento é uma rampa até o alvo, e as chaves viraram confirmação opcional
(`USE_LIMIT_SWITCHES`) em vez de sensor primário.

### 3. Faltava `ESP32PWM::allocateTimer()`

`DoorActuator.cpp:22` chamava `attach()` direto. A ESP32Servo exige a alocação
de timer antes — é o que o teste de bancada em
[`../../servo_mg996r/`](../../servo_mg996r/) faz e que funcionou na placa.

### 4. Nascia em `FAULT` sem as chaves ligadas, e não havia como rearmar

`DoorActuator.cpp:9`: `state_ = limitClosedActive() ? CLOSED : FAULT`. Em
`INPUT_PULLUP` sem chave conectada lê HIGH, então o boot já entrava em falha.
Pior: não existia comando de rearme em lugar nenhum, embora o
[doc 05](../../../docs/05-servo-360-fim-de-curso.md) §4 previsse
`FALHA → HOMING` por comando do app.

**Correção:** homing no `begin()` (escreve a posição de fechado uma vez) e
`DoorActuator::reset()` / `AccessStateMachine::reset()`, expostos no comando
`reset` do serial e do BLE.

## Sérios

### 5. Conectar ao Wi-Fi congelava o loop por até 15 s

`WifiManager.cpp:11` fazia `delay(200)` em laço até `WIFI_CONNECT_TIMEOUT_MS`,
chamado de dentro do `loop()` via `consumePendingCredentials()`. Durante esses
15 s o `doorActuator.update()` não rodava — ou seja, **o timeout de movimento
da porta não era avaliado com o servo energizado**. Num servo de rotação
contínua isso destruiria o mecanismo.

*Fora do escopo desta fase (sem Wi-Fi), mas precisa ser resolvido antes da fase
2 — com `WiFi.begin()` não bloqueante e checagem de estado no loop.*

### 6. `manualClose()` ignorava a janela de confirmação

`AccessStateMachine.cpp:39-52` não chamava `enterState()`, então
`stateEnteredMs_` continuava valendo de um estado antigo. No `update()`, a
condição `millis() - stateEnteredMs_ >= vacateConfirmMs` já estava satisfeita e
a porta fechava no instante em que a plataforma esvaziava, sem os 1,5 s que o
RNF04 pede.

**Correção:** timer próprio (`manualCloseSinceMs_`), reiniciado se o animal
voltar para a plataforma.

### 7. `WIFI_MODE_APSTA` desnecessário

`WifiManager.cpp:6` subia um SoftAP que ninguém usava, gastando RAM e tempo de
rádio — justamente o que não sobra com BLE ativo junto. Deveria ser `WIFI_STA`.

### 8. A FSM não observava `DoorState::FAULT`

Se a porta falhasse, a máquina seguia em `ACCESS_OPEN` achando que estava
aberta. **Corrigido:** registra o evento `door_fault` uma vez e para de decidir
até alguém rearmar.

### 9. `tare()` e `calibrateWithKnownMass()` bloqueavam ~1 s

`read_average(10)` a 10 Hz, dentro de um handler HTTP. *Volta a valer quando o
HX711 entrar.*

### 10. `delay(50)` dentro de `update()`

`DoorActuator.cpp:31`, no método que o header prometia ser não-bloqueante.
**Corrigido:** o novo `DoorActuator` não tem nenhum `delay()`.

## Menores

| # | Achado | Situação |
|---|---|---|
| 11 | Pinagem divergente do [doc 07](../../../docs/07-comunicacao-app-esp32.md) §4: servo 13 (vs 18), HX711 4/5 (vs 26/25), chaves 14/27 (vs 32/33) | corrigido |
| 12 | **GPIO 5 é pino de strapping** e estava no SCK do HX711 | corrigido na anotação de pinos |
| 13 | Race no BLE: `pendingSsid_`/`pendingPass_` eram `String` escritas na task do BLE e lidas no `loop()`; o `volatile bool` protegia o flag, não as strings | corrigido com fila FreeRTOS |
| 14 | `String(c->getValue().c_str())` quebra no core ESP32 3.x, onde `getValue()` já devolve `String` | corrigido com código tolerante às duas versões |
| 15 | Credenciais Wi-Fi por BLE sem pareamento nem criptografia | fora do escopo; registrar como risco conhecido |
| 16 | `DOOR_MOVE_TIMEOUT_MS` de 4 s conflitava com o RNF02 de "< 3 s" | timeout agora é guarda de falha (5 s), não o tempo de curso |
| 17 | `WEIGHT_EMPTY_THRESHOLD_G` é `#define`, não configurável pelo app | mantido; anotado |
| 18 | BLE parava de anunciar após a primeira desconexão | corrigido em `ServerCallbacks::onDisconnect` |

## Escopo desta fase

Wi-Fi e HTTP ficaram **de fora**, seguindo a fase 1 do
[doc 07](../../../docs/07-comunicacao-app-esp32.md) §3. Os arquivos
`WifiManager.*` e `HttpApi.*` não foram portados. Com isso, os itens 5, 7, 9 e
15 saem do caminho por ora e voltam à mesa quando o Wi-Fi entrar.

A balança virou `MockWeightSource` por trás da interface `IWeightSource`, já que
não temos células de carga. Quando o HX711 chegar, basta escrever
`Hx711WeightSource : public IWeightSource` — a FSM não muda uma linha.

## O que ainda falta

- [ ] Calibrar `SERVO_US_DOOR_CLOSED` / `SERVO_US_DOOR_OPEN` com a maquete real
      (hoje 1100/1900 µs são chutes) usando o comando `l` do teste do servo.
- [ ] Implementar `Hx711WeightSource` quando as células chegarem.
- [ ] Wi-Fi não bloqueante (item 5) antes da fase 2.
- [ ] Ligar as chaves de fim de curso e trocar `USE_LIMIT_SWITCHES` para 1.
- [ ] Conectar o app de [`app/`](../../../app/) ao protocolo BLE deste firmware.
