/*
 * iotFeagri.h - Biblioteca para simplificar o uso do ecossistema IoT FEAGRI
 * Desenvolvido para pesquisadores e alunos da Faculdade de Engenharia Agrícola - UNICAMP.
 */

#ifndef IOT_FEAGRI_H
#define IOT_FEAGRI_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

typedef void (*CommandCallback)(String command, String target, JsonObject data);

struct RuleValidationResult {
    bool ok;
    String error;
    String message;

    RuleValidationResult(bool valid = true, const String& err = "",
                         const String& msg = "")
        : ok(valid), error(err), message(msg) {}
};

struct RuleCommand {
    String requestId;
    String command;
    String clientId;
    String userMqtt;
    String ruleId;
    int revision;
    bool hasRevision;

    RuleCommand() : revision(0), hasRevision(false) {}
};

struct RuleAck {
    String requestId;
    String command;
    String clientId;
    String userMqtt;
    String ruleId;
    int revision;
    String processorType;
    String status;
    String message;
    bool stored;
    bool hasStored;
    String currentRuleId;
    int currentRevision;
    String currentProcessorType;

    RuleAck() : revision(-1), stored(false), hasStored(false), currentRevision(-1) {}
};

struct RuleStatus {
    String clientId;
    String userMqtt;
    String ruleId;
    int revision;
    String state;
    String message;
    bool running;

    RuleStatus() : revision(-1), running(false) {}
};

struct RuleEvent {
    String clientId;
    String userMqtt;
    String ruleId;
    int revision;
    String event;
    String message;

    RuleEvent() : revision(-1) {}
};

class IotFeagri {
public:
    IotFeagri(const char* user_default = "");

    // Inicializa carregando da NVS. Abre portal se falhar ou estiver vazio.
    void begin();

    // Inicializa forçando parâmetros (Legado/Manual)
    void begin(const char* mqtt_broker, int mqtt_port, const char* mqtt_user, const char* mqtt_pass);
    
    // Processa reconexões, MQTT, OTA e o Portal de Configuração
    void loop();

    // Publica uma medição simples
    bool publish(const char* grandeur, float value);
    bool publish(const char* grandeur, int value);
    bool publish(const char* grandeur, String value);
    bool publishStatus(const char* key, bool value);
    bool publishStatus(const char* key, const char* value);
    bool publishStatus(const char* key, String value);
    bool isOtaActive() const { return _otaActive; }

    // Define a versão do firmware (importante para o OTA)
    void setFirmwareVersion(const char* version);
    void setMqttCaCert(const char* caCert);
    void setFirmwareCaCert(const char* caCert);
    void setMqttAllowInsecure(bool allowInsecure);
    void setFirmwareAllowInsecure(bool allowInsecure);
    
    // Define o callback para comandos vindos da dashboard
    void onCommand(CommandCallback callback);

    // Getters
    String getDeviceId() const { return _deviceId; }
    String getVersion() const { return _fwVersion; }
    String getUserMqtt() const { return _userId; }
    String getDataVisibility() const { return _dataVisibility; }

    // Topicos padronizados RULE_CONFIG / RULE_COMMAND.
    static String deviceBaseTopic(const String& user, const String& clientId);
    static String rulesConfigTopic(const String& user, const String& clientId);
    static String rulesCommandTopic(const String& user, const String& clientId);
    static String rulesAckTopic(const String& user, const String& clientId);
    static String rulesStatusTopic(const String& user, const String& clientId);
    static String rulesRequestTopic(const String& user, const String& clientId);

    String mqttTopicRulesConfig() const;
    String mqttTopicRulesCommand() const;
    String mqttTopicRulesAck() const;
    String mqttTopicRulesStatus() const;
    String mqttTopicRulesRequest() const;

    // Validacao e builders comuns para firmwares com rule engine local.
    static RuleValidationResult validateClientId(const String& actual,
                                                 const String& expected);
    static RuleValidationResult validateUserMqtt(const String& actual,
                                                 const String& expected);
    static RuleValidationResult validateRevision(int actual, int expected);
    static RuleValidationResult validateRuleCommandJson(
        const String& payload, const String& expectedClientId,
        const String& expectedUserMqtt, RuleCommand& out,
        const String& activeRuleId = "", int activeRevision = -1);
    static RuleValidationResult validateCommonRuleConfigFields(
        const String& payload, const String& expectedClientId,
        const String& expectedUserMqtt, String& ruleId, int& revision);
    static String buildRuleAck(const RuleAck& ack);
    static String buildRuleStatus(const RuleStatus& status);
    static String buildRuleEvent(const RuleEvent& event);

private:
    String defaultProfile() const;
    String currentProfile() const;
    String currentGroup() const;
    String mqttTopicDeviceBase() const;
    String mqttTopicCmd() const;
    String mqttTopicGroupCmd() const;
    String mqttTopicStatus() const;
    String mqttTopicData() const;
    String mqttTopicPrivateData() const;
    String mqttTopicPublicData() const;
    String mqttTopicData(const String& sensorType, const String& serialOrId) const;
    String mqttTopicHeartbeat() const;
    String mqttTopicLegacyFirmwareCmd() const;
    String mqttTopicLegacyFirmwareStatus() const;
    String mqttTopicUserFirmwareCmd() const;
    String mqttTopicUserFirmwareStatus() const;

    // Configurações
    String _userId;
    String _profile;
    String _mqttBroker;
    int _mqttPort;
    String _mqttUser;
    String _mqttPass;
    String _wifiPass;
    String _fwServer;
    bool _useTls;
    String _webUser;
    String _webPass;
    String _webSessionId;
    unsigned long _webSessionUntil;
    
    String _deviceId;
    String _fwVersion;
    String _dataVisibility;
    String _topicRtcReq;
    String _topicRtcResp;

    WiFiClient _espClient;
    WiFiClientSecure _secureClient;
    PubSubClient _mqttClient;
    CommandCallback _userCallback;
    const char* _mqttCaCert;
    const char* _firmwareCaCert;
    bool _mqttAllowInsecure;
    bool _firmwareAllowInsecure;
    bool _otaActive;
    TaskHandle_t _otaTaskHandle;
    bool _otaTaskNeedsStagger;
    String _webOtaError;
    size_t _webOtaWritten;

    // Portal & DNS
    WebServer* _portalServer;
    DNSServer* _dnsServer;
    bool _portalActive;
    unsigned long _portalStartedAt;
    String _serialInput;
    bool _serialCliReady;

    unsigned long _lastReconnectAttempt;
    unsigned long _lastWiFiAttempt;
    unsigned long _wifiOfflineSince;
    unsigned long _lastMetricsTime;
    unsigned long _lastHeartbeatTime;
    bool _timeSynced;

    // Persistência
    void loadConfig();
    void saveConfig();
    void saveFirmwareVersion();
    void persistPendingFirmwareStatus(const String& version);
    bool publishPendingFirmwareStatus();
    void clearPendingFirmwareStatus();
    void setupIdentityAndTopics();
    bool publishCommandStatus(const char* command, const char* status,
                              const String& message = "");
    bool publishDataVisibilityAck(const char* status, const String& value,
                                  const String& message = "");
    bool saveDataVisibility();
    bool setDataVisibility(const String& visibility);
    void resetConfig();
    void handleSerialInput();
    void processSerialCommand(String cmd);

    // WiFi & Portal
    bool connectWiFi(bool allowPortalFallback = false);
    void startConfigServer();
    void startPortal();
    void handlePortal();
    void handleRoot();
    void handleSave();
    bool webAuthCheckAndReply();
    void handleLoginPage();
    void handleLoginSubmit();
    void handleLogout();
    void handleWebOtaUpload();
    void handleWebOtaDone();

    bool connectMQTT();
    void configureMqttTransport();
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
    void handleMqttMessage(char* topic, byte* payload, unsigned int length);
    
    // OTA Pull Logic
    bool queueFirmwareUpdate(bool commandNeedsStagger);
    static void otaTaskEntry(void* arg);
    void performUpdate(bool commandNeedsStagger = false);
    bool publishFwStatus(const char* state, const char* message = "");
    bool publishFwStatusDetail(const char* state, const String& message,
                               int progress = -1, int written = -1,
                               int total = -1, const String& url = "");
    bool downloadFirmwareWithRetry(const String& fwUrl, const String& md5,
                                   const String& manifestVersion,
                                   int maxAttempts);
    bool downloadFirmwareOnce(const String& fwUrl, const String& md5,
                              const String& manifestVersion, String& err);
    void waitOtaStagger(const char* reason);

    // Heartbeat & Time Sync
    void sendHeartbeat();
    void requestTimeSync();
    uint64_t getUnixTimeMs();
};

#endif
