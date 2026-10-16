# Resultados — XFW-HX711 + 4 células de carga

Preencher na bancada seguindo a §4 do [README](README.md). As linhas `[dado]` do
monitor serial podem ser coladas direto.

**Data:** ____/____/______  ·  **Quem rodou:** ______________________
**Células:** 4 × meia-ponte, ______ kg cada  ·  **Montagem:** ( ) soltas na mesa ( ) na plataforma
**Massa de referência usada:** ________ g  ·  **Temperatura da sala:** ______ °C

---

## 1. Diagnóstico — comando `d`

| Verificação | Resultado |
|---|---|
| Módulo responde (DT fica baixo) | ( ) sim ( ) não — |
| Leitura de repouso | ________ contagens |
| Faixa parada no diagnóstico | ________ contagens |
| Saturação? | ( ) não ( ) sim |
| Delta ao apertar uma célula | ________ contagens |
| Sentido do sinal | ( ) positivo ( ) invertido (trocar A+/A−) |

**As quatro células respondem?** (comando `r`, apertando uma de cada vez)

Com as células soltas, aperte cada uma entre os dedos ou contra a mesa. A
magnitude não importa nesta etapa — **a direção sim**.

| Célula | Move a leitura? | Direção | Delta aproximado |
|---|---|---|---|
| 1 (E+) | ( ) sim ( ) não | ( ) sobe ( ) desce | |
| 2 (A−) | ( ) sim ( ) não | ( ) sobe ( ) desce | |
| 3 (A+) | ( ) sim ( ) não | ( ) sobe ( ) desce | |
| 4 (E−) | ( ) sim ( ) não | ( ) sobe ( ) desce | |

Dois critérios, e o segundo é o que costuma passar batido:

- **Alguma não move?** O anel de fios preto/branco está aberto naquele trecho —
  essa célula não está dentro da ponte.
- **Alguma move para o lado contrário das outras?** O **preto e o branco dela
  estão trocados**. A contribuição entra subtraindo em vez de somando, e a
  balança vai ler menos que o peso real. Inverta os dois fios dessa célula.

As quatro movendo para o mesmo lado é o que faz a ponte somar os quatro apoios.

## 2. Calibração — comandos `t` e `c`

| Item | Valor |
|---|---|
| Offset (zero, plataforma vazia) | ________ contagens |
| Escala | ________ contagens/grama |
| Sinal da escala | ( ) positiva ( ) negativa |

## 3. Exatidão — comando `g`

Medir em três pontos da faixa. Se só houver uma massa de referência, use uma,
duas e três unidades dela.

| Peso real (g) | Leitura (g) | Erro (g) | Erro (%) |
|---|---|---|---|
| | | | |
| | | | |
| | | | |

**Repetibilidade:** tirar e recolocar a mesma massa 5 vezes.

| # | Leitura (g) |
|---|---|
| 1 | |
| 2 | |
| 3 | |
| 4 | |
| 5 | |
| Diferença máx−mín | |

**Volta a zero?** Depois de tirar tudo, a leitura voltou para ______ g.

## 3b. Balanceamento (teste de cantos) — comando `b`

A mesma massa em cinco posições. Mede se a leitura muda conforme **onde** o peso
está no prato — e o gato não vai pisar no centro.

**Massa usada:** ________ g

| Posição | Leitura (g) | Diferença para o centro (g) |
|---|---|---|
| Centro | | — |
| Célula 1 | | |
| Célula 2 | | |
| Célula 3 | | |
| Célula 4 | | |

| Item | Valor |
|---|---|
| Espalhamento (maior − menor) | ________ g |
| Espalhamento em % do centro | ________ % |
| Veredito | ( ) < 2% bom ( ) 2–5% aceitável ( ) > 5% desnivelado |

**Se passou de 5%, o que foi ajustado mecanicamente:**

________________________________________________________________________

> Não existe correção de canto em software. Com um único HX711, as quatro
> células são somadas em hardware antes do ADC — não dá para saber quanto cada
> uma contribuiu. O ajuste é mecânico: prato rígido e nivelado, as quatro
> células encostando e carregando, parafusos com aperto parecido, e calço no
> canto que lê menos.

**Depois do ajuste**, repetir e anotar o novo espalhamento: ________ g (______ %)

## 4. Ruído — comando `n`

| Item | Contagens | Gramas |
|---|---|---|
| Desvio padrão | | |
| Faixa (máx − mín) | | |

## 5. Taxa de amostragem — comando `s`

| Item | Valor |
|---|---|
| Taxa medida | ________ Hz |
| Interpretação | ( ) ~10 SPS (padrão) ( ) ~80 SPS (jumpeado) |

## 6. Deriva — comando `k`

| Tempo | Leitura | Deriva acumulada |
|---|---|---|
| 0:00 | | — |
| 1:00 | | |
| 2:00 | | |
| 3:00 | | |
| 4:00 | | |
| 5:00 | | |

## 7. Problemas observados

| # | O que aconteceu | Em que comando | Hipótese |
|---|---|---|---|
| 1 | | | |
| 2 | | | |

---

## 8. Impacto no firmware

Três decisões que saem dos números acima, para aplicar em
[`teste_esp/firmware_ble/src/config.h`](../teste_esp/firmware_ble/src/config.h):

1. **`WEIGHT_STABILITY_TOLERANCE_G`** (hoje 80 g) — precisa ser maior que a faixa
   de ruído da §4, senão a balança nunca é considerada estável e a porta nunca
   abre. Mas quanto maior, mais lenta a decisão.
   Valor sugerido: ______ g

2. **`WEIGHT_SAMPLE_INTERVAL_MS`** (hoje 50 ms) — não adianta ser menor que o
   período da taxa medida na §5. A 10 SPS, o intervalo real é 100 ms.
   Valor sugerido: ______ ms

3. **Precisa de re-tara periódica?** Se a deriva da §6 passar de algumas dezenas
   de gramas em 5 min, o firmware vai precisar re-tarar sozinho quando a
   plataforma ficar vazia por um tempo.
   Resposta: ______________________________________________

E a pergunta que decide se o projeto fecha:

4. **Dá para distinguir os dois gatos?** O app cadastra cada gato com ±500 g de
   tolerância, então um de 3 kg cobre 2500–3500 g e um de 6,5 kg cobre
   6000–7000 g.

   Somar os três erros, não só o ruído:

   | Fonte | Valor medido |
   |---|---|
   | Ruído (§4) | ________ g |
   | Erro contra a massa conhecida (§3) | ________ g |
   | Espalhamento entre cantos (§3b) | ________ g |
   | **Total** | ________ g |

   O erro de canto costuma ser o maior dos três, e é o mais fácil de esquecer.
   Se o total passar de ~300 g, a janela de ±500 g fica apertada demais para
   confiar na identificação.

   Resposta: ______________________________________________
