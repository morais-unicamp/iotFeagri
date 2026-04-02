/*
 * EXEMPLO SIMPLIFICADO - Dashboard IoT (ESP32)
 *
 * COMPATIBILIDADE:
 * - Funciona no PlatformIO (copie para main.cpp)
 * - Funciona na Arduino IDE (copie para um Sketch novo)
 *
 * PARA USUARIOS ARDUINO IDE:
 * 1. Instale as bibliotecas pelo Gerenciador de Bibliotecas (Sketch -> Incluir
 * Biblioteca):
 *    - "PubSubClient" por Nick O'Leary
 *    - "ArduinoJson" por Benoit Blanchon (versao 7+)
 * 2. Selecione sua placa em Ferramentas -> Placa (ESP32 Dev Module, C3 DevKit,
 * etc)
 *
 * Funcionalidades:
 * 1. Conecta no WiFi e MQTT
 * 2. Gera ID unico (MAC Address)
 * 3. Envia temperatura fake e aceita comandos ON/OFF
 *
 * --- IMPORTANTE: CONFIGURACAO NA DASHBOARD ---
 * Para que este dispositivo apareca automaticamente na sua tela:
 * 1. Acesso a pagina de "Configuracoes" (Icone de Engrenagem).
 * 2. No campo "Topicos Inscritos" (Subscribe Topics), adicione o seu topico
 * base. Exemplo: Se seu USER_TOPIC_ID abaixo for "ra123456", adicione:
 *    feagri/ra123456/#
 * 3. Salve. Assim que o ESP32 enviar o primeiro dado, ele aparecera na lista.
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

// --- 1. CONFIGURACOES DE REDE E BROKER ---
const char *WIFI_SSID = "NOME_DA_REDE";
const char *WIFI_PASS = "SENHA_DA_REDE";

const char *MQTT_BROKER = "192.168.0.X";
const int MQTT_PORT = 1883;

// Credenciais do BROKER MQTT (infraestrutura)
const char *MQTT_USER = "seu_usuario_mqtt";
const char *MQTT_PASS = "sua_senha_mqtt";

// --- 2. HIERARQUIA DE TOPICOS ---
// Defina aqui o identificador unico da SUA pasta no Broker.
// Use seu RA ou Email para garantir que ninguem sobscreva seus dados.
const char *USER_TOPIC_ID = "ra123456";

String deviceId;

// Hardware
#ifndef LED_BUILTIN
#define LED_BUILTIN                                                            \
  2 // Fallback para ESP32 DevKit V1 (mas no C3 costuma ser 8 ou outro)
#endif
const int PIN_LED = LED_BUILTIN;

// --- OBJETOS GLOBAIS ---
WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsgTime = 0;

String topicPub;
String topicSub;

// --- CALLBACK MQTT ---
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++)
    msg += (char)payload[i];
  Serial.printf("Msg em [%s]: %s\n", topic, msg.c_str());

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, msg);

  if (!error) {
    const char *type = doc["type"];
    const char *cmd = doc["command"];
    const char *target = doc["target_id"];

    if (String(type) == "COMMAND") {
      if (target && (String(target) != deviceId && String(target) != "todos"))
        return;

      String newState = "";
      if (String(cmd) == "ON") {
        digitalWrite(PIN_LED, HIGH);
        Serial.println(">> LED LIGADO");
        newState = "ON";
      } else if (String(cmd) == "OFF") {
        digitalWrite(PIN_LED, LOW);
        Serial.println(">> LED DESLIGADO");
        newState = "OFF";
      }

      // --- ENVIA ACK (CONFIRMACAO) ---
      if (newState != "") {
        JsonDocument ackDoc;
        ackDoc["type"] = "STATUS";
        ackDoc["client_id"] = deviceId;
        // ackDoc["result"] = "SUCCESS"; // Opcional

        // Retorna o estado atual no campo data para a dashboard atualizar o
        // icone se tiver
        JsonObject dataObj = ackDoc["data"].to<JsonObject>();
        dataObj["led_state"] = newState;

        String ackJson;
        serializeJson(ackDoc, ackJson);
        client.publish(topicPub.c_str(), ackJson.c_str());
        Serial.print(">> ACK Enviado: ");
        Serial.println(ackJson);
      }
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando ao MQTT...");
    if (client.connect(deviceId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println("OK");
      client.subscribe(topicSub.c_str());
      Serial.print("Inscrito em: ");
      Serial.println(topicSub);
    } else {
      Serial.print("Falha rc=");
      Serial.print(client.state());
      Serial.println(" tente em 5s");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Conectando WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK");

  // --- OBTEM E EXIBE O MAC ADDRESS ---
  // IMPORTANTE: Envie este endereço para sinfo@unicamp.br para liberar acesso!
  String macRaw = WiFi.macAddress();
  Serial.println("\n------------------------------------------------");
  Serial.print("   ENDEREÇO MAC: ");
  Serial.println(macRaw);
  Serial.println("   (Copie este valor e envie para sinfo@unicamp.br)");
  Serial.println("------------------------------------------------\n");

  // Gera ID pelo MAC (Prefixo = USER_TOPIC_ID)
  String mac = macRaw;
  mac.replace(":", "");
  deviceId = String(USER_TOPIC_ID) + "_" + mac.substring(6);
  Serial.print("DEVICE ID: ");
  Serial.println(deviceId);

  // Monta Topicos: feagri/<USER_ID>/devices/<DEVICE_ID>/...
  // Padrao atualizado para compatibilidade com Dashboard v2
  topicPub =
      String("feagri/") + USER_TOPIC_ID + "/devices/" + deviceId + "/telemetry";
  topicSub =
      String("feagri/") + USER_TOPIC_ID + "/devices/" + deviceId + "/cmd";

  client.setServer(MQTT_BROKER, MQTT_PORT);
  client.setCallback(mqttCallback);
}

void loop() {
  if (!client.connected())
    reconnect();
  client.loop();

  unsigned long now = millis();
  if (now - lastMsgTime > 5000) {
    lastMsgTime = now;

    // Temperatura fake para teste
    float temp = 20.0 + (random(0, 100) / 10.0);

    JsonDocument doc;
    doc["type"] = "MEASUREMENT";
    doc["client_id"] = deviceId;

    // Opcional: Se quiser agrupar os dados no dashboard, use "data"
    // O backend suporta tanto "temperature": 20 quanto "data": {"temperature":
    // 20} O padrao mais robusto e usar um objeto "data":
    JsonObject data = doc["data"].to<JsonObject>();
    data["Temperature"] = temp;
    // data["Humidity"] = 60;

    String jsonStr;
    serializeJson(doc, jsonStr);

    Serial.print("Enviando: ");
    Serial.println(jsonStr);
    client.publish(topicPub.c_str(), jsonStr.c_str());
  }
}
