# 06 — Decisões em aberto e próximos passos

## 1. Decisões que o grupo precisa tomar

| # | Decisão | Opções | Recomendação | Por quê |
|---|---|---|---|---|
| D1 | ✅ **DECIDIDO: sem FPGA, só ESP32** | — | — | Arquitetura e comunicação em [07](07-comunicacao-app-esp32.md) |
| D2 | Quantas chaves de fim de curso? | 1 (fechado) · 2 (fechado + aberto) | **2**; se só 1, no **fechado** | ver [05](05-servo-360-fim-de-curso.md) §3.1 |
| D3 | BLE ou Bluetooth Clássico? | BLE · SPP | **BLE** | iOS, coexistência com Wi-Fi, notify de estado. Ver [04](04-app-bluetooth.md) §3 |
| D4 | Mensagens: JSON ou binário? | JSON curto · binário | **JSON** (com MTU 185) | depuração com nRF Connect, reaproveitamento no Wi-Fi |
| D5 | O que o app decide? | ver tabela | app decide **política** (faixas, horários, tempos, reabastecer); ESP decide **tempo real e segurança** | ver [03](03-problema-comunicacao-app-esp.md) §5.2 |
| D6 | Como "desligar" o servo? | neutro · sem pulso · cortar +V | **sem pulso**; MOSFET se medir consumo/ruído relevante | ver [05](05-servo-360-fim-de-curso.md) §5 |
| D7 | Reaproveitar o app antigo ou recomeçar? | atualizar feedCat · novo projeto Expo | **novo projeto** no SDK atual, copiando só textos/validações úteis | o app antigo é 1 tela + template; libs nativas de BLE exigem dev build de qualquer jeito |
| D8 | Framework do firmware | Arduino (PlatformIO) · ESP-IDF | **Arduino via PlatformIO** | curva menor, libs prontas (HX711, NimBLE, ESP32Servo), mas com FreeRTOS disponível |

## 2. Perguntas para o grupo anterior (Felipe e colegas)

1. **Onde está o firmware da ESP32?** (arquivo `.ino`/projeto PlatformIO). Sem ele, as causas de [03](03-problema-comunicacao-app-esp.md) continuam hipóteses.
2. O app era usado pelo **Expo Go** ou por **APK** gerado no EAS?
3. Qual biblioteca de servidor HTTP (`WebServer` ou `ESPAsyncWebServer`) e de JSON?
4. Como era feita a calibração do HX711 (fator de escala usado)? Ainda serve, já que as células continuam.
5. O "até 2 segundos para abrir" era implementado na ESP?

(Perguntas sobre a serial ESP→FPGA deixaram de importar com a decisão D1.)
7. Em que situação exata o problema aparecia (logo na configuração do Wi-Fi? só no botão da porta? depois de algum
   tempo?).

## 3. Próximos passos (sem alterar nada ainda)

- [ ] Recuperar o firmware antigo e colocar em `referencia/` neste repo (só leitura).
- [ ] Rodar o roteiro de diagnóstico de [03](03-problema-comunicacao-app-esp.md) §4 e anotar os resultados abaixo.
- [ ] Decidir D1–D8.
- [ ] Definir mecanismo da porta para o servo 360° (pinhão-cremalheira, manivela, carretel) e posição das chaves.
- [ ] Levantar pinagem da ESP32 (evitar pinos de *strapping*; HX711 em 3,3 V).
- [ ] Especificar o protocolo final (mensagens, unidades, faixas, erros) num documento próprio.

## 4. Registro de resultados do diagnóstico

| Data | Teste | Resultado | Conclusão |
|---|---|---|---|
| | curl direto na ESP | | |
| | app no Expo Go, dados móveis off | | |
| | APK instalado | | |
