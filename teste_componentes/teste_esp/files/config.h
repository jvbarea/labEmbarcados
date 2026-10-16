#pragma once
/*
 * config.h
 * Pinos, constantes de tempo e parâmetros padrão do Alimentador de Animais.
 * Ajuste os números de pino conforme a fiação real do protótipo.
 */

// ---------------------------------------------------------------------------
// Pinos - Aquisição (HX711)
// ---------------------------------------------------------------------------
#define PIN_HX711_DT     4     // Data
#define PIN_HX711_SCK    5     // Clock

// ---------------------------------------------------------------------------
// Pinos - Atuação (servomotor + chaves fim de curso)
// ---------------------------------------------------------------------------
#define PIN_SERVO            13
#define PIN_LIMIT_OPEN       14    // chave fim de curso: porta totalmente aberta
#define PIN_LIMIT_CLOSED     27    // chave fim de curso: porta totalmente fechada
// Chaves fim de curso em INPUT_PULLUP, nível LOW = acionada (contato com GND).
#define LIMIT_SWITCH_ACTIVE_LEVEL LOW

// ---------------------------------------------------------------------------
// Servomotor de rotação contínua (360°) - pulsos em microssegundos
// ---------------------------------------------------------------------------
#define SERVO_US_STOP        1500  // pulso neutro (parado)
#define SERVO_US_OPEN_DIR    1000  // gira no sentido de abrir a porta
#define SERVO_US_CLOSE_DIR   2000  // gira no sentido de fechar a porta
#define DOOR_MOVE_TIMEOUT_MS 4000  // tempo máximo de movimento antes de considerar falha

// ---------------------------------------------------------------------------
// Balança - amostragem, filtro e detecção de estabilidade
// ---------------------------------------------------------------------------
#define WEIGHT_SAMPLE_INTERVAL_MS      50     // período de leitura do HX711
#define WEIGHT_FILTER_ALPHA            0.25f  // ganho do filtro exponencial (EMA)
#define WEIGHT_STABILITY_WINDOW        6      // nº de amostras filtradas usadas p/ checar estabilidade
#define WEIGHT_STABILITY_TOLERANCE_G   80.0f  // variação máx. (g) dentro da janela p/ considerar estável
#define WEIGHT_EMPTY_THRESHOLD_G       300.0f // abaixo disso, plataforma é considerada vazia
#define HX711_CALIBRATION_DEFAULT      420.0f // fator de calibração inicial (contagens por grama) - refinar com RF07

// ---------------------------------------------------------------------------
// Parâmetros padrão de operação (sobrescritos via app / persistidos em NVS)
// ---------------------------------------------------------------------------
#define DEFAULT_WEIGHT_MIN_G        3000.0f
#define DEFAULT_WEIGHT_MAX_G        8000.0f
#define DEFAULT_STABILIZE_MS        800     // tempo estável exigido antes de decidir (RNF02: total < 3s)
#define DEFAULT_VACATE_CONFIRM_MS   1500    // tempo com plataforma vazia antes de fechar (RNF04)

// ---------------------------------------------------------------------------
// Bluetooth (BLE) - provisionamento Wi-Fi
// ---------------------------------------------------------------------------
#define BLE_DEVICE_NAME          "AlimentadorAnimais"
#define BLE_SERVICE_UUID         "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_SSID_UUID       "6e400002-b5a3-f393-e0a9-e50e24dcca9e" // write
#define BLE_CHAR_PASS_UUID       "6e400003-b5a3-f393-e0a9-e50e24dcca9e" // write
#define BLE_CHAR_COMMAND_UUID    "6e400004-b5a3-f393-e0a9-e50e24dcca9e" // write ("CONNECT", "FORGET")
#define BLE_CHAR_STATUS_UUID     "6e400005-b5a3-f393-e0a9-e50e24dcca9e" // notify (status JSON)

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------
#define HTTP_SERVER_PORT   80
#define WIFI_CONNECT_TIMEOUT_MS  15000

// ---------------------------------------------------------------------------
// Persistência (NVS / Preferences)
// ---------------------------------------------------------------------------
#define NVS_NAMESPACE_WIFI   "wifi-cfg"
#define NVS_NAMESPACE_APP    "feeder-cfg"

// ---------------------------------------------------------------------------
// Log de eventos (RF11)
// ---------------------------------------------------------------------------
#define EVENT_LOG_CAPACITY   40
