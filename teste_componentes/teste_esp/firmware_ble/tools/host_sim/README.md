# Simulador do firmware no PC

Compila a lógica do firmware — os **mesmos** arquivos de [`../../src/`](../../src/) —
para o PC, contra substitutos mínimos do Arduino em [`stubs/`](stubs/), e roda
cenários em que o programa faz o papel do celular.

Serve para testar regras e protocolo **sem a placa**: em segundos, com relógio
falso (60 s de comedouro rodam em menos de 1 s) e com AddressSanitizer, que pega
estouro de buffer antes de ele virar um reset misterioso na ESP32.

## Rodar

De dentro de `firmware_ble`, no Windows com WSL:

```powershell
pio run                          # uma vez, para baixar a ArduinoJson em .pio/
wsl bash tools/host_sim/run.sh   # compila e roda
wsl bash tools/host_sim/run.sh -v   # tambem mostra o que o firmware imprime no serial
```

Em Linux, o mesmo `bash tools/host_sim/run.sh`. Precisa de `g++` 9 ou mais novo.
O executável vai para `/tmp/feeder_sim` (mude com `OUT=...`). Sai com código 0
quando tudo passa.

## O que é simulado

| Peça real | Substituto | O que o teste controla |
|---|---|---|
| `millis()`, `esp_timer` | relógio falso | avança 1 ms por volta do loop |
| NVS (`Preferences`) | mapa em memória | sobrevive ao "reboot"; conta as gravações |
| LittleFS | arquivos em memória | sobrevive ao "reboot"; pode falhar ao montar |
| Pilha BLE | rádio de mentira | conectar com um MTU, escrever na RX, ler as notificações (cortadas em MTU − 3, como a pilha real) |
| Servo (`ESP32PWM`) | registra o pulso | saber se o servo está energizado |
| Balança | a `MockWeightSource` de verdade | o teste põe e tira peso |

O "reboot" destrói e recria os objetos do firmware, mantendo NVS e arquivos.

## O que não é simulado

O rádio de verdade (alcance, negociação de MTU pelo celular, Web Bluetooth), o
HX711 (tara e calibração), o servo físico e o tempo real da flash. Isso fica para
o teste de bancada com a placa: [`../v2_handshake.py`](../v2_handshake.py).

## Cenários

Espelham os testes do simulador do app (`app/__tests__/mockLink.test.ts`) e
acrescentam o que só existe no firmware:

- hello, status completo, `boot` com a hora corrigida no `hello`;
- `cats`/`cfg`/`mode` iguais não gravam na flash nem mudam o `cfgV`; validação de faixas;
- gato liberado (abre, visita com duração e peso, fecha, servo solto);
- gato em dieta, peso desconhecido, encostar e sair antes da decisão;
- liberação por 1 visita (consumida) e por prazo (expira), `revoke`, `unknown_cat`, `no_clock`;
- modo adaptação;
- reabastecer com visitas no meio, prazo esgotado com gato em cima (RNF04), "fechar agora" pendente;
- porta em falha: um evento só, `refill` recusado, visita anotada, `close` rearma;
- sensor mudo e recuperação;
- `sync` com o anel cheio (256 de 300 eventos, em ordem, `syncEnd` certo);
- reboot: gatos, tempos, modo, perfil com emoji e histórico voltam; `seq` não repete;
- MTU 23: perfil em pedaços de 20 bytes nos dois sentidos, nenhuma notificação acima do MTU;
- JSON quebrado, tipo e operação desconhecidos, pedaços fora de ordem, rajada de escritas;
- status a cada 500 ms e imediato quando a porta muda;
- `evtrim`, `evclear`, janela de bancada sem gatos, LittleFS que não monta.

Para um cenário novo, siga o padrão de `sim_main.cpp`: `readyDevice()`, mexa no
peso com `weight()`, avance o tempo com `run(ms)`, mande mensagens com
`request(J("{...}"))` e confira com `CHECK`.
