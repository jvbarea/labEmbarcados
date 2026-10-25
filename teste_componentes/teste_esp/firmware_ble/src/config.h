#pragma once
/*
 * config.h
 * Pinos, tempos e parametros do firmware de teste.
 *
 * Diferencas em relacao a teste_esp/files/config.h estao anotadas: elas vem da
 * revisao (REVISAO.md) e dos resultados de bancada do servo.
 */

// ---------------------------------------------------------------------------
// Pinos
// ---------------------------------------------------------------------------
// Servo: GPIO 13, no lado ESQUERDO da placa (D13, acima do GND). Nao e pino de
// strapping (0, 2, 5, 12, 15), entao nao interfere no boot. Os primeiros testes
// de bancada usaram o GPIO 18, que o doc 07 sugeria; o 13 e o mesmo pino do
// codigo herdado.
#define PIN_SERVO 13

// Chaves de fim de curso. Nao temos as chaves na bancada, entao ficam
// desativadas: o servo e posicional e sabe onde esta. Ligue em 1 quando a
// maquete tiver as chaves montadas.
#define USE_LIMIT_SWITCHES 0
#define PIN_LIMIT_OPEN     32   // (original: 14) 32/33 tem pull-up interno
#define PIN_LIMIT_CLOSED   33
#define LIMIT_SWITCH_ACTIVE_LEVEL LOW

// HX711: pinagem do doc 07 secao 4, validada em teste_componentes/hx711.
// O codigo herdado usava 4 e 5, e o GPIO 5 e pino de strapping - nao usar.
#define PIN_HX711_DT   26
#define PIN_HX711_SCK  25

// 1 = balanca real; 0 = balanca simulada (comando 'w <gramas>' no serial).
// A AccessStateMachine fala com a interface IWeightSource e nao sabe qual das
// duas esta por tras. Nao precisa editar aqui: 'pio run -e mock' compila a
// simulada (o platformio.ini passa -D USE_HX711=0).
#ifndef USE_HX711
#define USE_HX711 1
#endif

// ---------------------------------------------------------------------------
// Servo POSICIONAL 180 graus (MG946R / MG995 / MG996R)
// ---------------------------------------------------------------------------
// O codigo original assumia servo de rotacao continua (1000 = gira p/ um lado,
// 2000 = gira p/ o outro, 1500 = parado). Com servo posicional o pulso e a
// POSICAO, entao aqui cada valor e um angulo da porta.
//
// A porta e descrita em GRAUS (o mecanismo usa 0 e 90). Para converter grau em
// pulso, o firmware usa DOIS pontos medidos no servo real: o pulso em que o eixo
// esta fisicamente em 0 graus e o pulso em que esta em 90. Eles ficam em NVS e
// sao gravados em bancada pelos comandos 'set0' e 'set90' (dentro de 'cal on').
//
// Os valores abaixo sao so o ponto de partida, a faixa nominal da familia MG99x
// (500 us = 0 graus, 1500 us = 90 graus, 2500 us = 180 graus). Nunca foram
// medidos no MG946R: ate rodar a calibracao, "0 graus" pode empurrar o eixo
// contra o batente.
#define DEFAULT_SERVO_US_AT_0DEG   500
#define DEFAULT_SERVO_US_AT_90DEG  1500

// Faixa ABSOLUTA de pulso que o firmware aceita escrever, qualquer que seja a
// calibracao. E um limite de seguranca, nao uma calibracao.
//
// O teto passou de 2500 para 3000 us porque a porta precisa chegar a trava fisica
// do servo, que fica alem de 90 graus: com a calibracao medida (~9 us por grau) o
// 90 graus ja caia em 2500. A biblioteca ESP32Servo corta o pulso em 2500 dentro
// do attach e do writeTicks, entao o PWM e escrito direto pelo ESP32PWM
// (ver DoorActuator::writeUs).
#define SERVO_US_MIN  500
#define SERVO_US_MAX  3000

// Acima disto o pulso sai da faixa nominal da familia MG99x. Nao e proibido, mas
// o servo pode chegar ao fim de curso interno; a calibracao avisa a cada passo.
#define SERVO_US_NOMINAL_MAX 2500

// PWM do servo: 50 Hz, 16 bits. Com 16 bits, 20 ms (o periodo) valem 65536
// passos, ~3,28 passos por microssegundo.
#define SERVO_PWM_FREQ_HZ  50
#define SERVO_PWM_BITS     16
#define SERVO_PWM_PERIOD_US 20000UL

// Diferenca minima entre o pulso de 0 e o de 90 graus para a calibracao valer.
// Abaixo disso a conversao degenera (todo angulo vira o mesmo pulso).
#define SERVO_CAL_MIN_SPAN_US 100

// Passo padrao dos comandos '+' e '-' do modo de calibracao.
#define SERVO_NUDGE_US 10

// Posicoes da porta, em graus. Persistidas em NVS e ajustaveis em bancada
// pelos comandos 'cal on' / 'pos' / 'setclosed' / 'setopen'.
#define DEFAULT_DOOR_CLOSED_DEG 0
#define DEFAULT_DOOR_OPEN_DEG   90

// Rampa de movimento: evita o pico de corrente do salto e o tranco mecanico.
// A cada SERVO_STEP_MS o pulso anda um passo; com a calibracao nominal, 1 grau
// vale ~11 us, entao 15 us por passo sao ~1,35 graus a cada 15 ms, ~90 graus/s.
// O servo sozinho, sem carga, faz uns 300 graus/s, entao a rampa continua bem
// abaixo do que ele aguenta e o eixo acompanha o comando sem ficar para tras.
//
// Tempo para percorrer 90 graus (1000 us), fora o assentamento:
//   passo  5 us -> 3,0 s   (o valor original; ainda usado na calibracao)
//   passo 10 us -> 1,5 s   (fechar)
//   passo 15 us -> 1,0 s   (abrir)
//
// Abrir e mais rapido que fechar de proposito: a porta que desce pode pegar um
// animal, entao fecha mais devagar e chega ao fim com menos tranco.
#define SERVO_STEP_US_OPEN   15
#define SERVO_STEP_US_CLOSE  10
// Modo de calibracao: passo suave, porque ali o operador esta chegando perto de
// um batente e e melhor errar para o lado lento.
#define SERVO_STEP_US_CAL    5
#define SERVO_STEP_MS        15

// Partida suave da abertura. A porta fechada tem atrito estatico: soltar o eixo
// do repouso pede mais torque do que mante-lo andando. Mandar o servo direto ate
// o fim faz o comando correr na frente de um eixo que ainda nao saiu do lugar;
// quando ele finalmente solta, o torque acumulado vira um pico de corrente, e se
// a fonte nao aguenta a tensao cai e o servo (ou a ESP32) reinicia.
// Medido na bancada: ir direto de 0 a 90 graus falhava, mas ir a 10 e depois a 90
// funcionava. A partida suave faz exatamente isso: anda DOOR_KICK_DEG devagar,
// segura DOOR_KICK_HOLD_MS, e so entao segue na velocidade normal.
// So vale saindo do repouso na posicao fechada; DOOR_KICK_DEG 0 desliga.
#define DOOR_KICK_DEG        10
#define DOOR_KICK_HOLD_MS    300
#define SERVO_STEP_US_KICK   5

// Segurar a porta aberta. A tampa nao se sustenta sozinha na posicao de aberta
// (testado: ao soltar o servo ela caiu), entao o servo fica energizado enquanto a
// porta esta OPEN e so solta depois de fechada. Isso contraria o RNF04 (servo
// desenergizado ao fim do movimento) de proposito, e tem custo: o motor segura a
// carga o tempo todo, esquenta, e a fonte precisa aguentar a corrente de
// sustentacao. Com 0 volta ao comportamento de soltar ao abrir.
#define DOOR_HOLD_OPEN 1

// Modo adaptacao (learn): a porta abre para qualquer gato e cada visita vira uma
// amostra de peso. Com 1 a porta fica aberta o tempo todo enquanto o modo durar,
// como no simulador do app. Com 0 ela abre a cada visita e fecha quando o gato sai.
// O modo dura DIAS: se o servo precisa segurar a tampa (DOOR_HOLD_OPEN 1), deixar
// aberta direto e manter o motor energizado por dias. Por isso o padrao segue o
// inverso de DOOR_HOLD_OPEN.
#define LEARN_DOOR_STAYS_OPEN (!DOOR_HOLD_OPEN)

// Tempo parado no alvo antes de desenergizar. Se a porta pesar, o eixo chega
// um pouco depois do comando; desenergizar cedo demais deixa a porta curta.
#define SERVO_SETTLE_MS      400
#define DOOR_MOVE_TIMEOUT_MS 5000  // guarda: acima disso e falha

// ---------------------------------------------------------------------------
// Balanca mockada
// ---------------------------------------------------------------------------
#define MOCK_RAMP_MS               600   // tempo p/ o peso "assentar" no alvo

// PROVISORIO: confirmar com o comando 's' do teste em teste_componentes/hx711.
// O HX711 amostra a 10 SPS com o pino RATE em GND, entao 100 ms e o periodo
// real; nao adianta pedir mais rapido que isso.
#define WEIGHT_SAMPLE_INTERVAL_MS  100

#define WEIGHT_STABILITY_WINDOW    6

// PROVISORIO: confirmar com o comando 'n' (ruido) do teste do HX711. Precisa
// ser MAIOR que a faixa de ruido medida, senao a balanca nunca e considerada
// estavel e a porta nunca abre.
#define WEIGHT_STABILITY_TOLERANCE_G 80.0f

// Media movel exponencial sobre as amostras cruas do HX711.
#define WEIGHT_FILTER_ALPHA 0.25f

// Quantas leituras entram na media da tara e da calibracao. A 10 SPS, 20
// leituras levam ~2 s - por isso as duas sao recusadas com a porta em movimento.
#define HX711_AVG_SAMPLES 20
#define WEIGHT_EMPTY_THRESHOLD_G   300.0f
#define WEIGHT_HEALTH_TIMEOUT_MS   1000  // sem amostra por mais que isso = sensor doente
                                         // (o original usava hx.is_ready(), que e
                                         //  "tem amostra agora", nao saude - ver REVISAO.md)

// ---------------------------------------------------------------------------
// Parametros de operacao (sobrescritos pelo app, persistidos em NVS)
// ---------------------------------------------------------------------------
// Janela do modo de bancada: so vale enquanto NENHUM gato estiver cadastrado.
// Com o cadastro do app, quem decide e a faixa e a regra de cada gato.
#define DEFAULT_WEIGHT_MIN_G  3000.0f
#define DEFAULT_WEIGHT_MAX_G  8000.0f

// Nomes do protocolo Feed Cat v2 (plano/01-app.md secao 7):
//   confMs  = tempo com peso estavel antes de decidir
//   leaveMs = tempo com a plataforma vazia antes de fechar
//   maxOpenS = limite do modo reabastecer
#define DEFAULT_CONF_MS     2000
#define DEFAULT_LEAVE_MS    4000
#define DEFAULT_MAX_OPEN_S  300

// Faixas aceitas no "cfg" (as mesmas de LIMITS em app/src/device/protocol.ts).
#define CONF_MS_MIN     500
#define CONF_MS_MAX     10000
#define LEAVE_MS_MIN    1000
#define LEAVE_MS_MAX    30000
#define MAX_OPEN_S_MIN  30
#define MAX_OPEN_S_MAX  1800
#define CAT_WEIGHT_MAX_G 20000

// Quantos gatos o dispositivo guarda (o protocolo v2 usa k = 1 ou 2).
#define MAX_CATS 2

// Acima deste peso a plataforma conta como ocupada: e o inicio de uma "visita".
#define PRESENCE_THRESHOLD_G WEIGHT_EMPTY_THRESHOLD_G

// ---------------------------------------------------------------------------
// BLE - Nordic UART Service (reconhecido pelo nRF Connect)
// ---------------------------------------------------------------------------
#define BLE_DEVICE_NAME  "Alimentador"
#define BLE_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_RX_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  // app -> ESP (write)
#define BLE_CHAR_TX_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  // ESP -> app (notify)
#define BLE_MTU_REQUEST  185  // MTU pedido; quem decide e o celular (minimo 23)

// Mensagens maiores que uma notificacao viajam em pedacos com cabecalho
// "#i/n:" (ver BleControl.h). BLE_CHUNK_MAX e o teto de cada pedaco; o tamanho
// real e o menor entre ele e o MTU negociado menos 3.
#define BLE_CHUNK_MAX    180
// Maior mensagem que o firmware aceita do app depois de remontar os pedacos.
// "profile" (nomes, emoji e cores dos dois gatos) e a maior, ~250 bytes.
#define BLE_MSG_MAX      512
// Escritas (inteiras ou pedacos) esperando o loop. O app escreve com resposta,
// uma de cada vez, entao raramente passa de 2; a folga cobre o loop parado ~2 s
// numa tara. Cada posicao ocupa BLE_MSG_MAX bytes.
#define BLE_RX_QUEUE_LEN 16
// Intervalo minimo entre duas notificacoes. Sem isso uma rajada (sync do
// historico) enche a fila da pilha BLE e as ultimas se perdem.
#define BLE_TX_GAP_MS    15
#define BLE_TX_QUEUE_MAX 48   // acima disso o mais antigo e descartado

// Status a cada 500 ms (o mesmo ritmo do simulador do app) e na hora quando
// porta, gato, modo, liberacao ou configuracao mudam.
#define STATUS_PERIOD_MS 500

// ---------------------------------------------------------------------------
// Persistencia e log
// ---------------------------------------------------------------------------
#define NVS_NAMESPACE_APP  "feeder-cfg"

// Historico: anel de eventos em LittleFS (docs/08 secao 4.2 pede 200 a 300).
// Com ~10 visitas por dia, 256 posicoes sao ~25 dias sem conectar o celular.
#define EVENT_LOG_CAPACITY 256
#define EVENT_LOG_FILE     "/events.bin"
