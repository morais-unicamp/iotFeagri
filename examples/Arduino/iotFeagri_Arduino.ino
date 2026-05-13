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

static bool parseBoolLike(JsonVariantConst value, bool fallback) {
  if (value.is<bool>()) {
    return value.as<bool>();
  }

  if (value.is<const char*>()) {
    String text = value.as<const char*>();
    text.trim();
    text.toLowerCase();
    if (text == "true" || text == "1" || text == "on") {
      return true;
    }
    if (text == "false" || text == "0" || text == "off") {
      return false;
    }
  }

  if (value.is<int>()) {
    return value.as<int>() != 0;
  }

  return fallback;
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  applyLedState(false);

  // Inicializa tudo. O Portal abrira se os dados estiverem faltando.
  node.begin();
  node.setFirmwareVersion("v1.1.1_Arduino");

  node.onCommand([](String command, String target, JsonObject data) {
    (void)target;
    String normalized = command;
    normalized.trim();
    String lowered = normalized;
    lowered.toLowerCase();

    if (lowered == "on" || lowered == "led_on") {
      applyLedState(true);
    } else if (lowered == "off" || lowered == "led_off") {
      applyLedState(false);
    } else if (lowered == "toggle_led") {
      applyLedState(!ledState);
    } else if (lowered == "set_led" || lowered == "led_builtin") {
      applyLedState(parseBoolLike(data["value"], ledState));
    }
  });
}

void loop() {
  // Mantem a conexao ativa e processa Portal, MQTT, OTA e heartbeat.
  node.loop();

  // Publica uma temperatura fake a cada 30 segundos.
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 30000) {
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
