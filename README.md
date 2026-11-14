# labEmbarcados
Implementação de um Alimentador inteligente de animais

Evolução do projeto **Feed Cat** (Laboratório Digital II, repositório `lab-digi-ii`) para a disciplina de
Laboratório de Sistemas Embarcados. **Sem FPGA**: a ESP32 concentra toda a lógica. Mantém HX711 e células de
carga; troca o servo por um de 360° com chave de fim de curso e adiciona controle pelo app via Bluetooth antes do
Wi-Fi.

## App

O app está em [`app/`](app/) (Expo + React Native). Para rodar no PC:

```powershell
cd app
npm install
npm run web
```

Guia de teste completo: [`app/README.md`](app/README.md).

## Testes de componente

Testes de bancada em [`teste_componentes/`](teste_componentes/) — um projeto PlatformIO por
componente, para medir o hardware real antes de escrever o firmware. O primeiro é o
[servo 180° da porta](teste_componentes/servo_mg996r/) (MG995 / MG996R).

## Plano

- [Novo app Feed Cat](plano/01-app.md) — funcionalidades, telas, stack, protocolo v2 e etapas

## Documentação

| Documento | Conteúdo |
|---|---|
| [01 — Implementação anterior](docs/01-implementacao-anterior.md) | Arquitetura, componentes, app e protocolo serial como eram |
| [02 — Revisão da implementação](docs/02-revisao-implementacao.md) | Bugs e riscos encontrados na FPGA, ESP e app |
| [03 — App → ESP não recebe dados](docs/03-problema-comunicacao-app-esp.md) | Causas prováveis, roteiro de diagnóstico, solução e divisão de decisões app × dispositivo |
| [04 — App com Bluetooth](docs/04-app-bluetooth.md) | Estado atual (só Wi-Fi), BLE × SPP, serviço GATT, estrutura do app, roteiro |
| [05 — Servo 360° + fim de curso](docs/05-servo-360-fim-de-curso.md) | Por que a lógica antiga não serve, nova FSM, ligação elétrica, calibração |
| [06 — Decisões em aberto](docs/06-decisoes-em-aberto.md) | Decisões do grupo, perguntas ao grupo anterior, próximos passos |
| [07 — Comunicação app ↔ ESP32](docs/07-comunicacao-app-esp32.md) | Arquitetura sem FPGA: precisa de intermediário?, BLE/WebSocket/MQTT, pinagem, tarefas do firmware |
| [08 — Persistência de dados](docs/08-persistencia-dados.md) | O que fica no celular e o que fica na flash da ESP32, desgaste de NVS e quem manda no conflito |
