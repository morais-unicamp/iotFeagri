#include "iotFeagri.h"

// Namespace e Chaves NVS
#define PREF_NAME "iot-feagri"
#define KEY_RA "ra"
#define KEY_WIFI_PASS "w_pass"
#define KEY_MQTT_HOST "m_host"
#define KEY_MQTT_PORT "m_port"
#define KEY_MQTT_USER "m_user"
#define KEY_MQTT_PASS "m_pass"
#define KEY_FW_SERVER "fw_s"
#define KEY_TLS "tls"

IotFeagri* _instance = nullptr;

IotFeagri::IotFeagri(const char* ra_default) : _ra(ra_default), _mqttClient(_espClient) {
    _instance = this;
    _lastReconnectAttempt = 0;
    _lastMetricsTime = 0;
    _lastHeartbeatTime = 0;
    _timeSynced = false;
    _mqttPort = 1883;
    _fwVersion = "v1.0.0";
    _portalActive = false;
    _portalServer = nullptr;
    _dnsServer = nullptr;
    _useTls = false;
}

void IotFeagri::loadConfig() {
    Preferences prefs;
    prefs.begin(PREF_NAME, true);
    _ra = prefs.getString(KEY_RA, _ra);
    _wifiPass = prefs.getString(KEY_WIFI_PASS, "");
    _mqttBroker = prefs.getString(KEY_MQTT_HOST, "leandro144.feagri.unicamp.br");
    _mqttPort = prefs.getInt(KEY_MQTT_PORT, 1883);
    _mqttUser = prefs.getString(KEY_MQTT_USER, "");
    _mqttPass = prefs.getString(KEY_MQTT_PASS, "");
    _fwServer = prefs.getString(KEY_FW_SERVER, "leandro144.feagri.unicamp.br");
    _useTls = prefs.getBool(KEY_TLS, false);
    prefs.end();
}

void IotFeagri::saveConfig() {
    Preferences prefs;
    prefs.begin(PREF_NAME, false);
    prefs.putString(KEY_RA, _ra);
    prefs.putString(KEY_WIFI_PASS, _wifiPass);
    prefs.putString(KEY_MQTT_HOST, _mqttBroker);
    prefs.putInt(KEY_MQTT_PORT, _mqttPort);
    prefs.putString(KEY_MQTT_USER, _mqttUser);
    prefs.putString(KEY_MQTT_PASS, _mqttPass);
    prefs.putString(KEY_FW_SERVER, _fwServer);
    prefs.putBool(KEY_TLS, _useTls);
    prefs.end();
}

void IotFeagri::begin() {
    Serial.begin(115200);
    delay(100);
    loadConfig();

    // Se não tiver RA ou Senha WiFi salvas, entra em modo portal
    if (_ra.length() == 0 || _wifiPass.length() == 0) {
        Serial.println(">> Configuracao incompleta. Iniciando Portal...");
        startPortal();
        return;
    }

    // Baseline de tempo
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    
    // Gerar Device ID baseado no MAC
    uint64_t chipId = ESP.getEfuseMac();
    char cid[20];
    sprintf(cid, "%04X%08X", (uint16_t)(chipId >> 32), (uint32_t)chipId);
    _deviceId = _ra + "_" + String(cid).substring(8);
    
    // Configurar Tópicos
    _topicPub = "feagri/" + _ra + "/devices/" + _deviceId + "/telemetry";
    _topicSub = "feagri/" + _ra + "/devices/" + _deviceId + "/cmd";
    _topicFwStatus = "feagri/firmware/update/status";
    _topicRtcReq = "feagri/rtc/requisicao";
    _topicRtcResp = "feagri/rtc/resposta";

    connectWiFi();
    
    _mqttClient.setServer(_mqttBroker.c_str(), _mqttPort);
    _mqttClient.setCallback(IotFeagri::mqttCallback);
    
    connectMQTT();
}

void IotFeagri::begin(const char* mqtt_broker, int mqtt_port, const char* mqtt_user, const char* mqtt_pass) {
    _mqttBroker = mqtt_broker;
    _mqttPort = mqtt_port;
    _mqttUser = mqtt_user;
    _mqttPass = mqtt_pass;
    _ra = mqtt_user; // Assume RA as user in manual mode
    _wifiPass = ""; 

    // Pula o portal e força conexão manual
    Serial.begin(115200);
    
    uint64_t chipId = ESP.getEfuseMac();
    char cid[20];
    sprintf(cid, "%04X%08X", (uint16_t)(chipId >> 32), (uint32_t)chipId);
    _deviceId = _ra + "_" + String(cid).substring(8);
    
    _topicPub = "feagri/" + _ra + "/devices/" + _deviceId + "/telemetry";
    _topicSub = "feagri/" + _ra + "/devices/" + _deviceId + "/cmd";
    _topicFwStatus = "feagri/firmware/update/status";
    _topicRtcReq = "feagri/rtc/requisicao";
    _topicRtcResp = "feagri/rtc/resposta";

    connectWiFi();
    _mqttClient.setServer(_mqttBroker.c_str(), _mqttPort);
    _mqttClient.setCallback(IotFeagri::mqttCallback);
    connectMQTT();
}

void IotFeagri::connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;
    
    Serial.print("Conectando a WiFi IoT-local...");
    WiFi.mode(WIFI_STA);
    WiFi.begin("IoT-local", _wifiPass.c_str());
    
    unsigned long startTry = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTry < 20000) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nFalha no WiFi! Iniciando Portal...");
        startPortal();
    } else {
        Serial.println("\nWiFi Conectado!");
    }
}

bool IotFeagri::connectMQTT() {
    if (_mqttClient.connected()) return true;
    
    Serial.print("Connecting to MQTT...");
    if (_mqttClient.connect(_deviceId.c_str(), _mqttUser.c_str(), _mqttPass.c_str())) {
        Serial.println("OK");
        _mqttClient.subscribe(_topicSub.c_str());
        _mqttClient.subscribe(_topicRtcResp.c_str()); 
        publishFwStatus("boot", "online");
        requestTimeSync(); 
        return true;
    } else {
        Serial.print("Failed, rc=");
        Serial.println(_mqttClient.state());
        return false;
    }
}

void IotFeagri::loop() {
    if (_portalActive) {
        _dnsServer->processNextRequest();
        _portalServer->handleClient();
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }
    
    if (!_mqttClient.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > 5000) {
            _lastReconnectAttempt = now;
            if (connectMQTT()) {
                _lastReconnectAttempt = 0;
            }
        }
    } else {
        _mqttClient.loop();
        
        unsigned long now = millis();
        if (now - _lastHeartbeatTime >= 30000) { // Heartbeat a cada 30s
            _lastHeartbeatTime = now;
            sendHeartbeat();
        }
    }
}

void IotFeagri::mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (_instance) {
        _instance->handleMqttMessage(topic, payload, length);
    }
}

void IotFeagri::handleMqttMessage(char* topic, byte* payload, unsigned int length) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error) return;

    // Se for resposta do RTC
    if (String(topic) == _topicRtcResp) {
        long long unixMs = doc["timestamp"] | 0;
        if (unixMs > 0) {
            struct timeval tv;
            tv.tv_sec = unixMs / 1000ULL;
            tv.tv_usec = (unixMs % 1000ULL) * 1000ULL;
            settimeofday(&tv, NULL);
            _timeSynced = true;
            Serial.println(">> Hora sincronizada via RTC FEAGRI!");
        }
        return;
    }

    const char* type = doc["type"];
    const char* cmd = doc["command"];
    const char* target = doc["target_id"];

    // Security Check: Target ID match or "todos"
    if (target && String(target) != _deviceId && String(target) != "todos") {
        return;
    }

    if (String(type) == "COMMAND") {
        if (String(cmd) == "UPDATE") {
            performUpdate();
        } else if (_userCallback) {
            _userCallback(String(cmd), String(target), doc["data"].as<JsonObject>());
        }
    }
}

bool IotFeagri::publish(const char* grandeur, float value) {
    JsonDocument doc;
    doc["type"] = "MEASUREMENT";
    doc["client_id"] = _deviceId;
    JsonObject data = doc["data"].to<JsonObject>();
    data[grandeur] = value;

    String jsonStr;
    serializeJson(doc, jsonStr);
    return _mqttClient.publish(_topicPub.c_str(), jsonStr.c_str());
}

bool IotFeagri::publish(const char* grandeur, int value) {
    return publish(grandeur, (float)value);
}

bool IotFeagri::publish(const char* grandeur, String value) {
    JsonDocument doc;
    doc["type"] = "MEASUREMENT";
    doc["client_id"] = _deviceId;
    JsonObject data = doc["data"].to<JsonObject>();
    data[grandeur] = value;

    String jsonStr;
    serializeJson(doc, jsonStr);
    return _mqttClient.publish(_topicPub.c_str(), jsonStr.c_str());
}

void IotFeagri::setFirmwareVersion(const char* version) {
    _fwVersion = version;
}

void IotFeagri::onCommand(CommandCallback callback) {
    _userCallback = callback;
}

void IotFeagri::sendHeartbeat() {
    if (!_mqttClient.connected()) return;

    String hbTopic = String("feagri/") + _ra + "/devices/" + _deviceId + "/heartbeat";
    
    JsonDocument doc;
    doc["type"] = "heartbeat";
    doc["client_id"] = _deviceId;
    doc["owner"] = _ra;
    doc["fw_version"] = _fwVersion;
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["timestamp"] = getUnixTimeMs();
    doc["uptime_ms"] = (unsigned long long)millis();

    String payload;
    serializeJson(doc, payload);
    _mqttClient.publish(hbTopic.c_str(), payload.c_str());
}

void IotFeagri::requestTimeSync() {
    JsonDocument doc;
    doc["type"] = "COMMAND";
    doc["command"] = "get_time";
    doc["client_id"] = _deviceId;
    
    String out;
    serializeJson(doc, out);
    _mqttClient.publish(_topicRtcReq.c_str(), out.c_str());
}

uint64_t IotFeagri::getUnixTimeMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
}

void IotFeagri::publishFwStatus(const char* state, const char* message) {
    JsonDocument doc;
    doc["type"] = "FW_STATUS";
    doc["client_id"] = _deviceId;
    doc["version"] = _fwVersion;
    doc["state"] = state;
    if (strlen(message) > 0) doc["message"] = message;

    String jsonStr;
    serializeJson(doc, jsonStr);
    _mqttClient.publish(_topicFwStatus.c_str(), jsonStr.c_str());
}

void IotFeagri::performUpdate() {
    publishFwStatus("starting", "OTA Triggered via Dashboard");
    
    String host = _fwServer.length() > 0 ? _fwServer : _mqttBroker;
    String proto = _useTls ? "https://" : "http://";
    String baseUrl = proto + host;
    String manifestUrl = baseUrl + "/static/firmware/generic_esp32/manifest.json";

    HTTPClient http;
    http.begin(manifestUrl);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        deserializeJson(doc, payload);
        
        String defaultFwUrl = baseUrl + "/static/firmware/generic_esp32/firmware.bin";
        String fwUrl = doc["url"] | defaultFwUrl;
        String md5 = doc["md5"] | "";

        publishFwStatus("downloading", fwUrl.c_str());

        http.end();
        http.begin(fwUrl);
        httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            int len = http.getSize();
            if (Update.begin(len)) {
                if (md5.length() > 0) Update.setMD5(md5.c_str());
                
                size_t written = Update.writeStream(http.getStream());
                if (written == len) {
                    if (Update.end()) {
                        publishFwStatus("updated", "Success! Rebooting...");
                        delay(500);
                        ESP.restart();
                    } else {
                        publishFwStatus("error", Update.errorString());
                    }
                } else {
                    publishFwStatus("error", "Written size mismatch");
                }
            } else {
                publishFwStatus("error", "Update.begin failed");
            }
        } else {
            publishFwStatus("error", "HTTP GET Firmware failed");
        }
    } else {
        publishFwStatus("error", "HTTP GET Manifest failed");
    }
    http.end();
}

void IotFeagri::startPortal() {
    _portalActive = true;
    _dnsServer = new DNSServer();
    _portalServer = new WebServer(80);
    
    uint64_t chipId = ESP.getEfuseMac();
    char ssid[32];
    sprintf(ssid, "IOT_FEAGRI_%04X", (uint16_t)(chipId >> 32));
    
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssid);
    
    _dnsServer->start(53, "*", IPAddress(192, 168, 4, 1));
    
    _portalServer->on("/", [this]() { this->handleRoot(); });
    _portalServer->on("/save", [this]() { this->handleSave(); });
    _portalServer->onNotFound([this]() { this->handleRoot(); });
    
    _portalServer->begin();
    Serial.printf("Portal Ativo! Conecte em: %s (IP 192.168.4.1)\n", ssid);
}

void IotFeagri::handleRoot() {
    String p;
    p.reserve(3000);
    p += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    p += "<title>IoT FEAGRI - Config</title><style>";
    p += "body{font-family:sans-serif;background:#f4f4f9;color:#333;margin:20px}.card{max-width:400px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}";
    p += "h2{color:#005a9c;text-align:center}label{display:block;margin:10px 0 5px;font-weight:bold}";
    p += "input[type=text],input[type=password],input[type=number]{width:100%;padding:10px;margin-bottom:15px;border:1px solid #ccc;border-radius:4px;box-sizing:border-box}";
    p += "button{width:100%;padding:12px;background:#005a9c;color:#fff;border:none;border-radius:4px;cursor:pointer;font-size:16px}";
    p += "button:hover{background:#00467a}.section{border-bottom:2px solid #005a9c;margin-bottom:20px;padding-bottom:5px;font-size:18px;margin-top:20px}";
    p += "</style></head><body><div class='card'><h2>Configuração IoT FEAGRI</h2>";
    p += "<form method='POST' action='/save'>";
    
    p += "<div class='section'>Conexão WiFi</div>";
    p += "<label>Rede</label><input type='text' value='IoT-local' readonly>";
    p += "<label>Senha WiFi</label><input name='w_pass' type='password' placeholder='Digite a senha da IoT-local' value='" + _wifiPass + "'>";
    
    p += "<div class='section'>Credenciais MQTT</div>";
    p += "<label>Host Broker</label><input name='m_host' type='text' value='" + _mqttBroker + "'>";
    p += "<label>Porta</label><input name='m_port' type='number' value='" + String(_mqttPort) + "'>";
    p += "<label>Usuário (RA ou Login)</label><input name='m_user' type='text' placeholder='Ex: ra123456' value='" + _ra + "'>";
    p += "<label>Senha MQTT</label><input name='m_pass' type='password' value='" + _mqttPass + "'>";
    
    p += "<div class='section'>Servidor de Firmware</div>";
    p += "<label>URL/IP do Servidor</label><input name='fw_s' type='text' value='" + _fwServer + "'>";
    p += "<label style='display:flex;align-items:center;gap:10px;margin-bottom:20px'>";
    p += "<input type='checkbox' name='tls' " + String(_useTls ? "checked" : "") + "> Usar TLS (HTTPS / Porta 8883)</label>";
    
    p += "<button type='submit'>Salvar e Conectar</button></form></div></body></html>";
    _portalServer->send(200, "text/html", p);
}

void IotFeagri::handleSave() {
    _wifiPass = _portalServer->arg("w_pass");
    _mqttBroker = _portalServer->arg("m_host");
    _mqttPort = _portalServer->arg("m_port").toInt();
    _ra = _portalServer->arg("m_user");
    _mqttUser = _ra;
    _mqttPass = _portalServer->arg("m_pass");
    _fwServer = _portalServer->arg("fw_s");
    _useTls = _portalServer->hasArg("tls");
    
    saveConfig();
    
    _portalServer->send(200, "text/plain", "Configuracoes salvas! Reiniciando...");
    delay(2000);
    ESP.restart();
}
