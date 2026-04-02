/*
 * EXEMPLO AVANCADO - Dashboard IoT (ESP32 + Active Time Sync)
 *
 * Funcionalidades:
 * 1. Conecta no WiFi e MQTT
 * 2. Ao iniciar, PEDE a hora certa para o servidor de tempo (rtc_feagri)
 * 3. Ajusta o relogio interno com a resposta
 * 4. Segue contando o tempo sozinho (RTC interno do ESP32)
 * 5. Envia telemetria com timestamp exato
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <time.h>

// --- 1. CONFIGURACOES DE REDE E BROKER ---
const char *WIFI_SSID = "NOME_DA_REDE";
const char *WIFI_PASS = "SENHA_DA_REDE";

const char *MQTT_BROKER = "192.168.0.X";
const int MQTT_PORT = 1883;
const char *MQTT_USER = "seu_usuario_mqtt";
const char *MQTT_PASS = "sua_senha_mqtt";

// --- 2. IDENTIFICACAO ---
const char *USER_TOPIC_ID = "ra123456";

String deviceId;

// --- 3. CONFIGURACAO DO TIME SYNC (ATIVO) ---
const char *TOPIC_RTC_REQ = "feagri/rtc/requisicao"; // Onde pedimos a hora
const char *TOPIC_RTC_RES = "feagri/rtc/resposta";   // Onde a resposta chega

// Hardware
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif
const int PIN_LED = LED_BUILTIN;

// --- OBJETOS GLOBAIS ---
WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsgTime = 0;
bool timeSynchronized = false;

String topicPub;
String topicSub;

// --- AJUSTA RELOGIO INTERNO ---
void setInternalTime(unsigned long unixTime) {
  struct timeval tv;
  tv.tv_sec = unixTime;
  tv.tv_usec = 0;
  settimeofday(&tv, NULL);
  timeSynchronized = true;
  Serial.printf(">> RELOGIO SINCRONIZADO: %lu (Agora o ESP32 segue sozinho)\n",
                unixTime);
}

// --- SOLICITA HORA AO SERVIDOR ---
void requestTimeSync() {
  if (timeSynchronized)
    return; // Se ja temos, nao precisa pedir de novo

  JsonDocument doc;
  doc["type"] = "COMMAND";
  doc["command"] = "get_time";
  doc["target_id"] = "todos"; // Pede pra qualquer um que puder responder
  doc["sender_user"] = "esp32_sync_client"; // Apenas informativo

  String msg;
  serializeJson(doc, msg);
  client.publish(TOPIC_RTC_REQ, msg.c_str());
  Serial.println(">> Solicitacao de hora enviada...");
}

// --- CALLBACK MQTT ---
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++)
    msg += (char)payload[i];

  String topicStr = String(topic);

  // 1. TRATA RESPOSTA DE HORA (Sync)
  if (topicStr == TOPIC_RTC_RES) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, msg);
    if (!error) {
      const char *cmd = doc["command"];
      // Servidor responde com um comando "set_timerstamp" e o valor no "value"
      if (String(cmd) == "set_timerstamp") {
        double val = doc["value"]; // Vem como double/number no JSON
        if (val > 1600000000) {
          setInternalTime((unsigned long)val);
        }
      }
    }
    return;
  }

  // 2. SEUS COMANDOS NORMAIS (ON/OFF)
  if (topicStr == topicSub) {
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
          newState = "ON";
        } else if (String(cmd) == "OFF") {
          digitalWrite(PIN_LED, LOW);
          newState = "OFF";
        }

        // ACK
        if (newState != "") {
          JsonDocument ackDoc;
          ackDoc["type"] = "STATUS";
          ackDoc["client_id"] = deviceId;
          JsonObject dataObj = ackDoc["data"].to<JsonObject>();
          dataObj["led_state"] = newState;
          String ackJson;
          serializeJson(ackDoc, ackJson);
          client.publish(topicPub.c_str(), ackJson.c_str());
        }
      }
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando ao MQTT...");
    if (client.connect(deviceId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println("OK");

      // Assina comandos normais
      client.subscribe(topicSub.c_str());

      // Assina RESPOSTA de hora
      client.subscribe(TOPIC_RTC_RES);

      Serial.println("Conectado e assinando topicos.");

      // Assim que conecta, pede a hora (se ainda nao tiver)
      if (!timeSynchronized) {
        requestTimeSync();
      }

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

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  deviceId = String(USER_TOPIC_ID) + "_" + mac.substring(6);

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

    // So publica se tivermos hora certa? Ou publica mesmo assim?
    // Exemplo: publica sempre, mas so poe timestamp se tiver.

    float temp = 20.0 + (random(0, 100) / 10.0);

    JsonDocument doc;
    doc["type"] = "MEASUREMENT";
    doc["client_id"] = deviceId;

    // --- TIMESTAMP ---
    if (timeSynchronized) {
      struct timeval tv;
      gettimeofday(&tv, NULL);

      // Formata timestamp (MS)
      long long timestampMs =
          ((long long)tv.tv_sec * 1000) + (tv.tv_usec / 1000);
      doc["timestamp"] = timestampMs;
    }

    JsonObject data = doc["data"].to<JsonObject>();
    data["Temperature"] = temp;

    String jsonStr;
    serializeJson(doc, jsonStr);

    Serial.print("Enviando: ");
    Serial.println(jsonStr);
    client.publish(topicPub.c_str(), jsonStr.c_str());
  }
}
