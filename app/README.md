# App Feed Cat — guia de teste no PC

App em **React Native + Expo (SDK 57)** do comedouro inteligente. Roda hoje contra um **comedouro simulado**, então
dá para testar tudo sem a maquete e sem a ESP32.

Dois caminhos para cadastrar os gatos:

- **Modo adaptação** (recomendado): por alguns dias a porta abre para qualquer gato, eles se acostumam com o
  comedouro e a balança registra o peso de cada visita. Depois o app separa as medições em dois grupos e você só dá
  nome a cada um. (No simulador a porta fica aberta o tempo todo; no comedouro real, que segura a tampa com o servo,
  ela abre a cada visita e fecha quando o gato sai.)
- **Modo manual**: você informa o peso de cada gato.

A faixa de reconhecimento é fixa: **peso ± 500 g**.

---

## 1. Rodar no navegador (jeito principal)

No PowerShell, dentro desta pasta:

```powershell
npm install        # só na primeira vez (ou quando alguém mudar as dependências)
npm run web
```

O Expo abre o navegador em **http://localhost:8081**. Se não abrir sozinho, cole o endereço no navegador.

Para parar: `Ctrl+C` no terminal.

### Enquanto estiver rodando

| O que fazer | Como |
|---|---|
| Ver a mudança do código | salve o arquivo — a tela recarrega sozinha (Fast Refresh) |
| Recarregar tudo | tecla `r` no terminal |
| Limpar o cache do Metro | `Ctrl+C` e depois `npx expo start -c --web` |
| Ver erros | console do navegador (F12) e o próprio terminal |

## 2. Outros comandos

```powershell
npm test         # protocolo, regras, simulador, fragmentação BLE e mensagens do firmware
npm run lint     # ESLint
npm run typecheck # TypeScript sem gerar arquivos
```

## 2b. Conectar na ESP32 de verdade (Bluetooth, no próprio PC)

O app fala com o comedouro real **sem development build**, usando Web Bluetooth
pelo navegador. Só em **Chrome ou Edge** — Firefox e Safari não têm a API.

1. Grave o firmware em
   [`../teste_componentes/teste_esp/firmware_ble/`](../teste_componentes/teste_esp/firmware_ble/)
   e deixe a ESP32 ligada.
2. `npm run web`
3. Abra **`http://localhost:8081/?ble=1`** no Chrome.
4. Toque em **Conectar** (tela inicial, boas-vindas, modo adaptação ou aba
   Comedouro). O navegador abre um seletor de dispositivos: escolha `Alimentador`.
5. Ao conectar, o app manda a hora, os tempos, o cadastro dos gatos (faixas e a
   cópia de nomes/emoji) e puxa o histórico que o comedouro guardou. Se o app
   ainda não tem gatos e o comedouro tem, o cadastro volta sozinho.
6. Na aba **Comedouro**, os botões de reabastecer e fechar movem o servo, e o
   cartão **Balança** tem a tara e a calibração com uma massa conhecida.

Para voltar ao simulador: `http://localhost:8081/?ble=0`. A escolha fica salva
no `localStorage`, então vale para as recargas seguintes sem o parâmetro.

Detalhes que valem saber:

- **O app tenta conectar sozinho ao abrir e falha** — é esperado. O navegador
  só abre o seletor de dispositivos a partir de um clique, então a primeira
  conexão precisa vir do botão **Conectar**.
- **Queda de conexão:** o app tenta reconectar sozinho no mesmo comedouro
  (espera 1, 2, 4, 8 e depois 15 s, até 10 tentativas), sem abrir o seletor. Se
  desistir, o **Conectar** volta direto no mesmo aparelho; se nem assim achar,
  o próximo toque abre o seletor de novo.
- **Mensagens grandes vão em pedaços.** Um pacote BLE leva ~180 bytes, e o
  `cfgDump` com os nomes dos gatos passa disso. Os dois lados usam o mesmo
  formato: mensagem que cabe vai inteira (começa com `{`); a maior vai em
  pedaços com o cabeçalho `#i/n:` (ver `src/device/framing.ts` e o
  `BleControl` do firmware). As escritas também passam por uma fila, porque o
  Bluetooth do navegador faz uma operação por vez.
- O comedouro **não aparece nas configurações de Bluetooth** do Windows, e não
  existe "parear". Só o seletor do navegador enxerga o dispositivo.
- O firmware pode rodar com a **balança real** (HX711) ou com a **simulada**
  dentro da ESP32, em que o peso muda por comando. Veja o README do firmware.
- **Histórico do simulador continua no app.** O que foi gerado no simulador
  fica salvo neste navegador e aparece junto com as visitas reais quando você
  troca para `?ble=1` (o app percebe que a numeração do comedouro é outra e
  sincroniza do zero, mas não apaga nada). Para uma demonstração limpa, use
  **Comedouro → Apagar dados do app** antes — cuidado: isso também apaga o
  cadastro dos gatos **no comedouro**, e você cadastra os dois de novo.
- No modo adaptação o comedouro real **abre a cada visita** (o servo precisa
  segurar a tampa, então não fica aberto por dias); no simulador a porta fica
  aberta direto. Os textos do app valem para os dois.

Esse caminho é para desenvolver e demonstrar. O APK da Etapa 4 continua
precisando de um `BleLink` nativo (`react-native-ble-plx`), que implementa a
mesma interface `DeviceLink` — as telas não mudam.

## 3. Testar no celular (opcional)

1. Instale o **Expo Go** (Play Store) no Android.
2. `npm start` no PC.
3. Leia o QR code com o Expo Go — o celular precisa estar na **mesma rede Wi-Fi** do PC.

Vale lembrar: quando o **Bluetooth real** entrar (Etapa 2), o Expo Go deixa de servir e será preciso um
*development build*. Enquanto for só simulador, funciona.

## 4. Roteiro de teste (10 minutos)

Tudo pelo **painel do simulador**, no fim da aba **Comedouro**.

| # | Passo | O que deve acontecer |
|---|---|---|
| 1 | Abrir o app pela primeira vez | aparece a tela "Bem-vindo ao Feed Cat" pedindo o cadastro dos dois gatos |
| 2 | Cadastrar **Mimi**: nome, emoji, cor, peso `3,2`, acesso **Liberado** | a faixa mostrada fica 2,70 kg — 3,70 kg (margem fixa de ± 500 g) |
| 3 | Cadastrar **Bolota**: peso `6,5`, acesso **Em dieta** | salva normalmente |
| 4 | Tentar salvar Bolota com peso `3,4` | aparece o aviso de que a faixa encosta na da Mimi e o botão Salvar fica desabilitado |
| 4b | **Modo adaptação**: Comedouro → "Abrir modo adaptação" → "Iniciar adaptação" | no simulador a porta abre e fica aberta; o Início mostra o aviso azul do modo adaptação |
| 4c | No painel do simulador: "Gerar medições do modo adaptação" | a tela mostra o histograma, separa dois grupos (~3,2 kg e ~6,5 kg) e deixa nomear cada um |
| 4d | Nomear os dois grupos e salvar | os gatos são cadastrados com a faixa ± 500 g e o comedouro volta ao modo automático |
| 5 | Comedouro → simulador → **Mimi** | Início mostra o peso subindo, o nome da Mimi e a porta indo para *Abrindo…* → *Aberta* |
| 6 | Simulador → **Tirar da balança** | a porta fecha e o Histórico registra a visita com "Comeu" e a duração |
| 7 | Simulador → **Bolota** | a porta continua *Fechada*; ao tirar da balança, o histórico registra "Negado" |
| 8 | Início → **Liberar Bolota › 1 visita** e depois simulador → **Bolota** | a porta abre; na visita seguinte volta a negar |
| 9 | Início → **Abrir para reabastecer** | a porta abre com contagem regressiva e fecha sozinha no fim (ou no botão "Fechar porta agora") |
| 10 | Simulador → **Travar porta** e colocar a Mimi | estado *Falha* na aba Comedouro e evento de falha no histórico; **Destravar** + **Rearmar porta** volta ao normal |
| 11 | Simulador → **Cair conexão** | as telas mostram "Desconectado" e o aviso com botão "Conectar" |
| 12 | Conectar de novo | os eventos que aconteceram offline entram no histórico |
| 13 | Simulador → **Gerar histórico de exemplo** | a aba Histórico mostra hoje e os dias anteriores; o gráfico por hora já vem preenchido (o simulador semeia dados na primeira conexão) |
| 14 | Recarregar a página (F5) | gatos e histórico continuam salvos |
| 14b | Comedouro → **Apagar dados do app** → confirmar | volta para a tela de boas-vindas, sem gatos e sem histórico (é assim que se recomeça o teste do zero) |
| 15 | Trocar o Windows entre tema claro e escuro | o app acompanha |

## 5. Problemas comuns no Windows

| Sintoma | Solução |
|---|---|
| `Port 8081 is being used` | `npm run web -- --port 8082` |
| Tela branca ou erro estranho depois de mudar configuração | `npx expo start -c --web` (limpa o cache) |
| Erros de arquivo travado / permissão | o **OneDrive** está sincronizando esta pasta: pause a sincronização enquanto desenvolve |
| `npm install` muito lento | também é o OneDrive; a pasta `node_modules` tem muitos arquivos pequenos |
| Metro reclamando da versão do Node | o projeto foi testado com Node 24; se der problema, use o Node 22 LTS |
| Quero recomeçar do zero (apagar gatos e histórico) | aba **Comedouro** → "Apagar dados do app". Pelo navegador também dá: F12 → console → `localStorage.clear(); location.reload()` |

## 6. Como o código está organizado

```
app/                 telas (expo-router: arquivo = rota)
  (tabs)/            Início · Gatos · Histórico · Comedouro
  gato/[id].tsx      cadastro/edição de um gato
  primeiro-uso.tsx   onboarding
src/
  device/            protocolo v2, interface do transporte, simulador (MockLink)
  domain/            regras de peso/acesso e formatação (testadas)
  store/             estado global (zustand) e persistência
  ui/                tema, componentes e hooks
__tests__/           testes do protocolo, das regras e do simulador
```

Regra que vale manter: **tela nunca fala com o transporte direto** — sempre via store. É isso que vai permitir
trocar o simulador pelo Bluetooth real sem reescrever as telas.

## 7. O que ainda não existe

`BleLink` nativo (para o APK), Wi-Fi/MQTT, horários por gato e foto do gato.

O Bluetooth pelo navegador **já funciona** — ver §2b —, incluindo liberações
temporárias, modo adaptação e calibração da balança. O plano completo está em
[`../plano/01-app.md`](../plano/01-app.md).
