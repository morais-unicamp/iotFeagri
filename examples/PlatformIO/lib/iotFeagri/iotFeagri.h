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
#include <HTTPClient.h>
#include <Update.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

typedef void (*CommandCallback)(String command, String target, JsonObject data);

class IotFeagri {
public:
    IotFeagri(const char* ra_default = "");

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

    // Define a versão do firmware (importante para o OTA)
    void setFirmwareVersion(const char* version);
    
    // Define o callback para comandos vindos da dashboard
    void onCommand(CommandCallback callback);

    // Getters
    String getDeviceId() const { return _deviceId; }
    String getVersion() const { return _fwVersion; }

private:
    // Configurações
    String _ra;
    String _mqttBroker;
    int _mqttPort;
    String _mqttUser;
    String _mqttPass;
    String _wifiPass;
    String _fwServer;
    bool _useTls;
    
    String _deviceId;
    String _fwVersion;
    String _topicPub;
    String _topicSub;
    String _topicFwStatus;
    String _topicRtcReq;
    String _topicRtcResp;

    WiFiClient _espClient;
    PubSubClient _mqttClient;
    CommandCallback _userCallback;

    // Portal & DNS
    WebServer* _portalServer;
    DNSServer* _dnsServer;
    bool _portalActive;
    unsigned long _portalStartedAt;

    unsigned long _lastReconnectAttempt;
    unsigned long _lastMetricsTime;
    unsigned long _lastHeartbeatTime;
    bool _timeSynced;

    // Persistência
    void loadConfig();
    void saveConfig();

    // WiFi & Portal
    void connectWiFi();
    void startPortal();
    void handlePortal();
    void handleRoot();
    void handleSave();

    bool connectMQTT();
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
    void handleMqttMessage(char* topic, byte* payload, unsigned int length);
    
    // OTA Pull Logic
    void performUpdate();
    void publishFwStatus(const char* state, const char* message = "");

    // Heartbeat & Time Sync
    void sendHeartbeat();
    void requestTimeSync();
    uint64_t getUnixTimeMs();
};

#endif
