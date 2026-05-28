/*
 * Exemplo start/stop sem acao fisica.
 *
 * O callback MQTT valida e agenda. A mudanca de runtime acontece em ruleTick(),
 * chamado no loop principal do firmware.
 */

#include <Arduino.h>
#include <iotFeagri.h>

static String clientId = "leandro_exemploESP32_20F540";
static String userMqtt = "leandro";
static String activeRuleId = "demo-rule";
static int activeRevision = 1;
static String runtimeState = "loaded";
static bool pendingStart = false;
static bool pendingStop = false;

static void publishMqtt(const String& topic, const String& payload,
                        bool retained = false) {
  (void)topic;
  (void)payload;
  (void)retained;
}

static void publishEvent(const String& eventName, const String& message) {
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

static void publishStatus() {
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

static void handleRulesCommand(const String& payload) {
  RuleCommand cmd;
  RuleValidationResult result = IotFeagri::validateRuleCommandJson(
      payload, clientId, userMqtt, cmd, activeRuleId, activeRevision);

  RuleAck ack;
  ack.clientId = clientId;
  ack.userMqtt = userMqtt;
  ack.command = cmd.command;
  ack.requestId = cmd.requestId;
  ack.ruleId = cmd.ruleId;
  ack.revision = cmd.hasRevision ? cmd.revision : -1;
  ack.status = result.ok ? "accepted" : "error";
  ack.message = result.ok ? "RULE_COMMAND accepted" : result.error;

  publishMqtt(IotFeagri::rulesAckTopic(userMqtt, clientId),
              IotFeagri::buildRuleAck(ack), false);

  if (!result.ok) {
    return;
  }

  if (cmd.command == "start") {
    pendingStart = true;
  } else if (cmd.command == "stop") {
    pendingStop = true;
  } else if (cmd.command == "status") {
    publishStatus();
  }
}

static void ruleTick() {
  if (pendingStart) {
    pendingStart = false;
    pendingStop = false;
    runtimeState = "running";
    publishEvent("started", "runtime started");
    publishStatus();
  }

  if (pendingStop) {
    pendingStop = false;
    runtimeState = "stopped";
    publishEvent("stopped", "runtime stopped");
    publishStatus();
  }
}
