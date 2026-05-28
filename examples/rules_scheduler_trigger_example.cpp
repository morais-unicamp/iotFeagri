/*
 * Exemplo scheduler/gatilho inspirado no rtc_feagri.
 *
 * RULE_COMMAND start habilita a agenda local. Quando o gatilho vence,
 * o publish para o alvo e feito no tick, fora do callback MQTT.
 */

#include <Arduino.h>
#include <iotFeagri.h>

static String clientId = "leandro_rtc_feagri_20F540";
static String userMqtt = "leandro";
static String activeRuleId;
static int activeRevision = -1;
static String runtimeState = "empty";
static String targetClientId = "leandro_espScan_A1B2C3";
static String targetCommand = "scan";
static unsigned long intervalMs = 60000;
static unsigned long nextTriggerAt = 0;
static bool schedulerRunning = false;
static bool pendingStart = false;
static bool pendingStop = false;

static void publishMqtt(const String& topic, const String& payload,
                        bool retained = false) {
  (void)topic;
  (void)payload;
  (void)retained;
}

static void publishRuleEvent(const String& eventName, const String& message) {
  RuleEvent event;
  event.clientId = clientId;
  event.userMqtt = userMqtt;
  event.ruleId = activeRuleId;
  event.revision = activeRevision;
  event.event = eventName;
  event.message = message;

  publishMqtt(IotFeagri::rulesStatusTopic(userMqtt, clientId),
              IotFeagri::buildRuleEvent(event), false);
}

static void publishRuleStatus() {
  RuleStatus status;
  status.clientId = clientId;
  status.userMqtt = userMqtt;
  status.ruleId = activeRuleId;
  status.revision = activeRevision;
  status.state = runtimeState;
  status.running = schedulerRunning;

  publishMqtt(IotFeagri::rulesStatusTopic(userMqtt, clientId),
              IotFeagri::buildRuleStatus(status), false);
}

static void publishLegacyCommandToTarget() {
  JsonDocument doc;
  doc["type"] = "COMMAND";
  doc["command"] = targetCommand;
  doc["target_id"] = targetClientId;
  doc["owner"] = userMqtt;

  String payload;
  serializeJson(doc, payload);
  publishMqtt(IotFeagri::deviceBaseTopic(userMqtt, targetClientId) + "/cmd",
              payload, false);
  publishRuleEvent("executed", "legacy COMMAND published to target");
}

static void handleRulesConfig(const String& payload) {
  String ruleId;
  int revision = -1;
  RuleValidationResult result = IotFeagri::validateCommonRuleConfigFields(
      payload, clientId, userMqtt, ruleId, revision);
  if (!result.ok) {
    runtimeState = "error";
    publishRuleStatus();
    return;
  }

  JsonDocument doc;
  deserializeJson(doc, payload);
  activeRuleId = ruleId;
  activeRevision = revision;
  intervalMs = doc["schedule"]["interval_ms"] | intervalMs;
  targetClientId = doc["target"]["client_id"] | targetClientId;
  targetCommand = doc["target"]["command"] | targetCommand;
  runtimeState = "loaded";
  schedulerRunning = false;
  publishRuleEvent("loaded", "schedule loaded");
  publishRuleStatus();
}

static void handleRulesCommand(const String& payload) {
  RuleCommand cmd;
  RuleValidationResult result = IotFeagri::validateRuleCommandJson(
      payload, clientId, userMqtt, cmd, activeRuleId, activeRevision);

  RuleAck ack;
  ack.clientId = clientId;
  ack.userMqtt = userMqtt;
  ack.command = cmd.command;
  ack.requestId = cmd.requestId;
  ack.ruleId = activeRuleId;
  ack.revision = activeRevision;
  ack.status = result.ok ? "accepted" : "error";
  ack.message = result.ok ? "RULE_COMMAND accepted" : result.error;
  publishMqtt(IotFeagri::rulesAckTopic(userMqtt, clientId),
              IotFeagri::buildRuleAck(ack), false);

  if (!result.ok) return;
  if (cmd.command == "start") pendingStart = true;
  if (cmd.command == "stop") pendingStop = true;
  if (cmd.command == "status") publishRuleStatus();
}

static void schedulerTick() {
  if (pendingStart) {
    pendingStart = false;
    schedulerRunning = true;
    runtimeState = "running";
    nextTriggerAt = millis() + intervalMs;
    publishRuleEvent("started", "schedule started");
    publishRuleStatus();
  }

  if (pendingStop) {
    pendingStop = false;
    schedulerRunning = false;
    runtimeState = "stopped";
    publishRuleEvent("stopped", "schedule stopped");
    publishRuleStatus();
  }

  if (schedulerRunning && millis() >= nextTriggerAt) {
    nextTriggerAt = millis() + intervalMs;
    publishLegacyCommandToTarget();
  }
}
