/*
 * EXEMPLO AVANCADO - Dashboard IoT (ESP32 + Active Time Sync + OTA)
 *
 * Funcionalidades:
 * 1. Conecta no WiFi e MQTT (+ Secure para HTTPS)
 * 2. Time Sync ativo (pede hora ao conectar)
 * 3. Telemetria com timestamp
 * 4. SUPORTE A ATUALIZACAO OTA (Firmware Update via Dashboard)
 *
 * --- NOTA SOBRE PARTICIONAMENTO (OTA) ---
 * Este exemplo cabe no particionamento padrao do ESP32 (~1.2MB).
 * Porem, se seu projeto crescer, adicione no `platformio.ini`:
 * board_build.partitions = min_spiffs.csv
 *
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <time.h>

// OTA: Bibliotecas de Update
#include <ESP32httpUpdate.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// --- 1. CONFIGURACOES DE REDE E BROKER ---
const char *WIFI_SSID = "NOME_DA_REDE";
const char *WIFI_PASS = "SENHA_DA_REDE";

const char *MQTT_BROKER = "192.168.0.X";
const int MQTT_PORT = 1883;
const char *MQTT_USER = "seu_usuario_mqtt";
const char *MQTT_PASS = "sua_senha_mqtt";

// --- 2. IDENTIFICACAO ---
const char *USER_TOPIC_ID = "ra123456"; // Seu identificador unico (Group ID)

String deviceId;

// --- 3. TOPICOS ---
const char *TOPIC_RTC_REQ = "feagri/rtc/requisicao";
const char *TOPIC_RTC_RES = "feagri/rtc/resposta";

// Topicos OTA (Padrao feagri)
const char *TOPIC_OTA_CMD = "feagri/firmware/update/cmd";
const char *TOPIC_OTA_STATUS = "feagri/firmware/update/status";

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

// --- FUNCOES AUXILIARES ---
void setInternalTime(unsigned long unixTime) {
  struct timeval tv;
  tv.tv_sec = unixTime;
  tv.tv_usec = 0;
  settimeofday(&tv, NULL);
  timeSynchronized = true;
  Serial.printf(">> RELOGIO SINCRONIZADO: %lu\n", unixTime);
}

void requestTimeSync() {
  if (timeSynchronized)
    return;
  JsonDocument doc;
  doc["type"] = "COMMAND";
  doc["command"] = "get_time";
  doc["target_id"] = "todos";
  doc["sender_user"] = deviceId;
  String msg;
  serializeJson(doc, msg);
  client.publish(TOPIC_RTC_REQ, msg.c_str());
}

// --- FUNCAO DE ATUALIZACAO OTA ---
void performUpdate(String url, String hash) {
  Serial.println(">> INICIANDO UPDATE OTA...");
  Serial.println(">> URL: " + url);

  // 1. Notifica inicio
  JsonDocument doc;
  doc["type"] = "STATUS";
  doc["client_id"] = deviceId;
  doc["status"] = "UPDATING";
  doc["progress"] = 0;
  String statusMsg;
  serializeJson(doc, statusMsg);
  client.publish(TOPIC_OTA_STATUS, statusMsg.c_str());

  // 2. Configura Client Seguro (ou inseguro para facilitar)
  WiFiClientSecure secureClient;
  secureClient.setInsecure(); // Em producao, use certificados!

  // 3. Callback de progresso (opcional, trava loop se for muito verboso)
  ESPhttpUpdate.onProgress([](int cur, int total) {
    static int lastP = 0;
    int pct = map(cur, 0, total, 0, 100);
    if (pct > lastP + 10) { // A cada 10%
      lastP = pct;
      Serial.printf("OTA: %d%%\n", pct);
    }
  });

  // 4. Executa Update
  t_httpUpdate_return ret = ESPhttpUpdate.update(secureClient, url);

  // 5. Verifica resultado
  String finalStatus = "FAILED";
  String errorMsg = "";

  switch (ret) {
  case HTTP_UPDATE_FAILED:
    errorMsg = ESPhttpUpdate.getLastErrorString();
    Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n",
                  ESPhttpUpdate.getLastError(), errorMsg.c_str());
    finalStatus = "FAILED";
    break;

  case HTTP_UPDATE_NO_UPDATES:
    Serial.println("HTTP_UPDATE_NO_UPDATES");
    finalStatus = "NO_UPDATES";
    break;

  case HTTP_UPDATE_OK:
    Serial.println("HTTP_UPDATE_OK");
    finalStatus = "SUCCESS";
    break;
  }

  // 6. Notifica final (se falhou, pq se for OK ele reinicia antes)
  JsonDocument docFinal;
  docFinal["type"] = "STATUS";
  docFinal["client_id"] = deviceId;
  docFinal["status"] = finalStatus;
  if (errorMsg != "")
    docFinal["error"] = errorMsg;
  String finalMsg;
  serializeJson(docFinal, finalMsg);
  client.publish(TOPIC_OTA_STATUS, finalMsg.c_str());

  if (ret == HTTP_UPDATE_OK) {
    Serial.println("Rebooting...");
    ESP.restart();
  }
}

// --- CALLBACK MQTT ---
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++)
    msg += (char)payload[i];
  String topicStr = String(topic);

  // 1. RESPOSTA DE HORA
  if (topicStr == TOPIC_RTC_RES) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, msg);
    if (!error) {
      if (String((const char *)doc["command"]) == "set_timerstamp") {
        double val = doc["value"];
        if (val > 1600000000)
          setInternalTime((unsigned long)val);
      }
    }
    return;
  }

  // 2. COMANDOS DE OTA (Global)
  if (topicStr == TOPIC_OTA_CMD) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, msg);
    if (error)
      return;

    const char *cmd = doc["command"];
    const char *target = doc["target_id"];

    // LOGICA DE GRUPO:
    // O grupo agora eh o proprio USER_TOPIC_ID.
    // Entao aceitamos: deviceId (eu), "todos" e USER_TOPIC_ID (meu grupo)
    bool isForMe = (String(target) == deviceId);
    bool isForAll = (String(target) == "todos");
    bool isForGroup = (String(target) == USER_TOPIC_ID);

    if (!isForMe && !isForAll && !isForGroup)
      return;

    if (String(cmd) == "update_firmware") {
      String url = doc["value"]; // URL do binario
      String hash = doc["hash"]; // MD5 (opcional)
      if (url.length() > 5) {
        performUpdate(url, hash);
      }
    }
    return;
  }

  // 3. SEUS COMANDOS NORMAIS (ON/OFF)
  if (topicStr == topicSub) {
    JsonDocument doc;
    deserializeJson(doc, msg);
    const char *type = doc["type"];
    const char *cmd = doc["command"];
    const char *target = doc["target_id"];
    if (String(type) == "COMMAND") {
      if (target && (String(target) != deviceId && String(target) != "todos"))
        return;
      // Nota: nos comandos locais, geralmente nao filtramos por grupo,
      // mas se quiser pode adicionar: && String(target) != USER_TOPIC_ID

      String newState = "";
      if (String(cmd) == "ON") {
        digitalWrite(PIN_LED, HIGH);
        newState = "ON";
      } else if (String(cmd) == "OFF") {
        digitalWrite(PIN_LED, LOW);
        newState = "OFF";
      }

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

void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando ao MQTT...");
    if (client.connect(deviceId.c_str(), MQTT_USER, MQTT_PASS)) {

      // Assinaturas
      client.subscribe(topicSub.c_str()); // Meus comandos
      client.subscribe(TOPIC_RTC_RES);    // Hora
      client.subscribe(TOPIC_OTA_CMD);    // Atualizacoes globais

      Serial.println("OK. Assinado em tudo.");
      if (!timeSynchronized)
        requestTimeSync();

    } else {
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED)
    delay(500);

  // PREFIXO = USER_TOPIC_ID (Ex: ra123456_A1B2C3)
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  deviceId = String(USER_TOPIC_ID) + "_" + mac.substring(6);
  Serial.print("DEVICE ID: ");
  Serial.println(deviceId);

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

    JsonDocument doc;
    doc["type"] = "MEASUREMENT";
    doc["client_id"] = deviceId;

    if (timeSynchronized) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      long long timestampMs =
          ((long long)tv.tv_sec * 1000) + (tv.tv_usec / 1000);
      doc["timestamp"] = timestampMs;
    }

    JsonObject data = doc["data"].to<JsonObject>();
    float temp = 20.0 + (random(0, 100) / 10.0);
    data["Temperature"] = temp;

    String jsonStr;
    serializeJson(doc, jsonStr);
    client.publish(topicPub.c_str(), jsonStr.c_str());
  }
}
