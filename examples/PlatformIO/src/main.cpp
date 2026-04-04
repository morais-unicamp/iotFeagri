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

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  applyLedState(false);

  // Inicializa a placa e tenta conectar. Se falhar, abre Portal 192.168.4.1.
  node.begin();
  node.setFirmwareVersion("v1.1.0_example");

  node.onCommand([](String command, String target, JsonObject data) {
    (void)target;
    (void)data;
    Serial.printf("Comando recebido via MQTT: %s\n", command.c_str());

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
