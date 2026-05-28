/*
 * Exemplo minimo de compatibilidade RULE_COMMAND status.
 *
 * Este arquivo mostra apenas a parte da rule engine. Integre as funcoes abaixo
 * ao callback MQTT e ao publish MQTT do seu projeto.
 */

#include <Arduino.h>
#include <iotFeagri.h>

static String clientId = "leandro_exemploESP32_20F540";
static String userMqtt = "leandro";
static String ruleState = "empty";

static void publishMqtt(const String& topic, const String& payload,
                        bool retained = false) {
  (void)topic;
  (void)payload;
  (void)retained;
  // Exemplo: mqttClient.publish(topic.c_str(), payload.c_str(), retained);
}

static void publishRuleStatus(const String& message = "") {
  RuleStatus status;
  status.clientId = clientId;
  status.userMqtt = userMqtt;
  status.state = ruleState;
  status.message = message;
  status.running = false;

  publishMqtt(IotFeagri::rulesStatusTopic(userMqtt, clientId),
              IotFeagri::buildRuleStatus(status), false);
}

static void handleRulesCommand(const String& payload) {
  RuleCommand cmd;
  RuleValidationResult result =
      IotFeagri::validateRuleCommandJson(payload, clientId, userMqtt, cmd);

  RuleAck ack;
  ack.clientId = clientId;
  ack.userMqtt = userMqtt;
  ack.command = cmd.command;
  ack.requestId = cmd.requestId;
  ack.status = result.ok ? "accepted" : "error";
  ack.message = result.ok ? "RULE_COMMAND accepted" : result.error;

  publishMqtt(IotFeagri::rulesAckTopic(userMqtt, clientId),
              IotFeagri::buildRuleAck(ack), false);

  if (!result.ok || cmd.command != "status") {
    return;
  }

  publishRuleStatus(ruleState == "empty" ? "no rule loaded" : "rule loaded");
}
