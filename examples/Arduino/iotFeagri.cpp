#include "iotFeagri.h"

// Namespace e Chaves NVS
#define PREF_NAME "iot-feagri"
#define KEY_USER "user"
#define KEY_WIFI_PASS "w_pass"
#define KEY_MQTT_HOST "m_host"
#define KEY_MQTT_PORT "m_port"
#define KEY_MQTT_USER "m_user"
#define KEY_MQTT_PASS "m_pass"
#define KEY_FW_SERVER "fw_s"
#define KEY_TLS "tls"
#define KEY_PROFILE "profile"
#define KEY_FW_VERSION "fw_ver"

namespace {
String buildFirmwareBaseUrl(const String& host, bool useTls) {
    String trimmedHost = host;
    trimmedHost.trim();
    if (trimmedHost.startsWith("http://") || trimmedHost.startsWith("https://")) {
        return trimmedHost;
    }

    return String(useTls ? "https://" : "http://") + trimmedHost;
}

bool urlUsesTls(const String& url) {
    return url.startsWith("https://");
}

void configureSecureClient(WiFiClientSecure& client, const char* caCert, bool allowInsecure) {
    client.stop();
    if (caCert != nullptr && strlen(caCert) > 0) {
        client.setCACert(caCert);
        return;
    }
    if (allowInsecure) {
        client.setInsecure();
    }
}

bool beginHttpRequest(HTTPClient& http, const String& url, WiFiClient& tcp,
                      WiFiClientSecure& tls, const char* caCert,
                      bool allowInsecure) {
    if (urlUsesTls(url)) {
        configureSecureClient(tls, caCert, allowInsecure);
        return http.begin(tls, url);
    }
    return http.begin(tcp, url);
}

bool isGlobalTarget(const char* target) {
    return strcmp(target, "todos") == 0 || strcmp(target, "all") == 0;
}

bool matchesGroup(const char* group, const String& userId, const String& groupId) {
    if (group == nullptr || strlen(group) == 0) {
        return true;
    }

    return strcmp(group, "todos") == 0 || strcmp(group, "all") == 0 ||
           userId.equalsIgnoreCase(group) || groupId.equalsIgnoreCase(group);
}
}

IotFeagri* _instance = nullptr;

IotFeagri::IotFeagri(const char* user_default) : _userId(user_default), _mqttClient(_espClient) {
    _instance = this;
    _lastReconnectAttempt = 0;
    _lastWiFiAttempt = 0;
    _wifiOfflineSince = 0;
    _lastMetricsTime = 0;
    _lastHeartbeatTime = 0;
    _timeSynced = false;
    _mqttPort = 1883;
    _fwVersion = "v1.1.1";
    _portalActive = false;
    _portalServer = nullptr;
    _dnsServer = nullptr;
    _useTls = false;
    _profile = defaultProfile();
    _mqttCaCert = nullptr;
    _firmwareCaCert = nullptr;
    _mqttAllowInsecure = true;
    _firmwareAllowInsecure = true;
}

String IotFeagri::defaultProfile() const {
#if CONFIG_IDF_TARGET_ESP32C3
    return "exemploESP32C3";
#else
    return "exemploESP32";
#endif
}

String IotFeagri::currentProfile() const {
    String profile = _profile;
    profile.trim();
    if (profile.length() == 0) {
        profile = defaultProfile();
    }
    return profile;
}

String IotFeagri::currentGroup() const {
    String user = _userId;
    user.trim();
    String profile = currentProfile();
    if (profile.length() == 0) {
        return user;
    }
    return user + "_" + profile;
}

void IotFeagri::setupIdentityAndTopics() {
    uint64_t chipId = ESP.getEfuseMac();
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%06X",
             (unsigned int)(chipId & 0xFFFFFFULL));
    _deviceId = currentGroup() + "_" + String(suffix);

    _topicPub = "feagri/" + _userId + "/devices/" + _deviceId + "/data";
    _topicSub = "feagri/" + _userId + "/devices/" + _deviceId + "/cmd";
    _topicGroupCmd = "feagri/" + _userId + "/groups/" + currentGroup() + "/cmd";
    _topicStatus = "feagri/" + _userId + "/devices/" + _deviceId + "/status";
    _topicFwCmd = "feagri/firmware/update/cmd";
    _topicFwStatus = "feagri/firmware/update/status";
    _topicRtcReq = "feagri/rtc/requisicao";
    _topicRtcResp = "feagri/rtc/resposta";
}

void IotFeagri::loadConfig() {
    Preferences prefs;
    prefs.begin(PREF_NAME, true);
    _userId = prefs.getString(KEY_USER, _userId);
    _profile = prefs.getString(KEY_PROFILE, defaultProfile());
    _wifiPass = prefs.getString(KEY_WIFI_PASS, "");
    _mqttBroker = prefs.getString(KEY_MQTT_HOST, "leandro144.feagri.unicamp.br");
    _mqttPort = prefs.getInt(KEY_MQTT_PORT, 1883);
    _mqttUser = prefs.getString(KEY_MQTT_USER, "");
    _mqttPass = prefs.getString(KEY_MQTT_PASS, "");
    _fwServer = prefs.getString(KEY_FW_SERVER, "leandro144.feagri.unicamp.br");
    _useTls = prefs.getBool(KEY_TLS, false);
    _fwVersion = prefs.getString(KEY_FW_VERSION, _fwVersion);
    prefs.end();
}

void IotFeagri::saveConfig() {
    Preferences prefs;
    prefs.begin(PREF_NAME, false);
    prefs.putString(KEY_USER, _userId);
    prefs.putString(KEY_PROFILE, currentProfile());
    prefs.putString(KEY_WIFI_PASS, _wifiPass);
    prefs.putString(KEY_MQTT_HOST, _mqttBroker);
    prefs.putInt(KEY_MQTT_PORT, _mqttPort);
    prefs.putString(KEY_MQTT_USER, _mqttUser);
    prefs.putString(KEY_MQTT_PASS, _mqttPass);
    prefs.putString(KEY_FW_SERVER, _fwServer);
    prefs.putBool(KEY_TLS, _useTls);
    prefs.end();
}

void IotFeagri::saveFirmwareVersion() {
    Preferences prefs;
    prefs.begin(PREF_NAME, false);
    prefs.putString(KEY_FW_VERSION, _fwVersion);
    prefs.end();
}

void IotFeagri::begin() {
    Serial.begin(115200);
    delay(100);
    loadConfig();

    // Se nao tiver usuario ou senha WiFi salvos, entra em modo portal.
    if (_userId.length() == 0 || _wifiPass.length() == 0) {
        Serial.println(">> Configuracao incompleta. Iniciando Portal...");
        startPortal();
        return;
    }

    configTime(0, 0, "pool.ntp.org", "time.google.com");
    setupIdentityAndTopics();

    connectWiFi(true);

    configureMqttTransport();
    _mqttClient.setServer(_mqttBroker.c_str(), _mqttPort);
    _mqttClient.setBufferSize(4096);
    _mqttClient.setCallback(IotFeagri::mqttCallback);

    connectMQTT();
}

void IotFeagri::begin(const char* mqtt_broker, int mqtt_port, const char* mqtt_user, const char* mqtt_pass) {
    loadConfig();
    _mqttBroker = mqtt_broker;
    _mqttPort = mqtt_port;
    _mqttUser = mqtt_user;
    _mqttPass = mqtt_pass;
    if (_userId.length() == 0) {
        _userId = mqtt_user;
    }

    Serial.begin(115200);
    configTime(0, 0, "pool.ntp.org", "time.google.com");

    setupIdentityAndTopics();

    connectWiFi(true);
    configureMqttTransport();
    _mqttClient.setServer(_mqttBroker.c_str(), _mqttPort);
    _mqttClient.setBufferSize(4096);
    _mqttClient.setCallback(IotFeagri::mqttCallback);
    connectMQTT();
}

bool IotFeagri::connectWiFi(bool allowPortalFallback) {
    if (WiFi.status() == WL_CONNECTED) {
        _wifiOfflineSince = 0;
        return true;
    }

    const unsigned long now = millis();
    if (_wifiOfflineSince == 0) {
        _wifiOfflineSince = now;
    }

    if (WiFi.getMode() != WIFI_STA) {
        WiFi.mode(WIFI_STA);
    }

    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);

    if (_lastWiFiAttempt == 0 || now - _lastWiFiAttempt >= 10000UL ||
        WiFi.status() == WL_IDLE_STATUS || WiFi.status() == WL_DISCONNECTED) {
        Serial.print("Conectando a WiFi IoT-local...");
        WiFi.begin("IoT-local", _wifiPass.c_str());
        _lastWiFiAttempt = now;
    }

    const unsigned long waitWindowMs = allowPortalFallback ? 20000UL : 5000UL;
    const unsigned long startTry = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTry < waitWindowMs) {
        delay(250);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        _wifiOfflineSince = 0;
        Serial.println("\nWiFi Conectado!");
        return true;
    }

    Serial.println("\nFalha no WiFi.");
    if (allowPortalFallback || (millis() - _wifiOfflineSince >= 180000UL)) {
        if (!_portalActive) {
            Serial.println("Iniciando Portal...");
            startPortal();
        }
    }
    return false;
}

bool IotFeagri::connectMQTT() {
    if (_mqttClient.connected()) return true;

    Serial.print("Connecting to MQTT...");
    if (_mqttClient.connect(_deviceId.c_str(), _mqttUser.c_str(), _mqttPass.c_str())) {
        Serial.println("OK");
        _mqttClient.subscribe(_topicSub.c_str());
        _mqttClient.subscribe(_topicGroupCmd.c_str());
        _mqttClient.subscribe(_topicFwCmd.c_str());
        _mqttClient.subscribe(_topicRtcResp.c_str());
        publishFwStatus("boot", "online");
        sendHeartbeat();
        _lastHeartbeatTime = millis();
        requestTimeSync();
        return true;
    } else {
        Serial.print("Failed, rc=");
        Serial.println(_mqttClient.state());
        return false;
    }
}

void IotFeagri::configureMqttTransport() {
    const bool mqttUsesTls = _useTls || _mqttPort == 8883;
    if (mqttUsesTls) {
        configureSecureClient(_secureClient, _mqttCaCert, _mqttAllowInsecure);
        _mqttClient.setClient(_secureClient);
    } else {
        _mqttClient.setClient(_espClient);
    }
}

void IotFeagri::loop() {
    if (_portalActive) {
        _dnsServer->processNextRequest();
        _portalServer->handleClient();
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi(false);
        if (_portalActive) {
            return;
        }
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
        if (now - _lastHeartbeatTime >= 10000) {
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
    String topicStr(topic);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error) return;

    if (topicStr == _topicRtcResp) {
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
    const char* target = doc["target_id"] | doc["target"] | "";
    const bool isGroupTopic = topicStr == _topicGroupCmd;
    const String groupId = currentGroup();

    if (strlen(target) > 0 && String(target) != _deviceId && !isGlobalTarget(target) &&
        !(isGroupTopic && groupId.equalsIgnoreCase(target))) {
        return;
    }

    const char* group = doc["group"] | "";
    if ((isGlobalTarget(target) || isGroupTopic) && !matchesGroup(group, _userId, groupId)) {
        return;
    }

    if (String(type) == "COMMAND") {
        String command = cmd ? String(cmd) : "";
        if (command == "UPDATE" || command == "update_firmware" || command == "trigger_update") {
            performUpdate();
        } else if (_userCallback) {
            _userCallback(command, String(target), doc["data"].as<JsonObject>());
        }
    }
}

bool IotFeagri::publish(const char* grandeur, float value) {
    JsonDocument doc;
    doc["type"] = "MEASUREMENT";
    doc["client_id"] = _deviceId;
    doc["group"] = currentGroup();
    doc["owner"] = _userId;
    doc["profile"] = currentProfile();
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
    doc["group"] = currentGroup();
    doc["owner"] = _userId;
    doc["profile"] = currentProfile();
    JsonObject data = doc["data"].to<JsonObject>();
    data[grandeur] = value;

    String jsonStr;
    serializeJson(doc, jsonStr);
    return _mqttClient.publish(_topicPub.c_str(), jsonStr.c_str());
}

bool IotFeagri::publishStatus(const char* key, bool value) {
    return publishStatus(key, value ? "on" : "off");
}

bool IotFeagri::publishStatus(const char* key, const char* value) {
    return publishStatus(key, String(value));
}

bool IotFeagri::publishStatus(const char* key, String value) {
    JsonDocument doc;
    doc["type"] = "STATUS";
    doc["client_id"] = _deviceId;
    doc["group"] = currentGroup();
    doc["owner"] = _userId;
    doc["profile"] = currentProfile();
    JsonObject data = doc["data"].to<JsonObject>();
    data[key] = value;

    String jsonStr;
    serializeJson(doc, jsonStr);
    return _mqttClient.publish(_topicStatus.c_str(), jsonStr.c_str());
}

void IotFeagri::setFirmwareVersion(const char* version) {
    _fwVersion = version;
}

void IotFeagri::setMqttCaCert(const char* caCert) {
    _mqttCaCert = caCert;
}

void IotFeagri::setFirmwareCaCert(const char* caCert) {
    _firmwareCaCert = caCert;
}

void IotFeagri::setMqttAllowInsecure(bool allowInsecure) {
    _mqttAllowInsecure = allowInsecure;
}

void IotFeagri::setFirmwareAllowInsecure(bool allowInsecure) {
    _firmwareAllowInsecure = allowInsecure;
}

void IotFeagri::onCommand(CommandCallback callback) {
    _userCallback = callback;
}

void IotFeagri::sendHeartbeat() {
    if (!_mqttClient.connected()) return;

    String hbTopic = String("feagri/") + _userId + "/devices/" + _deviceId + "/heartbeat";

    JsonDocument doc;
    doc["type"] = "heartbeat";
    doc["client_id"] = _deviceId;
    doc["group"] = currentGroup();
    doc["owner"] = _userId;
    doc["profile"] = currentProfile();
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
    publishFwStatusDetail(state, String(message));
}

void IotFeagri::publishFwStatusDetail(const char* state, const String& message,
                                      int progress, int written, int total,
                                      const String& url) {
    JsonDocument doc;
    doc["type"] = "FW_STATUS";
    doc["client_id"] = _deviceId;
    doc["group"] = currentGroup();
    doc["owner"] = _userId;
    doc["profile"] = currentProfile();
    doc["version"] = _fwVersion;
    doc["state"] = state;
    doc["timestamp"] = getUnixTimeMs();
    if (message.length() > 0) doc["message"] = message;
    if (progress >= 0) doc["progress"] = progress;
    if (written >= 0) doc["written"] = written;
    if (total >= 0) doc["total"] = total;
    if (url.length() > 0) doc["url"] = url;

    String jsonStr;
    serializeJson(doc, jsonStr);
    _mqttClient.publish(_topicFwStatus.c_str(), jsonStr.c_str());
    _mqttClient.loop();
}

void IotFeagri::performUpdate() {
    publishFwStatus("starting", "OTA Triggered via Dashboard");

    String host = _fwServer.length() > 0 ? _fwServer : _mqttBroker;
    String baseUrl = buildFirmwareBaseUrl(host, _useTls);
#if CONFIG_IDF_TARGET_ESP32C3
    const char* fwChannel = "generic_esp32c3";
#else
    const char* fwChannel = "generic_esp32";
#endif

    String groupId = currentGroup();
    groupId.trim();
    String manifestUrl = baseUrl + "/static/firmware/" + groupId + "/manifest.json";
    String defaultFwUrl = baseUrl + "/static/firmware/" + groupId + "/firmware.bin";
    String channelManifestUrl = baseUrl + "/static/firmware/" + fwChannel + "/" + groupId + "/manifest.json";
    String channelFwUrl = baseUrl + "/static/firmware/" + fwChannel + "/" + groupId + "/" + groupId + ".bin";
    String legacyManifestUrl = baseUrl + "/static/firmware/" + fwChannel + "/manifest.json";
    String legacyFwUrl = baseUrl + "/static/firmware/" + fwChannel + "/firmware.bin";

    publishFwStatusDetail("manifest_url", manifestUrl, -1, -1, -1, manifestUrl);

    HTTPClient http;
    if (!beginHttpRequest(http, manifestUrl, _espClient, _secureClient,
                          _firmwareCaCert, _firmwareAllowInsecure)) {
        publishFwStatus("error", "HTTP begin manifest failed");
        return;
    }
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        http.end();
        manifestUrl = channelManifestUrl;
        defaultFwUrl = channelFwUrl;
        publishFwStatusDetail("manifest_url", manifestUrl, -1, -1, -1, manifestUrl);
        if (!beginHttpRequest(http, manifestUrl, _espClient, _secureClient,
                              _firmwareCaCert, _firmwareAllowInsecure)) {
            publishFwStatus("error", "HTTP begin channel manifest failed");
            return;
        }
        httpCode = http.GET();
    }

    if (httpCode != HTTP_CODE_OK) {
        http.end();
        manifestUrl = legacyManifestUrl;
        defaultFwUrl = legacyFwUrl;
        publishFwStatusDetail("manifest_url", manifestUrl, -1, -1, -1, manifestUrl);
        if (!beginHttpRequest(http, manifestUrl, _espClient, _secureClient,
                              _firmwareCaCert, _firmwareAllowInsecure)) {
            publishFwStatus("error", "HTTP begin legacy manifest failed");
            return;
        }
        httpCode = http.GET();
    }

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        deserializeJson(doc, payload);

        String fwUrl = doc["url"] | defaultFwUrl;
        String md5 = doc["md5"] | "";
        String manifestVersion = doc["version"] | "";

        manifestVersion.trim();
        if (manifestVersion.length() > 0 && manifestVersion == _fwVersion) {
            publishFwStatus("skipped", "Firmware version already installed");
            http.end();
            return;
        }

        String manifestMessage = "version=" + manifestVersion;
        if (md5.length() > 0) {
            manifestMessage += " md5=" + md5;
        }
        publishFwStatusDetail("starting", manifestMessage, -1, -1, -1, manifestUrl);
        publishFwStatusDetail("ota_url", fwUrl, 0, 0, -1, fwUrl);

        http.end();
        if (!beginHttpRequest(http, fwUrl, _espClient, _secureClient,
                              _firmwareCaCert, _firmwareAllowInsecure)) {
            publishFwStatus("error", "HTTP begin firmware failed");
            return;
        }
        httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            int len = http.getSize();
            publishFwStatusDetail("downloading", "HTTP 200", 0, 0, len, fwUrl);
            if (Update.begin(len)) {
                if (md5.length() > 0) Update.setMD5(md5.c_str());

                WiFiClient* stream = http.getStreamPtr();
                uint8_t buffer[1024];
                size_t written = 0;
                int lastProgress = -1;

                while (http.connected() && (len <= 0 || written < (size_t)len)) {
                    size_t available = stream->available();
                    if (available == 0) {
                        delay(5);
                        _mqttClient.loop();
                        continue;
                    }

                    size_t toRead = available;
                    if (toRead > sizeof(buffer)) {
                        toRead = sizeof(buffer);
                    }

                    int readLen = stream->readBytes(buffer, toRead);
                    if (readLen <= 0) {
                        continue;
                    }

                    size_t chunkWritten = Update.write(buffer, readLen);
                    if (chunkWritten != (size_t)readLen) {
                        publishFwStatus("error", "Update.write failed");
                        Update.abort();
                        http.end();
                        return;
                    }

                    written += chunkWritten;
                    if (len > 0) {
                        int progress = (int)((written * 100ULL) / (unsigned int)len);
                        if (progress >= 100 || progress == 0 || progress >= lastProgress + 10) {
                            lastProgress = progress;
                            publishFwStatusDetail("downloading", "writing firmware", progress,
                                                  (int)written, len, fwUrl);
                        }
                    }
                }

                if (written == len) {
                    if (Update.end()) {
                        if (manifestVersion.length() > 0) {
                            _fwVersion = manifestVersion;
                            saveFirmwareVersion();
                        }
                        publishFwStatusDetail("updated", "Success! Rebooting...", 100,
                                              (int)written, len, fwUrl);
                        _mqttClient.loop();
                        delay(1500);
                        ESP.restart();
                    } else {
                        publishFwStatus("error", Update.errorString());
                    }
                } else {
                    publishFwStatusDetail("error", "Written size mismatch", -1,
                                          (int)written, len, fwUrl);
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
    _portalStartedAt = millis();
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
    p += "</style></head><body><div class='card'><h2>Configuracao IoT FEAGRI</h2>";
    p += "<form method='POST' action='/save'>";

    p += "<div class='section'>Conexao WiFi</div>";
    p += "<label>Rede</label><input type='text' value='IoT-local' readonly>";
    p += "<label>Senha WiFi</label><input name='w_pass' type='password' placeholder='Digite a senha da IoT-local' value='" + _wifiPass + "'>";

    p += "<div class='section'>Acesso ao Broker</div>";
    p += "<label>Usuario</label><input name='m_user' type='text' placeholder='Ex: 123456' value='" + _userId + "'>";
    p += "<label>Perfil do projeto</label><input name='profile' type='text' placeholder='Ex: exemploESP32' value='" + currentProfile() + "'>";
    p += "<label>Senha MQTT</label><input name='m_pass' type='password' value='" + _mqttPass + "'>";

    p += "<div class='section'>Configuracao Avancada</div>";
    p += "<label>Host do broker</label><input name='m_host' type='text' value='" + _mqttBroker + "'>";
    p += "<label>Porta</label><input name='m_port' type='number' value='" + String(_mqttPort) + "'>";
    p += "<label>Servidor de firmware</label><input name='fw_s' type='text' value='" + _fwServer + "'>";
    p += "<label style='display:flex;align-items:center;gap:10px;margin-bottom:20px'>";
    p += "<input type='checkbox' name='tls' " + String(_useTls ? "checked" : "") + "> Usar TLS (HTTPS / Porta 8883)</label>";

    p += "<button type='submit'>Salvar e Conectar</button></form></div></body></html>";
    _portalServer->send(200, "text/html", p);
}

void IotFeagri::handleSave() {
    _wifiPass = _portalServer->arg("w_pass");
    _mqttBroker = _portalServer->arg("m_host");
    _mqttPort = _portalServer->arg("m_port").toInt();
    _userId = _portalServer->arg("m_user");
    _profile = _portalServer->arg("profile");
    _profile.trim();
    if (_profile.length() == 0) {
        _profile = defaultProfile();
    }
    _mqttUser = _userId;
    _mqttPass = _portalServer->arg("m_pass");
    _fwServer = _portalServer->arg("fw_s");
    _useTls = _portalServer->hasArg("tls");

    saveConfig();

    _portalServer->send(200, "text/plain", "Configuracoes salvas! Reiniciando...");
    delay(2000);
    ESP.restart();
}
