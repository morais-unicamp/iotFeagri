/*
 * Exemplo simples para Arduino IDE.
 * No primeiro uso, conecte o celular ao AP "IOT_FEAGRI_XXXX"
 * e acesse 192.168.4.1 para configurar WiFi e acesso ao broker.
 */

#include "iotFeagri.h"

IotFeagri node;

#ifndef LED_BUILTIN
#if CONFIG_IDF_TARGET_ESP32C3
#define LED_BUILTIN 8
#else
#define LED_BUILTIN 2
#endif
#endif

static bool ledState = false;

static void applyLedState(bool on) {
  ledState = on;
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
  node.publishStatus("led_builtin", on);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  applyLedState(false);

  // Inicializa tudo. O Portal abrira se os dados estiverem faltando.
  node.begin();
  node.setFirmwareVersion("v1.1.0_Arduino");

  node.onCommand([](String command, String target, JsonObject data) {
    (void)target;
    (void)data;

    if (command == "ON" || command == "led_on") {
      applyLedState(true);
    } else if (command == "OFF" || command == "led_off") {
      applyLedState(false);
    } else if (command == "toggle_led") {
      applyLedState(!ledState);
    }
  });
}

void loop() {
  // Mantem a conexao ativa e processa Portal, MQTT, OTA e heartbeat.
  node.loop();

  // Publica uma temperatura fake a cada 10 segundos.
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 10000) {
    lastUpdate = millis();
    float valorSorteado = 20.0 + (random(0, 100) / 10.0);
    node.publish("temperature", valorSorteado);
    node.publishStatus("led_builtin", ledState);
    Serial.print("Dados enviados: ");
    Serial.print(valorSorteado);
    Serial.print(" C | LED=");
    Serial.println(ledState ? "ON" : "OFF");
  }
}
