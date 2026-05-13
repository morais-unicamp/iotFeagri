#include "iotFeagri.h"
#include <esp_system.h>

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
#define KEY_FW_STATUS_STATE "fw_state"
#define KEY_FW_STATUS_MSG "fw_msg"

static const char GLOBALSIGN_ROOT_CA_R3[] = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDXzCCAkegAwIBAgILBAAAAAABIVhTCKIwDQYJKoZIhvcNAQELBQAwTDEgMB4G
A1UECxMXR2xvYmFsU2lnbiBSb290IENBIC0gUjMxEzARBgNVBAoTCkdsb2JhbFNp
Z24xEzARBgNVBAMTCkdsb2JhbFNpZ24wHhcNMDkwMzE4MTAwMDAwWhcNMjkwMzE4
MTAwMDAwWjBMMSAwHgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMzETMBEG
A1UEChMKR2xvYmFsU2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjCCASIwDQYJKoZI
hvcNAQEBBQADggEPADCCAQoCggEBAMwldpB5BngiFvXAg7aEyiie/QV2EcWtiHL8
RgJDx7KKnQRfJMsuS+FggkbhUqsMgUdwbN1k0ev1LKMPgj0MK66X17YUhhB5uzsT
gHeMCOFJ0mpiLx9e+pZo34knlTifBtc+ycsmWQ1z3rDI6SYOgxXG71uL0gRgykmm
KPZpO/bLyCiR5Z2KYVc3rHQU3HTgOu5yLy6c+9C7v/U9AOEGM+iCK65TpjoWc4zd
QQ4gOsC0p6Hpsk+QLjJg6VfLuQSSaGjlOCZgdbKfd/+RFO+uIEn8rUAVSNECMWEZ
XriX7613t2Saer9fwRPvm2L7DWzgVGkWqQPabumDk3F2xmmFghcCAwEAAaNCMEAw
DgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0OBBYEFI/wS3+o
LkUkrk1Q+mOai97i3Ru8MA0GCSqGSIb3DQEBCwUAA4IBAQBLQNvAUKr+yAzv95ZU
RUm7lgAJQayzE4aGKAczymvmdLm6AC2upArT9fHxD4q/c2dKg8dEe3jgr25sbwMp
jjM5RcOO5LlXbKr8EpbsU8Yt5CRsuZRj+9xTaGdWPoO4zzUhw8lo/s7awlOqzJCK
6fBdRoyV3XpYKBovHd7NADdBj+1EbddTKJd+82cEHhXXipa0095MJ6RMG3NzdvQX
mcIfeg7jLQitChws/zyrVQ4PkX4268NXSb7hLi18YIvDQVETI53O9zJrlAGomecs
Mx86OyXShkDOOyyGeMlhLxS67ttVb9+E7gUJTb0o2HLO02JQZR7rkpeDMdmztcpH
WD9f
-----END CERTIFICATE-----
)EOF";

namespace {
String buildFirmwareBaseUrl(const String& host, bool useTls) {
    String trimmedHost = host;
    trimmedHost.trim();
    if (trimmedHost.startsWith("http://") || trimmedHost.startsWith("https://")) {
        return trimmedHost;
    }

    String normalizedHost = trimmedHost;
    normalizedHost.toLowerCase();
    const bool forceHttps =
        normalizedHost == "leandro144.feagri.unicamp.br";

    return String((useTls || forceHttps) ? "https://" : "http://") +
           trimmedHost;
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

String lowerTrimmed(const String& value) {
    String out = value;
    out.trim();
    out.toLowerCase();
    return out;
}

String commandValue(JsonDocument& doc, const char* primary, const char* fallback = nullptr) {
    JsonVariant v = doc[primary];
    if (v.isNull() && fallback != nullptr) {
        v = doc[fallback];
    }
    if (v.isNull()) {
        JsonVariant data = doc["data"];
        if (!data.isNull()) {
            v = data[primary];
            if (v.isNull() && fallback != nullptr) {
                v = data[fallback];
            }
        }
    }
    if (v.is<const char*>()) {
        return String(v.as<const char*>());
    }
    if (v.is<String>()) {
        return v.as<String>();
    }
    if (v.is<int>() || v.is<long>() || v.is<float>()) {
        String out;
        serializeJson(v, out);
        return out;
    }
    return "";
}

bool matchesGroup(const char* group, const String& userId, const String& groupId) {
    if (group == nullptr || strlen(group) == 0) {
        return true;
    }

    return strcmp(group, "todos") == 0 || strcmp(group, "all") == 0 ||
           userId.equalsIgnoreCase(group) || groupId.equalsIgnoreCase(group);
}

void appendReadonlyTopic(String& page, const String& label, const String& topic) {
    page += "<label>";
    page += label;
    page += "</label><input type='text' readonly value='";
    page += topic;
    page += "'>";
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
    _firmwareCaCert = GLOBALSIGN_ROOT_CA_R3;
    _mqttAllowInsecure = true;
    _firmwareAllowInsecure = false;
    _otaActive = false;
    _webOtaWritten = 0;
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

String IotFeagri::mqttTopicDeviceBase() const {
    return "feagri/" + _userId + "/devices/" + _deviceId;
}

String IotFeagri::mqttTopicCmd() const {
    return mqttTopicDeviceBase() + "/cmd";
}

String IotFeagri::mqttTopicGroupCmd() const {
    return "feagri/" + _userId + "/groups/" + currentGroup() + "/cmd";
}

String IotFeagri::mqttTopicStatus() const {
    return mqttTopicDeviceBase() + "/status";
}

String IotFeagri::mqttTopicData() const {
    return mqttTopicDeviceBase() + "/data";
}

String IotFeagri::mqttTopicData(const String& sensorType, const String& serialOrId) const {
    String type = sensorType;
    type.trim();
    if (type.length() == 0) {
        type = "sensor";
    }

    String serial = serialOrId;
    serial.trim();
    if (serial.length() == 0) {
        serial = _deviceId;
    }

    return mqttTopicData() + "/" + type + "/" + serial;
}

String IotFeagri::mqttTopicHeartbeat() const {
    return mqttTopicDeviceBase() + "/heartbeat";
}

String IotFeagri::mqttTopicLegacyFirmwareCmd() const {
    return "feagri/firmware/update/cmd";
}

String IotFeagri::mqttTopicLegacyFirmwareStatus() const {
    return "feagri/firmware/update/status";
}

String IotFeagri::mqttTopicUserFirmwareCmd() const {
    return "feagri/" + _userId + "/firmware/update/cmd";
}

String IotFeagri::mqttTopicUserFirmwareStatus() const {
    return "feagri/" + _userId + "/firmware/update/status";
}

void IotFeagri::setupIdentityAndTopics() {
    uint64_t chipId = ESP.getEfuseMac();
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%06X",
             (unsigned int)(chipId & 0xFFFFFFULL));
    _deviceId = currentGroup() + "_" + String(suffix);

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
    _fwServer = prefs.getString(KEY_FW_SERVER, "");
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

void IotFeagri::persistPendingFirmwareStatus(const String& version) {
    String label = version;
    label.trim();
    if (!label.startsWith("v") && !label.startsWith("V")) {
        label = "v" + label;
    }

    Preferences prefs;
    prefs.begin(PREF_NAME, false);
    prefs.putString(KEY_FW_STATUS_STATE, "updated");
    prefs.putString(KEY_FW_STATUS_MSG, "updated (" + label + ") " + version);
    prefs.end();
}

bool IotFeagri::publishPendingFirmwareStatus() {
    Preferences prefs;
    prefs.begin(PREF_NAME, true);
    String state = prefs.getString(KEY_FW_STATUS_STATE, "");
    String message = prefs.getString(KEY_FW_STATUS_MSG, "");
    prefs.end();

    if (state.length() == 0) {
        return false;
    }
    if (publishFwStatus(state.c_str(), message.c_str())) {
        clearPendingFirmwareStatus();
        return true;
    }
    return false;
}

void IotFeagri::clearPendingFirmwareStatus() {
    Preferences prefs;
    prefs.begin(PREF_NAME, false);
    prefs.remove(KEY_FW_STATUS_STATE);
    prefs.remove(KEY_FW_STATUS_MSG);
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

    if (connectWiFi(true)) {
        startConfigServer();
    }

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

    if (connectWiFi(true)) {
        startConfigServer();
    }
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
        String cmdTopic = mqttTopicCmd();
        String groupCmdTopic = mqttTopicGroupCmd();
        String legacyFwCmdTopic = mqttTopicLegacyFirmwareCmd();
        String userFwCmdTopic = mqttTopicUserFirmwareCmd();
        _mqttClient.subscribe(cmdTopic.c_str());
        _mqttClient.subscribe(groupCmdTopic.c_str());
        _mqttClient.subscribe(legacyFwCmdTopic.c_str());
        _mqttClient.subscribe(userFwCmdTopic.c_str());
        _mqttClient.subscribe(_topicRtcResp.c_str());
        publishFwStatus("boot", "online");
        publishPendingFirmwareStatus();
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
    if (_portalServer != nullptr) {
        if (_portalActive && _dnsServer != nullptr) {
            _dnsServer->processNextRequest();
        }
        // Web OTA precisa continuar atendendo HTTP enquanto MQTT/heartbeat pausam.
        _portalServer->handleClient();
    }

    if (_otaActive) {
        delay(10);
        return;
    }

    if (_portalActive) {
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi(false);
        if (_portalActive) {
            return;
        }
    } else if (_portalServer == nullptr) {
        startConfigServer();
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
    const bool isGroupTopic = topicStr == mqttTopicGroupCmd();
    const String groupId = currentGroup();
    String targetText = target;
    String targetLower = lowerTrimmed(targetText);
    String clientLower = lowerTrimmed(_deviceId);
    String groupLower = lowerTrimmed(groupId);

    if (targetLower.length() > 0 && targetLower != clientLower &&
        !isGlobalTarget(targetLower.c_str()) && targetLower != groupLower) {
        return;
    }

    const char* group = doc["group"] | "";
    if ((isGlobalTarget(target) || isGroupTopic) && !matchesGroup(group, _userId, groupId)) {
        return;
    }

    if (String(type) == "COMMAND") {
        String command = cmd ? String(cmd) : "";
        String commandLower = lowerTrimmed(command);
        if (command == "UPDATE" || command == "update_firmware" || command == "trigger_update") {
            String groupCommand = lowerTrimmed(String(group));
            bool commandNeedsStagger =
                targetLower.length() == 0 || isGlobalTarget(targetLower.c_str()) ||
                targetLower == groupLower || groupCommand == "todos" ||
                groupCommand == "all" || groupCommand == groupLower ||
                (isGroupTopic && targetLower != clientLower);
            performUpdate(commandNeedsStagger);
        } else if (commandLower == "set_fw_host" ||
                   commandLower == "set_firmware_host") {
            String host = commandValue(doc, "value", "host");
            host.trim();
            if (host == "default") {
                host = "";
            }
            _fwServer = host;
            saveConfig();
            publishFwStatus("set_firmware_host", _fwServer.length() ? _fwServer.c_str() : "default");
        } else if (commandLower == "get_firmware_version" ||
                   commandLower == "get_fw_version") {
            publishFwStatus("firmware_version", _fwVersion.c_str());
        } else if (commandLower == "set_firmware_version" ||
                   commandLower == "set_fw_version") {
            String version = commandValue(doc, "value", "version");
            version.trim();
            if (version.length() > 0) {
                _fwVersion = version;
                saveFirmwareVersion();
                publishFwStatus("set_firmware_version", _fwVersion.c_str());
            } else {
                publishFwStatus("error", "Firmware version empty");
            }
        } else if (_userCallback) {
            _userCallback(command, String(target), doc["data"].as<JsonObject>());
        }
    }
}

bool IotFeagri::publish(const char* grandeur, float value) {
    if (_otaActive || !_mqttClient.connected()) return false;

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
    String topic = mqttTopicData(String(grandeur), _deviceId);
    Serial.printf("[MQTT PUB] %s\n", topic.c_str());
    return _mqttClient.publish(topic.c_str(), jsonStr.c_str());
}

bool IotFeagri::publish(const char* grandeur, int value) {
    return publish(grandeur, (float)value);
}

bool IotFeagri::publish(const char* grandeur, String value) {
    if (_otaActive || !_mqttClient.connected()) return false;

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
    String topic = mqttTopicData(String(grandeur), _deviceId);
    Serial.printf("[MQTT PUB] %s\n", topic.c_str());
    return _mqttClient.publish(topic.c_str(), jsonStr.c_str());
}

bool IotFeagri::publishStatus(const char* key, bool value) {
    return publishStatus(key, value ? "on" : "off");
}

bool IotFeagri::publishStatus(const char* key, const char* value) {
    return publishStatus(key, String(value));
}

bool IotFeagri::publishStatus(const char* key, String value) {
    if (_otaActive || !_mqttClient.connected()) return false;

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
    String topic = mqttTopicStatus();
    Serial.printf("[MQTT PUB] %s\n", topic.c_str());
    return _mqttClient.publish(topic.c_str(), jsonStr.c_str());
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

    String hbTopic = mqttTopicHeartbeat();

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
    Serial.printf("[MQTT PUB] %s\n", hbTopic.c_str());
    _mqttClient.publish(hbTopic.c_str(), payload.c_str());
}

void IotFeagri::requestTimeSync() {
    JsonDocument doc;
    doc["type"] = "COMMAND";
    doc["command"] = "get_time";
    doc["client_id"] = _deviceId;

    String out;
    serializeJson(doc, out);
    Serial.printf("[MQTT PUB] %s\n", _topicRtcReq.c_str());
    _mqttClient.publish(_topicRtcReq.c_str(), out.c_str());
}

uint64_t IotFeagri::getUnixTimeMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
}

bool IotFeagri::publishFwStatus(const char* state, const char* message) {
    return publishFwStatusDetail(state, String(message));
}

bool IotFeagri::publishFwStatusDetail(const char* state, const String& message,
                                      int progress, int written, int total,
                                      const String& url) {
    if (!_mqttClient.connected()) return false;

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
    String legacyTopic = mqttTopicLegacyFirmwareStatus();
    String userTopic = mqttTopicUserFirmwareStatus();
    Serial.printf("[MQTT PUB] %s\n", legacyTopic.c_str());
    bool legacyOk = _mqttClient.publish(legacyTopic.c_str(), jsonStr.c_str());
    Serial.printf("[MQTT PUB] %s\n", userTopic.c_str());
    bool userOk = _mqttClient.publish(userTopic.c_str(), jsonStr.c_str());
    _mqttClient.loop();
    return legacyOk || userOk;
}

void IotFeagri::waitOtaStagger(const char* reason) {
    uint64_t mac = ESP.getEfuseMac();
    uint32_t macSalt = (uint32_t)(mac ^ (mac >> 32));
    uint32_t waitMs = (esp_random() ^ macSalt) % 300001UL;
    String msg = "Aguardando janela OTA";
    if (reason && strlen(reason) > 0) {
        msg += " ";
        msg += reason;
    }
    msg += ": ";
    msg += String(waitMs);
    msg += " ms";
    publishFwStatus("stagger", msg.c_str());
    Serial.println(msg);

    unsigned long start = millis();
    while ((unsigned long)(millis() - start) < waitMs) {
        if (_mqttClient.connected()) {
            _mqttClient.loop();
        }
        delay(250);
    }
}

bool IotFeagri::downloadFirmwareOnce(const String& fwUrl, const String& md5,
                                     const String& manifestVersion, String& err) {
    WiFiClient tcp;
    WiFiClientSecure tls;
    HTTPClient http;
    http.setConnectTimeout(6000);
    http.setTimeout(45000);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setReuse(false);

    if (!beginHttpRequest(http, fwUrl, tcp, tls, _firmwareCaCert,
                          _firmwareAllowInsecure)) {
        err = "HTTP begin firmware failed";
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        err = "HTTP GET firmware failed: " + String(httpCode);
        http.end();
        return false;
    }

    int len = http.getSize();
    if (len <= 0) {
        err = "Content-Length invalido";
        http.end();
        return false;
    }

    if (Update.isRunning()) {
        Update.abort();
    }
    if (md5.length() > 0 && !Update.setMD5(md5.c_str())) {
        err = "MD5 esperado invalido";
        http.end();
        return false;
    }
    if (!Update.begin((size_t)len, U_FLASH)) {
        String firstErr = Update.errorString();
        Serial.printf("Update.begin(%d) failed: %s. Retrying with UPDATE_SIZE_UNKNOWN.\n",
                      len, firstErr.c_str());
        publishFwStatusDetail("begin_retry",
                              "Update.begin(size) failed: " + firstErr,
                              -1, 0, len, fwUrl);
        Update.abort();
        if (md5.length() > 0 && !Update.setMD5(md5.c_str())) {
            err = "MD5 esperado invalido apos retry";
            http.end();
            return false;
        }
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            err = "Update.begin fallback failed: " + String(Update.errorString());
            http.end();
            return false;
        }
    }

    WiFiClient* stream = http.getStreamPtr();
    stream->setTimeout(45);
    uint8_t buffer[1024];
    size_t written = 0;
    unsigned long lastDataMs = millis();
    size_t lastLog = 0;

    Serial.printf("OTA download: %s (%d bytes)\n", fwUrl.c_str(), len);
    while (written < (size_t)len && (http.connected() || stream->available())) {
        size_t available = stream->available();
        if (available == 0) {
            delay(5);
            if ((unsigned long)(millis() - lastDataMs) > 30000UL) {
                err = "Timeout durante download";
                break;
            }
            continue;
        }

        lastDataMs = millis();
        size_t toRead = available > sizeof(buffer) ? sizeof(buffer) : available;
        size_t remaining = (size_t)len - written;
        if (toRead > remaining) {
            toRead = remaining;
        }

        int readLen = stream->read(buffer, toRead);
        if (readLen <= 0) {
            delay(5);
            continue;
        }

        size_t chunkWritten = Update.write(buffer, readLen);
        if (chunkWritten != (size_t)readLen) {
            err = "Update.write failed: " + String(Update.errorString());
            break;
        }

        written += chunkWritten;
        if (written == (size_t)len || written - lastLog >= 65536U) {
            lastLog = written;
            Serial.printf("OTA progress: %u / %u\n", (unsigned)written,
                          (unsigned)len);
        }
        yield();
    }

    if (written != (size_t)len) {
        if (err.length() == 0) {
            err = "Download incompleto (" + String(written) + "/" + String(len) + ")";
        }
        Update.abort();
        http.end();
        return false;
    }

    if (!Update.end()) {
        err = "Update.end failed: " + String(Update.errorString());
        http.end();
        return false;
    }

    http.end();
    String versionToPersist = manifestVersion;
    versionToPersist.trim();
    if (versionToPersist.length() > 0) {
        _fwVersion = versionToPersist;
        saveFirmwareVersion();
    } else {
        versionToPersist = _fwVersion;
    }
    persistPendingFirmwareStatus(versionToPersist);
    return true;
}

bool IotFeagri::downloadFirmwareWithRetry(const String& fwUrl, const String& md5,
                                          const String& manifestVersion,
                                          int maxAttempts) {
    String err;
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        Serial.printf("OTA tentativa %d/%d\n", attempt, maxAttempts);
        err = "";
        if (downloadFirmwareOnce(fwUrl, md5, manifestVersion, err)) {
            return true;
        }
        Serial.printf("OTA tentativa %d falhou: %s\n", attempt, err.c_str());
        if (Update.isRunning()) {
            Update.abort();
        }
        if (attempt < maxAttempts) {
            delay(2000);
        }
    }
    publishFwStatus("error", err.length() ? err.c_str() : "OTA failed");
    return false;
}

void IotFeagri::performUpdate(bool commandNeedsStagger) {
    if (_otaActive) {
        publishFwStatus("error", "OTA ja em andamento");
        return;
    }

    if (commandNeedsStagger) {
        waitOtaStagger("coletivo");
    }

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

    const String manifestCandidates[] = {manifestUrl, channelManifestUrl, legacyManifestUrl};
    const String fwCandidates[] = {defaultFwUrl, channelFwUrl, legacyFwUrl};
    String payload;
    int selected = -1;
    String lastErr;

    for (int i = 0; i < 3 && selected < 0; ++i) {
        publishFwStatusDetail("manifest_url", manifestCandidates[i], -1, -1, -1,
                              manifestCandidates[i]);
        for (int attempt = 1; attempt <= 3; ++attempt) {
            WiFiClient tcp;
            WiFiClientSecure tls;
            HTTPClient http;
            http.setConnectTimeout(8000);
            http.setTimeout(15000);
            http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
            http.setReuse(false);

            if (!beginHttpRequest(http, manifestCandidates[i], tcp, tls,
                                  _firmwareCaCert, _firmwareAllowInsecure)) {
                lastErr = "HTTP begin manifest failed";
                delay(500);
                continue;
            }
            int httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK) {
                payload = http.getString();
                selected = i;
                http.end();
                break;
            }
            lastErr = "HTTP GET manifest failed: " + String(httpCode);
            http.end();
            delay(700);
        }
    }

    if (selected < 0) {
        publishFwStatus("error", lastErr.length() ? lastErr.c_str() : "HTTP GET Manifest failed");
        return;
    }

    JsonDocument doc;
    DeserializationError jsonErr = deserializeJson(doc, payload);
    if (jsonErr) {
        publishFwStatus("error", "JSON manifest invalido");
        return;
    }

    String fwUrl = doc["url"] | fwCandidates[selected];
    String md5 = doc["md5"] | "";
    String manifestVersion = doc["version"] | "";
    manifestVersion.trim();

    if (manifestVersion.length() > 0 && manifestVersion == _fwVersion) {
        publishFwStatus("skipped", "Firmware version already installed");
        return;
    }

    String manifestMessage = "version=" + manifestVersion;
    if (md5.length() > 0) {
        manifestMessage += " md5=" + md5;
    }
    publishFwStatusDetail("starting", manifestMessage, -1, -1, -1,
                          manifestCandidates[selected]);
    publishFwStatusDetail("ota_url", fwUrl, 0, 0, -1, fwUrl);

    _otaActive = true;
    if (_mqttClient.connected()) {
        _mqttClient.disconnect();
    }
    delay(250);

    bool ok = downloadFirmwareWithRetry(fwUrl, md5, manifestVersion, 3);
    _otaActive = false;
    if (ok) {
        delay(200);
        ESP.restart();
    }
}

void IotFeagri::startPortal() {
    _portalActive = true;
    _portalStartedAt = millis();

    uint64_t chipId = ESP.getEfuseMac();
    char ssid[32];
    sprintf(ssid, "IOT_FEAGRI_%04X", (uint16_t)(chipId >> 32));

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssid);

    if (_dnsServer == nullptr) {
        _dnsServer = new DNSServer();
    }
    _dnsServer->start(53, "*", IPAddress(192, 168, 4, 1));

    startConfigServer();
    Serial.printf("Portal Ativo! Conecte em: %s (IP 192.168.4.1)\n", ssid);
}

void IotFeagri::startConfigServer() {
    if (_portalServer != nullptr) {
        return;
    }

    _portalServer = new WebServer(80);
    _portalServer->on("/", [this]() { this->handleRoot(); });
    _portalServer->on("/config", [this]() { this->handleRoot(); });
    _portalServer->on("/save", [this]() { this->handleSave(); });
    _portalServer->on("/update", HTTP_POST,
        [this]() { this->handleWebOtaDone(); },
        [this]() { this->handleWebOtaUpload(); });
    _portalServer->onNotFound([this]() { this->handleRoot(); });

    _portalServer->begin();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("Config web ativo em http://");
        Serial.println(WiFi.localIP());
    }
}

void IotFeagri::handleRoot() {
    String p;
    p.reserve(3000);
    p += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    p += "<title>IoT FEAGRI - Config</title><style>";
    p += "body{font-family:sans-serif;background:#f4f4f9;color:#333;margin:20px}.card{max-width:400px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}";
    p += "h2{color:#005a9c;text-align:center}label{display:block;margin:10px 0 5px;font-weight:bold}";
    p += "input[type=text],input[type=password],input[type=number],input[type=file]{width:100%;padding:10px;margin-bottom:15px;border:1px solid #ccc;border-radius:4px;box-sizing:border-box}";
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

    p += "<div class='section'>Topicos MQTT automaticos</div>";
    appendReadonlyTopic(p, "Base do dispositivo", mqttTopicDeviceBase());
    appendReadonlyTopic(p, "Comandos do dispositivo", mqttTopicCmd());
    appendReadonlyTopic(p, "Comandos do grupo", mqttTopicGroupCmd());
    appendReadonlyTopic(p, "Dados de sensores", mqttTopicData("temperature", _deviceId));
    appendReadonlyTopic(p, "Status/ACK", mqttTopicStatus());
    appendReadonlyTopic(p, "Heartbeat", mqttTopicHeartbeat());
    appendReadonlyTopic(p, "OTA comando por usuario", mqttTopicUserFirmwareCmd());
    appendReadonlyTopic(p, "OTA status por usuario", mqttTopicUserFirmwareStatus());
    appendReadonlyTopic(p, "OTA comando legado", mqttTopicLegacyFirmwareCmd());
    appendReadonlyTopic(p, "OTA status legado", mqttTopicLegacyFirmwareStatus());

    p += "<button type='submit'>Salvar e Conectar</button></form>";
    p += "<div class='section'>Atualizacao de firmware</div>";
    p += "<form method='POST' action='/update' enctype='multipart/form-data'>";
    p += "<label>Arquivo .bin</label><input type='file' name='update' accept='.bin,application/octet-stream'>";
    p += "<button type='submit'>Atualizar firmware</button></form>";
    p += "</div></body></html>";
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

void IotFeagri::handleWebOtaUpload() {
    HTTPUpload& upload = _portalServer->upload();

    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("[WEB_OTA] Inicio: %s\n", upload.filename.c_str());
        _webOtaError = "";
        _webOtaWritten = 0;
        _otaActive = true;
        if (_mqttClient.connected()) {
            _mqttClient.disconnect();
        }
        if (Update.isRunning()) {
            Update.abort();
        }
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            _webOtaError = "Update.begin falhou: " + String(Update.errorString());
            Serial.println("[WEB_OTA] " + _webOtaError);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (_webOtaError.length() > 0) {
            return;
        }
        size_t written = Update.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
            _webOtaError = "Update.write falhou: " + String(Update.errorString());
            Serial.println("[WEB_OTA] " + _webOtaError);
            return;
        }
        _webOtaWritten += written;
    } else if (upload.status == UPLOAD_FILE_END) {
        if (_webOtaError.length() == 0 && _webOtaWritten != upload.totalSize) {
            _webOtaError = "Upload incompleto (" + String(_webOtaWritten) +
                           "/" + String(upload.totalSize) + ")";
        }
        if (_webOtaError.length() == 0 && !Update.end(true)) {
            _webOtaError = "Update.end falhou: " + String(Update.errorString());
        }
        if (_webOtaError.length() > 0) {
            Update.abort();
            Serial.println("[WEB_OTA] " + _webOtaError);
        } else {
            Serial.printf("[WEB_OTA] Concluido: %u bytes\n",
                          (unsigned)_webOtaWritten);
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        _webOtaError = "Upload abortado";
        Update.abort();
        _otaActive = false;
        Serial.println("[WEB_OTA] Upload abortado");
    }
}

void IotFeagri::handleWebOtaDone() {
    if (_webOtaWritten == 0 && _webOtaError.length() == 0) {
        _webOtaError = "Nenhum arquivo recebido";
    }
    bool ok = _webOtaError.length() == 0 && !Update.hasError();
    String body = ok ? "OTA OK. Reiniciando..." : "OTA falhou: " + _webOtaError;
    _portalServer->send(ok ? 200 : 500, "text/plain", body);
    _otaActive = false;
    if (ok) {
        delay(500);
        ESP.restart();
    }
}
