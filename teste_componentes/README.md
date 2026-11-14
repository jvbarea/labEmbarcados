# Testes de componente

Testes de bancada, um componente de cada vez, para medir o hardware real **antes** de escrever o
firmware do alimentador. Cada teste responde perguntas concretas e deixa os números registrados num
`resultados.md` que alimenta as decisões dos [`docs/`](../docs/).

## Convenção

Um subdiretório por componente, cada um com **projeto PlatformIO independente**:

```
<componente>/
├── platformio.ini
├── README.md        # objetivo, ligação elétrica, roteiro de bancada
├── resultados.md    # tabelas em branco para preencher durante o teste
└── src/main.cpp     # firmware de teste, com menu pelo Serial
```

Rodar sempre de dentro da pasta do teste:

```powershell
cd teste_componentes\<componente>
pio run -t upload
pio device monitor -b 115200
```

## Testes

| Teste | Componente | Status | O que valida |
|---|---|---|---|
| [servo_mg996r](servo_mg996r/) | TowerPro 180° (MG995 / MG996R) + ESP32-WROOM-32 | pronto para bancada | faixa de pulso útil, tempo de excursão, repetibilidade, consumo, comportamento sem pulso (D6) |
| [hx711](hx711/) | XFW-HX711 + 4 células de meia-ponte | pronto para bancada | diagnóstico de fiação, calibração, ruído, taxa de amostragem, deriva |
| _fim_de_curso_ | chave NC + pull-up | a fazer | debounce, falha segura com fio rompido |
| [teste_esp/firmware_ble](teste_esp/firmware_ble/) | ESP32 ↔ app (BLE) | pronto para bancada | protocolo JSON sobre NUS, FSM de acesso com balança mockada, porta com servo posicional |
| [teste_esp/files](teste_esp/files/) | — | referência | código herdado, intocado. Revisão em [REVISAO.md](teste_esp/firmware_ble/REVISAO.md) |

## Antes de ligar qualquer coisa

- Servo e células **não** são alimentados pelo pino 5V/VIN da ESP32. Fonte externa, **GND comum**.
- HX711 em **3,3 V**, nunca 5 V — é o problema E1 do [doc 02](../docs/02-revisao-implementacao.md).
- Evitar os GPIO 0, 2, 5, 12 e 15 (*strapping*) e 6–11 (flash). Entradas de chave fora dos 34–39,
  que não têm pull-up interno. Pinagem sugerida no [doc 07](../docs/07-comunicacao-app-esp32.md) §4.
