# Resultados — Servo 180° da porta (MG995)

Preencher na bancada, seguindo o roteiro da §4 do [README](README.md). As linhas `[dado]` do monitor
serial podem ser coladas direto nas tabelas.

**Data:** ____/____/______  ·  **Quem rodou:** ______________________
**Servo (etiqueta):** ______________________  ·  **Fonte:** ______ V / ______ A

> O MG996R comprado veio quebrado de outro projeto; o teste está sendo feito com um **MG995**,
> o mesmo modelo do projeto anterior. Para trocar de modelo, ajuste `MODELO` e `MS_POR_60_REF`
> no topo de [`src/main.cpp`](src/main.cpp).

---

## 1. Faixa de pulso útil — comando `l`

| | Pulso (µs) | Ângulo correspondente | Observação |
|---|---|---|---|
| Limite inferior bruto (forçou) | | | |
| Limite inferior com margem | | | usar este |
| Centro | 1500 | 90° | |
| Limite superior com margem | | | usar este |
| Limite superior bruto (forçou) | | | |

**Janela gravada com `w`:** ________ a ________ µs → excursão total de ________ graus

## 2. Tempo de excursão — comando `t`

| Repetição | min→max (ms) | max→min (ms) | ms / 60° |
|---|---|---|---|
| 1 | | | |
| 2 | | | |
| 3 | | | |
| **Média** | | | |

Datasheet: MG995 ~200 ms/60° · MG996R ~170 ms/60°, ambos a 4,8 V e sem carga.
Muito acima disso → fonte fraca ou mecanismo duro.

**`T_max` sugerido para a FSM da porta** (2 × o maior tempo medido): ________ ms

## 3. Repetibilidade — comando `r`

| Item | Resultado |
|---|---|
| 10 ciclos concluíram sem travar? | ( ) sim ( ) não — |
| Tempo total dos 10 ciclos | ________ ms |
| Horn voltou exatamente à marca? | ( ) sim ( ) desviou ______ mm |
| Temperatura do corpo do servo no fim | ( ) frio ( ) morno ( ) quente demais |
| Houve reset da ESP32 durante o teste? | ( ) não ( ) sim, ______ vezes |

## 4. Consumo (fonte ou multímetro em série)

| Estado | Corrente | Observação |
|---|---|---|
| `off` (sem pulso) | ________ mA | |
| Parado segurando 90° (`j`) | ________ mA | |
| Em movimento (sweep `s`) | ________ mA | pico: ________ A |
| Rotor travado (segurar o horn 1 s — **não insistir**) | ________ A | |

## 5. Comportamento sem pulso — decisão D6

| Pergunta | Resposta |
|---|---|
| Com `off`, o eixo gira livre com a mão? | ( ) livre ( ) continua travado ( ) travado, mas cede |
| Com `off`, o consumo cai a praticamente zero? | ( ) sim ( ) não, ficou em ______ mA |
| Há tremor/zumbido parado em 90° (`j`)? | ( ) não ( ) leve ( ) forte |

**Conclusão sobre D6:** parar os pulsos é suficiente para "desligar" o servo, ou vale o MOSFET
cortando o +V, como sugere o [doc 05](../../docs/05-servo-360-fim-de-curso.md) §5?

________________________________________________________________________

## 6. Problemas observados

| # | O que aconteceu | Em que comando | Hipótese |
|---|---|---|---|
| 1 | | | |
| 2 | | | |

---

## 7. Impacto no projeto

Três perguntas para responder com os números acima, antes de escrever o firmware da porta:

1. **As chaves de fim de curso continuam necessárias (D2)?**
   O servo é posicional e sabe onde está, diferente do 360° que os docs assumem. Se a §3 mostrar boa
   repetibilidade, as chaves deixam de ser o sensor de posição e passam a ser só proteção contra
   travamento — possivelmente uma só, no fechado.
   Resposta: ______________________________________________________________

2. **Parar os pulsos basta para desligar o servo (D6)?**
   Ver §5. Se ficar travado ou consumindo parado, entra o MOSFET no +V.
   Resposta: ______________________________________________________________

3. **A FSM da porta precisa ser reescrita?**
   A máquina de estados do [doc 05](../../docs/05-servo-360-fim-de-curso.md) §4 gira o motor
   "enquanto a chave não atuar". Com servo posicional, `ABRIR` vira "escreve o ângulo aberto e
   espera `T_max`", o que é bem mais simples. Decidir se o doc 05 é revisado ou substituído.
   Resposta: ______________________________________________________________

Depois de responder, atualizar a tabela de decisões do [doc 06](../../docs/06-decisoes-em-aberto.md) §1.
