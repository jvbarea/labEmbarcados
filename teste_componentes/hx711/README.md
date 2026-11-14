# Teste — Módulo XFW-HX711 + 4 células de carga

Teste de bancada da balança do alimentador, antes de ligá-la no firmware do app.

## 1. O que este teste responde

| Pergunta | Comando | Por que importa |
|---|---|---|
| A fiação das 4 células está certa? | `d` | é o erro mais comum, e sem diagnóstico vira hora perdida |
| Quantas contagens do ADC valem 1 grama? | `c <gramas>` | é o fator de escala que o firmware precisa |
| Quanto a leitura oscila parada? | `n` | define `WEIGHT_STABILITY_TOLERANCE_G` |
| A que taxa o módulo amostra? | `s` | define `WEIGHT_SAMPLE_INTERVAL_MS` |
| A leitura escorrega com o tempo? | `k` | decide se precisa re-tara periódica |
| A leitura muda conforme **onde** o peso está? | `b` | o gato não pisa no centro |
| Dá para distinguir um gato de 3 kg de um de 6,5 kg? | tudo junto | é o requisito real do projeto |

## 2. Ligação

> ### ⚠️ 3,3 V no `VCC`. Nunca 5 V.
> O nível lógico do `DT` acompanha o `VCC`. Com o módulo em 5 V, ele entrega
> 5 V num GPIO da ESP32 que tolera 3,3 V. Isso é exatamente o problema **E1**
> registrado em [docs/02](../../docs/02-revisao-implementacao.md) — o grupo
> anterior tinha o HX711 em 5 V.
>
> Os diagramas que circulam na internet (circuitjournal e afins) mostram um
> **Arduino a 5 V** com os pinos D4/D5. A fiação das células é a mesma, mas a
> alimentação e os pinos **não**: aqui é 3,3 V, GPIO 26 e GPIO 25.

### As 4 células → uma ponte de Wheatstone

Cada célula é uma **meia-ponte** (3 fios) e vira **um braço** da ponte. O fio
**vermelho** de cada uma é o ponto central, e é ele que vai para o módulo. Os
outros dois fios encadeiam as células vizinhas, formando um anel:

```
              branco
        TL ─────────────── TR
        │                   │
      preto               preto
        │                   │
        BL ─────────────── BR
              branco

   TL.vermelho ──► E+          BL.vermelho ──► A+
   BR.vermelho ──► E-          TR.vermelho ──► A-
```

A regra que importa: **uma diagonal recebe a excitação (`E+`/`E−`), a outra
diagonal é o sinal (`A+`/`A−`)**. Células adjacentes se ligam entre si pelos
fios preto e branco.

Se o peso sair negativo, troque `A+` com `A−` — ou nem troque: a calibração com
`c` gera um fator negativo e o resultado fica certo do mesmo jeito.

### Módulo → ESP32

```
  XFW-HX711            ESP32
     VCC  ──────────── 3V3       ← nunca 5V
     GND  ──────────── GND
     DT   ──────────── GPIO 26
     SCK  ──────────── GPIO 25
```

O `3V3` tem que vir da **ESP32**, não da fonte do servo: o pico de corrente do
motor vira ruído direto na leitura de peso
([doc 05](../../docs/05-servo-360-fim-de-curso.md) §6). Se as duas fontes
estiverem em uso, os **GND precisam estar unidos**.

### Conferindo as células com multímetro

Antes de ligar, vale identificar os fios de cada célula em ohms. Numa
meia-ponte, a resistência entre os **dois extremos** é o **dobro** da medida de
cada extremo até o fio do meio. O fio do meio costuma ser o vermelho.

## 3. Como rodar

```powershell
cd teste_componentes\hx711
pio run -t upload
pio device monitor
```

Envio de linha em **NL**. O diagnóstico roda sozinho no boot.

## 4. Roteiro de bancada

1. **`d`** — diagnóstico. Ele pede que você aperte uma célula com o dedo. Só
   siga adiante quando ele disser que responde a carga no sentido certo.
2. **`r`** — leitura bruta. Aperte **cada uma das quatro** células, uma de cada
   vez. Todas devem mover a leitura. Se só uma mover, o anel de fios preto/branco
   está aberto em alguma emenda.
3. **`t`** — tara, com a plataforma vazia e sem encostar.
4. Coloque a massa conhecida e rode **`c <gramas>`** (ex.: `c 1000` para 1 kg).
5. **`g`** — confira a leitura contra o peso real. Tire e recoloque a massa
   algumas vezes.
6. **`b <gramas>`** — teste de cantos. Ele pede a mesma massa em cinco posições:
   centro e sobre cada célula. Mede o quanto a leitura muda com a posição.
7. **`n`** — ruído. É o número que define a tolerância do firmware.
8. **`s`** — taxa de amostragem.
9. **`k`** — deriva ao longo de 5 min. Pode interromper antes com qualquer tecla.
10. **`save`** — grava offset e escala na NVS.
11. Preencha [`resultados.md`](resultados.md) com as linhas `[dado]`.

## 5. Quando algo dá errado

O comando `d` já imprime o veredito, mas a tabela resumida:

| Sintoma | Causa provável |
|---|---|
| "o DT nunca ficou baixo" | módulo sem 3,3 V, `DT`/`SCK` trocados, ou GND não comum |
| "ADC SATURADO" | anel preto/branco aberto, `E±` trocado com `A±`, ou célula com fio rompido |
| "a leitura quase não mudou" | `A+`/`A−` mal conectados ou nas células erradas |
| leitura cai ao apertar | sinal invertido — trocar `A+`/`A−`, ou deixar e calibrar |
| só uma célula move a leitura | o anel está aberto; as outras três não estão na ponte |
| ruído alto parado | módulo na fonte do servo, fios longos, plataforma balançando |

## 6. Duas coisas que limitam a precisão

**As células são muito maiores que o gato.** Quatro de 50 kg dão **200 kg** de
fundo de escala para medir um animal de 3 a 6,5 kg. O HX711 tem 24 bits, então
sobra resolução teórica — mas a resolução *útil* é limitada pelo ruído, e é por
isso que o `n` existe. O critério prático: a faixa de ruído em gramas precisa
ser bem menor que os ±500 g de tolerância que o app usa por gato.

**Solta na mesa, a leitura não é peso.** As quatro células só medem corretamente
montadas nos quatro cantos de uma plataforma rígida, cada uma apoiada pelo
parafuso central e livre para fletir. Antes da maquete, este teste valida
eletrônica, fiação e calibração — não precisão.
