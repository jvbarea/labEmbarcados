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
// Servo: GPIO 18, validado na bancada em teste_componentes/servo_mg996r.
// (o codigo original usava 13; o doc 07 sugere 18)
#define PIN_SERVO 18

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
// Trocar aqui e recompilar e tudo o que e preciso: a AccessStateMachine fala
// com a interface IWeightSource e nao sabe qual das duas esta por tras.
#define USE_HX711 1

// ---------------------------------------------------------------------------
// Servo POSICIONAL 180 graus (MG946R / MG995 / MG996R)
// ---------------------------------------------------------------------------
// O codigo original assumia servo de rotacao continua (1000 = gira p/ um lado,
// 2000 = gira p/ o outro, 1500 = parado). Com servo posicional o pulso e a
// POSICAO, entao aqui cada valor e um angulo da porta.
//
// A porta e descrita em GRAUS (o mecanismo usa 0 e 90). A conversao para
// microssegundos usa os dois extremos abaixo, que sao a faixa nominal da
// familia MG99x. CONFIRMAR com o comando 'l' do teste servo_mg996r antes de
// montar na maquete: se o servo real parar antes, ajuste aqui, senao ele forca
// o batente nos extremos.
#define SERVO_US_AT_0DEG    500
#define SERVO_US_AT_180DEG  2500
#define SERVO_US_MIN        SERVO_US_AT_0DEG
#define SERVO_US_MAX        SERVO_US_AT_180DEG

// Posicoes da porta, em graus. Persistidas em NVS e ajustaveis em bancada
// pelos comandos 'cal on' / 'pos' / 'setclosed' / 'setopen'.
#define DEFAULT_DOOR_CLOSED_DEG 0
#define DEFAULT_DOOR_OPEN_DEG   90

// Rampa de movimento: evita o pico de corrente do salto e o tranco mecanico.
#define SERVO_STEP_US        5
#define SERVO_STEP_MS        15
#define SERVO_SETTLE_MS      250   // tempo parado no alvo antes de desenergizar
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
#define DEFAULT_WEIGHT_MIN_G  3000.0f   // janela do modo de bancada
#define DEFAULT_WEIGHT_MAX_G  8000.0f

// Nomes do protocolo Feed Cat v2 (plano/01-app.md secao 7):
//   confMs  = tempo com peso estavel antes de decidir
//   leaveMs = tempo com a plataforma vazia antes de fechar
//   maxOpenS = limite do modo reabastecer
#define DEFAULT_CONF_MS     2000
#define DEFAULT_LEAVE_MS    4000
#define DEFAULT_MAX_OPEN_S  300

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
#define BLE_MSG_MAX      320  // "cats" e "profile" sao as maiores mensagens do app
#define BLE_RX_QUEUE_LEN 8
#define STATUS_PERIOD_MS 1000

// ---------------------------------------------------------------------------
// Persistencia e log
// ---------------------------------------------------------------------------
#define NVS_NAMESPACE_APP  "feeder-cfg"
#define EVENT_LOG_CAPACITY 40
#define EVENT_LIST_MAX     10   // quantos eventos o comando "get events" devolve
