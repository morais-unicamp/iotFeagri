#include <Arduino.h>
#include <iotFeagri.h>

/*
 * Exemplo simples para aluno iniciante.
 * No primeiro uso, a placa cria um ponto de acesso "IOT_FEAGRI_XXXX".
 * Depois de configurada, ela:
 * 1. publica temperatura fake em .../data
 * 2. publica o estado do LED em .../status
 * 3. recebe comandos em .../cmd
 */

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

  // Inicializa a placa e tenta conectar. Se falhar, abre Portal 192.168.4.1.
  node.setFirmwareVersion("v1.1.1_example");
  node.begin();

  node.onCommand([](String command, String target, JsonObject data) {
    (void)target;
    String normalized = command;
    normalized.trim();
    String lowered = normalized;
    lowered.toLowerCase();
    Serial.printf("Comando recebido via MQTT: %s\n", command.c_str());

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
  node.loop();

  // Publica uma temperatura fake a cada 10 segundos.
  static unsigned long lastMsg = 0;
  if (millis() - lastMsg > 10000) {
    lastMsg = millis();
    float valorTemp = random(200, 300) / 10.0;
    node.publish("temperature", valorTemp);
    node.publishStatus("led_builtin", ledState);
    Serial.printf("Dados enviados: %.1f C | LED=%s\n", valorTemp,
                  ledState ? "ON" : "OFF");
  }
}
