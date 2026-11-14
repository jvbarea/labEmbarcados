/*
 * Alimentador de Animais - Firmware ESP32
 * PCS3858 - Laboratório de Sistemas Embarcados - Escola Politécnica da USP
 *
 * Fluxo geral (ver README.md para detalhes e bibliotecas necessárias):
 *  1. WeightSensor lê continuamente as células de carga via HX711, filtra
 *     e detecta estabilidade.
 *  2. AccessStateMachine decide, com base no peso, se abre ou mantém
 *     fechada a porta do compartimento (DoorActuator), respeitando o
 *     comportamento seguro em falha (RNF03) e o fechamento só com a
 *     plataforma desocupada (RNF04).
 *  3. BleProvisioning fica sempre ativo, recebendo credenciais Wi-Fi do
 *     aplicativo quando necessário.
 *  4. WifiManager tenta conectar automaticamente usando credenciais já
 *     salvas na flash (RF09/RF10); só depende do BLE quando ainda não há
 *     credenciais ou quando o usuário quer trocá-las.
 *  5. HttpApi expõe a configuração, comandos manuais, calibração e eventos
 *     na rede local, assim que o Wi-Fi conecta.
 */
#include "config.h"
#include "Storage.h"
#include "EventLog.h"
#include "WeightSensor.h"
#include "DoorActuator.h"
#include "AccessStateMachine.h"
#include "WifiManager.h"
#include "BleProvisioning.h"
#include "HttpApi.h"

Storage storage;
EventLog eventLog;
WeightSensor weightSensor;
DoorActuator doorActuator;
AccessStateMachine accessStateMachine;
WifiManager wifiManager;
BleProvisioning bleProvisioning;
HttpApi httpApi;

bool httpServerStarted = false;

void startHttpServerIfNeeded() {
  if (httpServerStarted) return;
  if (!wifiManager.isConnected()) return;

  httpApi.begin(&storage, &weightSensor, &doorActuator, &accessStateMachine, &eventLog);
  httpServerStarted = true;
  Serial.printf("[HTTP] Disponível em http://%s/\n", wifiManager.ipAddress().c_str());
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Alimentador de Animais - boot ===");

  storage.begin();
  AppConfig cfg = storage.loadAppConfig();

  weightSensor.begin(cfg);
  doorActuator.begin();
  accessStateMachine.begin(&weightSensor, &doorActuator, &eventLog, cfg);

  // BLE sempre ativo: provisionamento inicial e canal de fallback (RF08).
  bleProvisioning.begin(&wifiManager);

  // Wi-Fi: tenta conectar sozinho com o que já estiver salvo na flash.
  // Não é necessário reenviar credenciais a cada boot (RF09/RF10).
  wifiManager.begin(&storage);
  wifiManager.connectFromStoredCredentials();
  startHttpServerIfNeeded();

  Serial.println("=== Setup concluído ===\n");
}

void loop() {
  // 1) Aquisição e filtragem do peso (não-bloqueante, respeita seu próprio período).
  weightSensor.update();

  // 2) Lógica de decisão de acesso à ração.
  accessStateMachine.update();

  // 3) Atuação física da porta (monitora chaves fim de curso e timeout).
  doorActuator.update();

  // 4) BLE: aplica credenciais recebidas do app, se houver.
  if (bleProvisioning.hasPendingCredentials()) {
    bleProvisioning.consumePendingCredentials();
  }
  if (bleProvisioning.hasPendingForget()) {
    storage.clearWifiCredentials();
    bleProvisioning.consumePendingForget();
    Serial.println("[WiFi] Credenciais removidas por comando do aplicativo.");
  }

  // 5) Wi-Fi: mantém/retoma a conexão em segundo plano.
  wifiManager.update();
  startHttpServerIfNeeded();

  // 6) HTTP: atende requisições do aplicativo (fase 2), se conectado.
  if (httpServerStarted) {
    httpApi.handleClient();
  }

  // Status resumido publicado por BLE para acompanhamento em tempo real.
  static unsigned long lastStatusMs = 0;
  if (millis() - lastStatusMs > 1000) {
    lastStatusMs = millis();
    String status = "{\"weight_g\":" + String(weightSensor.filteredWeightG(), 1) +
                     ",\"wifi\":" + String(wifiManager.isConnected() ? "true" : "false") + "}";
    bleProvisioning.publishStatus(status);
  }
}
