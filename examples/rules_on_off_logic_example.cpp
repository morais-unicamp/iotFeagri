/*
 * Exemplo ON_OFF_LOGIC inspirado no contrato validado.
 *
 * RULE_CONFIG deve ser salvo pelo firmware. RULE_COMMAND apenas agenda
 * start/stop; a aplicacao do estado ocorre no tick local.
 */

#include <Arduino.h>
#include <iotFeagri.h>

static String clientId = "leandro_exemploESP32_20F540";
static String userMqtt = "leandro";
static String activeRuleId;
static int activeRevision = -1;
static String runtimeState = "empty";
static int startValue = 1;
static int stopValue = 0;
static bool pendingStart = false;
static bool pendingStop = false;

static void publishMqtt(const String& topic, const String& payload,
                        bool retained = false) {
  (void)topic;
  (void)payload;
  (void)retained;
}

static void applyOutputLater(int value) {
  (void)value;
  // Exemplo: salve o valor desejado e aplique no loop/task principal.
}

static void publishRuleEvent(const String& name, const String& message) {
  RuleEvent event;
  event.clientId = clientId;
  event.userMqtt = userMqtt;
  event.ruleId = activeRuleId;
  event.revision = activeRevision;
  event.event = name;
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
  status.running = runtimeState == "running";

  publishMqtt(IotFeagri::rulesStatusTopic(userMqtt, clientId),
              IotFeagri::buildRuleStatus(status), false);
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
  if (strcmp(doc["processor_type"] | "", "ON_OFF_LOGIC") != 0) {
    runtimeState = "error";
    publishRuleStatus();
    return;
  }

  activeRuleId = ruleId;
  activeRevision = revision;
  startValue = doc["start_action"]["value"] | 1;
  stopValue = doc["stop_action"]["value"] | 0;
  runtimeState = "loaded";
  publishRuleEvent("loaded", "rule loaded");
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

static void ruleTick() {
  if (pendingStart) {
    pendingStart = false;
    runtimeState = "running";
    applyOutputLater(startValue);
    publishRuleEvent("started", "start action scheduled");
    publishRuleEvent("executed", "start action executed");
    publishRuleStatus();
  }

  if (pendingStop) {
    pendingStop = false;
    runtimeState = "stopped";
    applyOutputLater(stopValue);
    publishRuleEvent("stopped", "stop action scheduled");
    publishRuleEvent("executed", "stop action executed");
    publishRuleStatus();
  }
}
