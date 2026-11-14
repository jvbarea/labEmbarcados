# Alimentador de Animais — Firmware ESP32

Firmware modular (RNF07) para o projeto da disciplina PCS3858. Estrutura em
camadas: aquisição (`WeightSensor`), atuação (`DoorActuator`), lógica de
controle (`AccessStateMachine`), persistência (`Storage`), comunicação
(`BleProvisioning`, `WifiManager`, `HttpApi`) e log de eventos (`EventLog`).

## Como abrir

Abra a pasta `AlimentadorAnimais` inteira no Arduino IDE (clique no
`AlimentadorAnimais.ino`) — os demais `.h`/`.cpp` são compilados juntos
automaticamente, sem precisar de configuração extra.

## Placa e bibliotecas

- **Placa**: ESP32 Dev Module (pacote *esp32* by Espressif Systems, no
  Boards Manager do Arduino IDE).
- **Bibliotecas** (Library Manager):
  - `HX711` (bogde/HX711)
  - `ESP32Servo` (madhephaestus/ESP32Servo)
  - `ArduinoJson` (bblanchon/ArduinoJson, v6.x)
  - BLE (`BLEDevice.h`) e `WiFi.h`/`WebServer.h`/`Preferences.h` já vêm
    inclusas no core ESP32, não precisam ser instaladas.

## Fiação (ajustar em `config.h` conforme a montagem real)

| Sinal                    | Pino ESP32 |
|---------------------------|-----------|
| HX711 DT                  | GPIO 4    |
| HX711 SCK                 | GPIO 5    |
| Servomotor (PWM)          | GPIO 13   |
| Chave fim de curso — aberta | GPIO 14 |
| Chave fim de curso — fechada| GPIO 27 |

As chaves fim de curso são lidas em `INPUT_PULLUP` (nível baixo = acionada).
Lembre-se de aterrar a fonte de 5 V do servo/células junto com o GND do ESP32
(conforme a Figura 1 da consolidação).

## Premissas assumidas (não estavam explícitas na especificação)

Como pedido, seguem documentadas para vocês ajustarem se necessário:

1. **Servo 360° = rotação contínua.** "Abrir" e "fechar" giram o motor em
   sentidos opostos (pulsos de 1000 µs / 2000 µs) até a chave fim de curso
   correspondente ser acionada; ao chegar lá, o firmware envia o pulso
   neutro (1500 µs) e desacopla o servo (`detach()`), atendendo à RNF04
   ("servo desenergizado ao fim do movimento"). Se o servo de vocês tiver
   outra convenção de sentido, basta inverter `SERVO_US_OPEN_DIR` e
   `SERVO_US_CLOSE_DIR` em `config.h`.
2. **BLE sempre ativo**, não só na "fase 1". Ele continua anunciando mesmo
   depois de conectado ao Wi-Fi, servindo como canal de reconfiguração/
   fallback. Se preferirem desligar o BLE após o provisionamento (para
   economizar energia/RAM), é só chamar `BLEDevice::deinit()` depois da
   primeira conexão Wi-Fi bem-sucedida.
3. **Formato da API HTTP.** Mantive genérico e em JSON, como vocês pediram:
   `GET/POST /api/config`, `POST /api/door`, `POST /api/calibrate`,
   `GET /api/status`, `GET /api/events`. Sem autenticação por enquanto —
   fácil de adicionar depois (ex.: token fixo em header) se for exigido.
4. **Sem MQTT ainda.** A consolidação cita MQTT como opção da fase 2, mas
   o pedido de vocês foi especificamente HTTP; o `HttpApi` foi escrito de
   forma isolada, então dá para adicionar um cliente MQTT em paralelo sem
   mexer na lógica de acesso.
5. **Limiar de "plataforma vazia"** (`WEIGHT_EMPTY_THRESHOLD_G`, 300 g por
   padrão) e **tolerância de estabilidade** (`WEIGHT_STABILITY_TOLERANCE_G`,
   80 g) são hipóteses de engenharia, não valores da especificação — ajustem
   depois dos primeiros testes com as células de carga reais (semana 3 do
   cronograma, "Balança calibrada").
6. **Fator de calibração inicial** (`HX711_CALIBRATION_DEFAULT`) é um
   valor de partida qualquer; o valor real deve vir da rotina de
   calibração com massa-padrão (RF07), acessível tanto localmente
   (poderiam adicionar um botão físico futuramente) quanto via
   `POST /api/calibrate`.

## O que falta para fechar o firmware

- Definir a massa mínima acima da qual a plataforma é considerada "ocupada"
  com base em testes reais (hoje é o mesmo `WEIGHT_EMPTY_THRESHOLD_G`).
- Validar o tempo total de RNF02 (<3 s da subida do animal até a porta
  abrir) na prática: hoje ele é a soma do tempo de estabilização
  (`DEFAULT_STABILIZE_MS` = 800 ms, mais o tempo da janela de estabilidade
  do filtro) e do tempo de curso do servo até a chave fim de curso.
- RF11 hoje guarda o log em RAM (até 40 eventos, perde ao desligar). Se
  quiserem manter histórico entre reinícios, dá para persistir em NVS ou,
  melhor ainda, deixar o app puxar via HTTP periodicamente e guardar do
  lado dele.
