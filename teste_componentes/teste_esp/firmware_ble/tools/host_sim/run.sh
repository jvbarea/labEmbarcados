#!/usr/bin/env bash
# Compila a logica do firmware para o PC e roda os cenarios de sim_main.cpp.
#
#   bash tools/host_sim/run.sh        (de dentro de firmware_ble, em Linux ou WSL)
#   bash tools/host_sim/run.sh -v     mostra tambem o que o firmware imprime no serial
#
# Precisa de g++ (>= 9) e da ArduinoJson baixada pelo PlatformIO: rode 'pio run'
# uma vez antes, para existir .pio/libdeps/esp32dev/ArduinoJson.
# OUT muda onde o executavel e gerado (padrao: /tmp/feeder_sim).
set -euo pipefail

cd "$(dirname "$0")/../.."
OUT="${OUT:-/tmp/feeder_sim}"
JSON_DIR=".pio/libdeps/esp32dev/ArduinoJson/src"
if [ ! -d "$JSON_DIR" ]; then
  echo "falta a ArduinoJson em $JSON_DIR - rode 'pio run' uma vez antes" >&2
  exit 2
fi

# AddressSanitizer e UBSan: estouro de buffer ou conta com sinal errado no
# firmware viram erro aqui, em vez de um reset misterioso na placa.
# A pilha BLE de mentira e recriada a cada "reboot" e nunca liberada (na placa
# ela nasce uma vez so), entao o detector de vazamento so faria barulho.
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"

g++ -std=gnu++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-maybe-uninitialized \
  -fsanitize=address,undefined -fno-sanitize-recover=undefined \
  -DUSE_HX711=0 \
  -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 \
  -DARDUINOJSON_ENABLE_PROGMEM=0 \
  -I tools/host_sim/stubs -I src -I "$JSON_DIR" \
  src/AccessStateMachine.cpp src/AppProtocol.cpp src/BleControl.cpp \
  src/DoorActuator.cpp src/EventLog.cpp src/Hx711WeightSource.cpp \
  src/MockWeightSource.cpp src/Storage.cpp \
  tools/host_sim/stubs/stubs.cpp tools/host_sim/sim_main.cpp \
  -o "$OUT"

"$OUT" "$@"
