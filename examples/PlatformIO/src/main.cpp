#include <Arduino.h>
#include <iotFeagri.h>

/*
 * Exemplo Profissional - PlataformIO
 * A biblioteca iotFeagri agora gerencia credenciais via Portal de Configuração.
 * No primeiro uso, a placa criará um ponto de acesso "IOT_FEAGRI_XXXX".
 */

IotFeagri node; 

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

void setup() {
  // Inicializa a placa e tenta conectar. Se falhar, abre Portal 192.168.4.1.
  node.begin();
  
  node.setFirmwareVersion("v1.1.0_PIO");

  // Callback para comandos vindos da Dashboard
  node.onCommand([](String command, String target, JsonObject data) {
    Serial.printf("Comando recebido via MQTT: %s\n", command.c_str());
    if (command == "ON") digitalWrite(LED_BUILTIN, HIGH);
    else if (command == "OFF") digitalWrite(LED_BUILTIN, LOW);
  });

  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  // Mantém a sanidade da conexão, Heartbeat e Porta de Configuração
  node.loop();

  // Publica telemetria a cada 10 segundos
  static unsigned long lastMsg = 0;
  if (millis() - lastMsg > 10000) {
    lastMsg = millis();
    float valorTemp = random(200, 300) / 10.0;
    node.publish("temperatura", valorTemp);
    Serial.printf("Telemetria enviada: %.1f\n", valorTemp);
  }
}
