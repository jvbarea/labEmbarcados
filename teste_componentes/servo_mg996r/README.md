# Teste — Servo TowerPro 180° (MG995 / MG996R) + ESP32-WROOM-32

Teste de bancada do servo da porta do alimentador, **antes** de escrever a FSM da porta.

**Modelo em teste: MG995.** O MG996R que tínhamos veio quebrado de outro projeto. Os dois são
equivalentes para este teste — mesmos 3 fios, 50 Hz, faixa de ~500–2500 µs, 180° posicionais — e o
firmware roda sem alteração. Para trocar de modelo, ajuste `MODELO` e `MS_POR_60_REF` no topo de
[`src/main.cpp`](src/main.cpp); é só o texto e a velocidade de referência do comando `t`.

| | MG995 | MG996R |
|---|---|---|
| Velocidade (4,8 V, sem carga) | ~200 ms/60° | ~170 ms/60° |
| Torque (4,8 V) | ~8,5 kgf·cm | ~9,4 kgf·cm |
| Corrente no arranque / travado | acima de 1 A nos dois — fonte externa obrigatória | idem |

O MG995 é, aliás, o servo que o **projeto anterior já usava** (ver
[doc 05](../../docs/05-servo-360-fim-de-curso.md) §1).

## 1. O que este teste responde

| Pergunta | Comando | Por que importa |
|---|---|---|
| Qual a faixa de pulso que o servo realmente aceita, sem forçar o batente? | `l` | define `minUs`/`maxUs` do firmware final |
| Quanto tempo leva de um extremo ao outro? | `t` | vira o `T_max` (timeout) da FSM da porta |
| Ele volta sempre para a mesma posição depois de muitos ciclos? | `r` | se sim, chave de fim de curso vira só segurança |
| Sem pulso (`detach`) o eixo fica livre ou continua travado? | `on`/`off`, `j` | é a decisão **D6** do [doc 06](../../docs/06-decisoes-em-aberto.md) |
| Quanto ele consome parado, movendo e travado? | fonte/multímetro | dimensiona a fonte e o filtro do HX711 |
| A ESP32 reseta (brownout) quando o servo arranca? | qualquer rotina | indica GND/fonte mal dimensionados |

> **Nota sobre os docs.** [05](../../docs/05-servo-360-fim-de-curso.md) e
> [07](../../docs/07-comunicacao-app-esp32.md) foram escritos assumindo um servo **360° de rotação
> contínua** (pulso = velocidade). O servo testado aqui é **180° posicional**
> (pulso = ângulo, com potenciômetro interno). Os resultados deste teste são a base para revisar
> aqueles documentos — em especial a FSM da §4 do doc 05 e a decisão D2 (quantas chaves de fim de
> curso). Nada foi alterado neles ainda.

## 2. Ligação elétrica

> ### ⚠️ Nunca alimente o servo pelo pino 5V/VIN da ESP32 nem pela USB
> MG995 e MG996R puxam **mais de 1 A** no arranque e com o rotor travado. Pela USB derrubam a tensão,
> a ESP32 reseta por *brownout* e, no pior caso, o regulador da placa queima.
> Use **fonte externa 5–6 V de ao menos 2 A**.

```
   Fonte 5-6 V (>=2 A)
      +  ──────────────┬───────────────► fio VERMELHO do servo
                       │
                    [C 1000uF]   eletrolitico, o mais perto possivel do servo
                       │                 (respeitar a polaridade)
      -  ──────────────┴───────┬───────► fio MARROM/PRETO do servo
                               │
                               │  GND COMUM  (obrigatorio)
                               │
   ESP32  GND ─────────────────┘

   ESP32  GPIO 18 ─────────────────────► fio LARANJA/AMARELO do servo (sinal)
```

- **GPIO 18** para o sinal, conforme a pinagem sugerida no [doc 07](../../docs/07-comunicacao-app-esp32.md) §4.
- **GND comum** entre a fonte e a ESP32 não é opcional: sem ele o servo não enxerga o pulso.
- Capacitor de 1000 µF (470 µF serve) junto ao servo, para absorver o pico de arranque.
- **Nível lógico:** o servo costuma reconhecer os 3,3 V da ESP32. Se o movimento for errático ou
  trêmulo mesmo com a fonte boa, colocar um conversor de nível no sinal (o projeto anterior já
  usava um).
- Mantenha o servo **sem carga mecânica** (horn livre) neste primeiro teste.

## 3. Como rodar

```powershell
cd teste_componentes\servo_mg996r
pio run                        # compila
pio run -t upload              # grava na ESP32
pio device monitor -b 115200   # abre o monitor serial
```

No monitor, configure o envio de linha como **NL** (nova linha) e digite `?` para ver o menu.

## 4. Roteiro de bancada (nesta ordem)

1. **Só a ESP32 ligada** na USB, fonte do servo **desligada**. Abra o monitor.
   No boot deve aparecer `boot ok - servo DESLIGADO`. Nada pode se mover.
2. Confira a fiação da §2 com a fonte ainda desligada. **Ligue a fonte.**
3. `on` — o servo energiza e salta para o último setpoint (1500 µs no primeiro uso). Segure a base.
4. `c` — centro. Marque a posição do horn com fita ou caneta; é a referência do teste `r`.
5. `s` — sweep completo. Ouça: estalos, trechos duros ou zumbido alto indicam mecanismo ou fonte ruim.
6. `l` — **teste de limites**. Enter avança 10 µs; digite `stop` no instante em que o servo começar a
   forçar (som mais grave, eixo parou de andar). Ele faz os dois sentidos e imprime a janela útil.
7. `w <min> <max>` — grave a janela que o passo 6 imprimiu.
8. `t` — tempo de excursão. Enter dispara, Enter de novo quando o eixo parar. Repita 3 vezes e use a
   média; o cronômetro é manual, some ~0,2 s de tempo de reação.
9. `r` — 10 ciclos 60°↔120°. No fim, compare o horn com a marca do passo 4 e toque no corpo do servo
   para ver se esquentou.
10. `j` — 10 s parado em 90°. Olhe o consumo na fonte e escute o zumbido.
11. `off` — corta os pulsos. Tente girar o horn com a mão: **fica livre ou continua travado?**
    Anote o consumo nesse estado. É essa a resposta da decisão D6.
12. Preencha [`resultados.md`](resultados.md) com as linhas `[dado]` copiadas do monitor.

Em qualquer momento, **`x` corta os pulsos imediatamente** — inclusive durante `s`, `t`, `r` e `j`.

## 5. Proteções embutidas no firmware

Vale saber que existem, para não estranhar o comportamento:

| Proteção | Efeito |
|---|---|
| Começa `detach` | nenhum pulso sai antes do `on` |
| Janela padrão 1000–2000 µs | os extremos de fábrica (500/2500 µs) só são alcançados pela rotina `l` |
| Rampa de 5 µs a cada 15 ms | evita o pico de corrente do salto e o reset por brownout |
| `x` a qualquer momento | parada de emergência |
| Auto-desliga em 30 s parado | o servo não fica zumbindo contra um batente enquanto ninguém olha |

O salto sem rampa só acontece em `t` e `r`, onde a velocidade do próprio servo é o que se mede.

## 6. Critérios de aprovação

- Atinge os dois extremos da janela sem estalos nem travamento.
- Os 10 ciclos do `r` terminam com o horn na marca inicial e o corpo do servo apenas morno.
- Nenhum reset da ESP32 (mensagem de boot repetida no meio do log).
- Tempo por 60° na mesma ordem de grandeza do datasheet (~170 ms a 4,8 V).
