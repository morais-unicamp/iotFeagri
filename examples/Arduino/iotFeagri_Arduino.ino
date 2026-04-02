/*
 * PROJETO: iotFeagri_Arduino
 * 
 * Agora com Portal de Configuração!
 * No primeiro uso, conecte seu celular no AP "IOT_FEAGRI_XXXX"
 * e acesse 192.168.4.1 para configurar WiFi e MQTT.
 */

#include "iotFeagri.h"

IotFeagri node;

#ifndef LED_BUILTIN
#define LED_BUILTIN 2 
#endif

void setup() {
  // Inicializa tudo. O Portal abrirá se os dados estiverem faltando.
  node.begin();
  
  // Versão do Firmware (visto na Dashboard)
  node.setFirmwareVersion("v1.1.0_Arduino");

  // Ações para comandos recebidos
  node.onCommand([](String command, String target, JsonObject data) {
    if (command == "ON") digitalWrite(LED_BUILTIN, HIGH);
    else if (command == "OFF") digitalWrite(LED_BUILTIN, LOW);
  });

  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  // Mantém a conexão ativa e processa o Portal/Heartbeat
  node.loop();

  // Exemplo de envio de dado a cada 10 segundos
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 10000) {
    lastUpdate = millis();
    float valorSorteado = 20.0 + (random(0, 100) / 10.0);
    node.publish("Sensor_Arduino", valorSorteado);
    Serial.print("Dado enviado: ");
    Serial.println(valorSorteado);
  }
}
