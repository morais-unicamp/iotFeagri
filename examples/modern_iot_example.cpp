/*
 * EXEMPLO MODERNO - Biblioteca iotFeagri (ESP32)
 * 
 * Este código demonstra como usar a biblioteca iotFeagri para conectar
 * um dispositivo à Dashboard com o mínimo de esforço.
 * 
 * Bibliotecas Necessárias:
 * - iotFeagri (disponível no GitHub ou na pasta do projeto)
 * - PubSubClient
 * - ArduinoJson
 */

#include <Arduino.h>
#include <iotFeagri.h>

// --- 1. CONFIGURAÇÕES DO USUÁRIO ---
const char* MEU_RA = "ra123456";  // Substitua pelo seu RA ou e-mail (sem @)
const char* MQTT_USER = "seu_usuario";
const char* MQTT_PASS = "sua_senha";
const char* BROKER_URL = "143.106.87.144"; // Endereço do Servidor FEAGRI

// --- 2. INSTÂNCIA DO SISTEMA ---
IotFeagri node(MEU_RA);

// --- 3. CALLBACK DE COMANDOS (OPCIONAL) ---
// Esta função é chamada sempre que você clica em um botão na Dashboard
void aoReceberComando(String cmd, String target, JsonObject data) {
    Serial.printf("Comando Recebido: %s para o alvo: %s\n", cmd.c_str(), target.c_str());
    
    if (cmd == "ON") {
        digitalWrite(LED_BUILTIN, HIGH);
        Serial.println("Luz Ligada!");
    } else if (cmd == "OFF") {
        digitalWrite(LED_BUILTIN, LOW);
        Serial.println("Luz Desligada!");
    }
}

void setup() {
    // Inicializa a placa e conecta ao sistema
    // A rede WiFi "IoT-local" é gerenciada internamente pela biblioteca.
    node.begin(BROKER_URL, 1883, MQTT_USER, MQTT_PASS);
    
    // Define a versão atual (ajuda o administrador no controle de OTA)
    node.setFirmwareVersion("v1.2.0");
    
    // Configura o que fazer quando receber comandos
    node.onCommand(aoReceberComando);

    // Configura hardware local
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    // Mantém a conexão viva e processa comandos/OTA
    node.loop();

    // Exemplo de publicação regular (a cada 10 segundos)
    static unsigned long ultimaEnvio = 0;
    if (millis() - ultimaEnvio > 10000) {
        ultimaEnvio = millis();
        
        float temperaturaSimulada = 22.0 + (random(0, 50) / 10.0);
        
        // Publica na Dashboard. O tópico é gerado automaticamente!
        node.publish("Temperature", temperaturaSimulada);
        
        Serial.print("Dados enviados: ");
        Serial.println(temperaturaSimulada);
    }
}
