# iotFeagri Kit - ESP32 🚀

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![FEAGRI](https://img.shields.io/badge/FEAGRI-UNICAMP-blue.svg)](https://www.feagri.unicamp.br/)

Biblioteca e Kit de Desenvolvimento para simplificar o provisionamento e telemetria de sensores no ecossistema **IoT FEAGRI - UNICAMP**.

Cada dispositivo provisionado pela biblioteca publica:
- `owner`: usuário MQTT salvo no portal
- `profile`: perfil do projeto salvo no portal
- `group`: composição `owner_profile`

Por padrão, a biblioteca usa:
- `exemploESP32` para placas ESP32
- `exemploESP32C3` para placas ESP32-C3

O usuário pode alterar o `profile` no portal para separar projetos diferentes sem misturar firmwares na dashboard.

## 🌟 Principais Recursos
- **Provisionamento via Portal**: Configure Rede WiFi e Credenciais MQTT via celular (Captive Portal em `192.168.4.1`).
- **Portal no IP da placa**: depois de conectada ao WiFi, a placa mantem a pagina de configuracao em `http://<IP_DA_PLACA>/` e `http://<IP_DA_PLACA>/config`.
- **Persistência NVS**: Credenciais são salvas permanentemente na memória do ESP32.
- **Auto-OTA**: Atualização de firmware automática via Dashboard.
- **Web OTA local**: upload manual de `.bin` pela propria pagina de configuracao da placa.
- **Suporte Multi-IDE**: Templates prontos para **Arduino IDE** e **PlatformIO (VS Code)**.
- **Sincronização de Tempo**: Relógio interno (RTC) sincronizado automaticamente com os servidores da FEAGRI.

---

## 🛠️ Como Começar (Gravação de Fábrica)

Para quem deseja apenas colocar a placa em funcionamento imediatamente:

1. Baixe o arquivo binário em [`releases/iotFeagri_factory.bin`](releases/iotFeagri_factory.bin).
2. Grave no seu ESP32 usando o [Espressif Flash Download Tool](https://www.espressif.com/en/support/download/other-tools) ou via Web Serial.
3. No celular, conecte-se à rede WiFi `IOT_FEAGRI_XXXX`.
4. Acesse `192.168.4.1` e preencha as credenciais.
5. Defina o `Perfil do projeto`. Se deixar em branco, a biblioteca usa o padrão da arquitetura.

---

## 💻 Para Desenvolvedores (Templates)

Se você vai desenvolver seu próprio firmware, escolha sua IDE preferida e utilize nossos templates:

### PlatformIO (Recomendado)
Acesse a pasta [`examples/PlatformIO`](examples/PlatformIO/). O projeto já vem configurado para buscar as dependências (`ArduinoJson`, `PubSubClient`) automaticamente.

### Arduino IDE
Acesse a pasta [`examples/Arduino`](examples/Arduino/). Copie os arquivos `.ino`, `.h` e `.cpp` para a pasta do seu sketch.

---

## 📋 Uso da Biblioteca

O código básico para integrar seu sensor é muito simples:

```cpp
#include "iotFeagri.h"

IotFeagri node;

void setup() {
  node.setFirmwareVersion("v1.1.0");
  node.begin(); // Inicializa WiFi, MQTT e Portal
}

void loop() {
  node.loop(); // Mantém conexões em segundo plano

  // Exemplo de telemetria
  static unsigned long last = 0;
  if (millis() - last > 10000) {
    last = millis();
    node.publish("sensor_luz", 450);
  }
}
```

## Grupos e OTA

Depois do provisionamento, a placa passa a se identificar como:
- `client_id = <group>_<MAC6>`
- `group = <user>_<profile>`

Exemplos:
- `leandro_exemploESP32_3FA780` com `group = leandro_exemploESP32`
- `ana_estufa_A1B2C3` com `group = ana_estufa`

No fluxo de OTA da biblioteca, o firmware é buscado em:
- `.../static/firmware/<group>/manifest.json`
- `.../static/firmware/<group>/firmware.bin`

Para manter compatibilidade com uploads antigos, a biblioteca tambem tenta como fallback:
- `.../static/firmware/generic_esp32/<group>/...`
- `.../static/firmware/generic_esp32c3/<group>/...`

Isso evita colisão entre usuários e também separa projetos distintos do mesmo usuário.

O campo `Servidor de firmware` do portal e opcional. Quando fica vazio, a biblioteca usa o `Host do broker` como servidor base do OTA. Para o host `leandro144.feagri.unicamp.br`, a biblioteca monta a URL de firmware com `https://` automaticamente. Preencha esse campo apenas quando o firmware estiver em outro servidor, ou quando for necessario forcar explicitamente `http://` ou `https://`.

Alterar o `Perfil do projeto` muda o `group` e, por consequencia, muda o `client_id` e os topicos automaticos. Apos salvar, a placa reinicia e recalcula esses valores no boot seguinte.

O portal de configuracao e protegido por login. O padrao inicial e:
- usuario: `admin`
- senha: `admin`

Essas credenciais podem ser alteradas na secao `Seguranca do portal` da pagina de configuracao. Tambem podem ser atualizadas por MQTT com os comandos `set_web_auth`, `set_web_credentials` ou `set_portal_credentials`. O comando aceita `user` e `pass` no JSON, ou um unico argumento em `value` no formato `usuario senha` ou `usuario:senha`, por exemplo `admin 1234`. A senha nao e exibida em status ou consultas.

A biblioteca tambem assina comandos por grupo no formato:
- `feagri/<user>/groups/<group>/cmd`

Exemplo para `client_id = leandro_exemploESP32_20F540`:
- `user = leandro`
- `group = leandro_exemploESP32`
- topico de grupo: `feagri/leandro/groups/leandro_exemploESP32/cmd`

Comandos enviados nesse topico sao recebidos por todas as placas do mesmo grupo.

O heartbeat e publicado a cada 10 segundos em:
- `feagri/<user>/devices/<client_id>/heartbeat`

Payload:
```json
{
  "type": "heartbeat",
  "client_id": "leandro_exemploESP32_20F540",
  "group": "leandro_exemploESP32",
  "owner": "leandro",
  "profile": "exemploESP32",
  "fw_version": "v1.1.1",
  "ip": "192.168.1.100",
  "rssi": -55,
  "timestamp": 1704825600000,
  "uptime_ms": 3600000
}
```

A dashboard identifica o grupo a partir do `client_id` no formato:
- `<group>_<MAC6>`

Ou seja, ela remove o sufixo final `_XXXXXX` e usa o restante como grupo do dispositivo.

### Topicos MQTT automaticos

A biblioteca nao persiste topicos MQTT configuraveis na NVS. Os topicos sao calculados a partir de `user`, `profile`, `group` e `client_id`.

Padrao dos topicos:
- Base do dispositivo: `feagri/<user>/devices/<client_id>`
- Comando individual: `feagri/<user>/devices/<client_id>/cmd`
- Comando de grupo: `feagri/<user>/groups/<group>/cmd`
- Dados de sensores privados: `feagri/<user>/devices/<client_id>/data/<tipo_sensor>/<serial_ou_id>`
- Dados de sensores publicos: `feagri/publico/users/<user>/devices/<client_id>/data/<tipo_sensor>/<serial_ou_id>`
- Dados de sensores admin: `feagri/adm/devices/<client_id>/data/<tipo_sensor>/<serial_ou_id>`
- Status/ACK: `feagri/<user>/devices/<client_id>/status`
- Heartbeat: `feagri/<user>/devices/<client_id>/heartbeat`

`data_visibility` aceita `private`, `public` ou `admin`. O padrao e `private`.

### RULE_CONFIG e RULE_COMMAND

A biblioteca tambem fornece helpers para o contrato padronizado de regras locais. Ela nao executa regras por conta propria; o firmware do projeto continua responsavel por persistir `RULE_CONFIG`, manter o runtime e executar a acao local fora do callback MQTT.

Topicos padrao:
- `RULE_CONFIG`: `feagri/<user>/devices/<client_id>/config/rules`
- `RULE_COMMAND`: `feagri/<user>/devices/<client_id>/config/rules/cmd`
- Pedido legado/status: `feagri/<user>/devices/<client_id>/config/rules/request`
- `RULE_ACK`: `feagri/<user>/devices/<client_id>/config/rules/ack`
- `RULE_STATUS` e `RULE_EVENT`: `feagri/<user>/devices/<client_id>/config/rules/status`

Retain:
- `RULE_CONFIG` pode ser publicado com retain.
- `RULE_COMMAND`, `RULE_ACK`, `RULE_STATUS` e `RULE_EVENT` devem ser publicados sem retain.

Contrato de conflito de revisao:
- Quando `RULE_CONFIG` for rejeitado por `stale rule revision` ou `same revision with different content`, o `RULE_ACK` de erro deve incluir a regra atualmente carregada no dispositivo.
- Campos esperados: `current_rule_id`, `current_revision` e `current_processor_type`.
- A dashboard usa `current_revision + 1` para republicar a proxima revisao sem tentativas incrementais longas.

Ciclo recomendado:
- `RULE_CONFIG` carrega e salva a regra, mas nao inicia execucao.
- `RULE_COMMAND start` inicia ou retoma a execucao local.
- `RULE_COMMAND stop` para a execucao sem apagar a regra salva.
- `RULE_COMMAND status` responde o estado atual, mesmo sem regra carregada.
- Estados comuns: `empty`, `loaded`, `idle`, `running`, `stopped`, `completed`, `error`.
- Eventos comuns: `loaded`, `started`, `stopped`, `completed`, `resumed`, `executed`, `error`.

Payload minimo de `RULE_COMMAND`:

```json
{
  "type": "RULE_COMMAND",
  "schema_version": 1,
  "command": "start",
  "client_id": "leandro_exemploESP32_20F540",
  "user_mqtt": "leandro",
  "rule_id": "rule-001",
  "revision": 3,
  "request_id": "opcional"
}
```

Helpers disponiveis:

```cpp
String base = IotFeagri::deviceBaseTopic(user, clientId);
String cmdTopic = IotFeagri::rulesCommandTopic(user, clientId);

RuleCommand cmd;
RuleValidationResult result = IotFeagri::validateRuleCommandJson(
    payload, clientId, userMqtt, cmd, activeRuleId, activeRevision);

RuleAck ack;
ack.clientId = clientId;
ack.userMqtt = userMqtt;
ack.command = cmd.command;
ack.ruleId = cmd.ruleId;
ack.revision = cmd.revision;
ack.status = result.ok ? "accepted" : "error";
ack.message = result.ok ? "RULE_COMMAND accepted" : result.error;
String ackJson = IotFeagri::buildRuleAck(ack);
```

`validateRuleCommandJson()` valida:
- `type == RULE_COMMAND`
- `schema_version == 1`
- `command` em `start`, `stop` ou `status`
- `client_id` igual ao da placa, quando informado
- `user_mqtt` igual ao usuario configurado, quando informado
- `rule_id` e `revision` contra a regra ativa para `start`/`stop`, quando esses valores forem passados

O callback MQTT deve apenas validar e agendar trabalho. Acionamento fisico, publish em cascata e qualquer operacao sensivel devem acontecer no `loop()`, task principal ou tick da rule engine.

Na API atual, `publish("temperature", valor)` usa o nome informado como `<tipo_sensor>` e o proprio `client_id` como fallback de `<serial_ou_id>`.

Para OTA MQTT, a biblioteca mantem compatibilidade com os topicos globais legados:
- `feagri/firmware/update/cmd`
- `feagri/firmware/update/status`

Ela tambem ja assina/publica os topicos por usuario, preparando a migracao futura:
- `feagri/<user>/firmware/update/cmd`
- `feagri/<user>/firmware/update/status`

### Upload pela dashboard

Ao compilar no exemplo PlatformIO, o artefato base continua sendo `firmware.bin`, mas o script `rename_bin.py` tambem cria uma copia com o nome do grupo para upload OTA.

Arquivos gerados por padrao:
- ESP32 com perfil padrão: `<user>_exemploESP32.bin`
- ESP32-C3 com perfil padrão: `<user>_exemploESP32C3.bin`
- projeto com perfil customizado: `<user>_<profile>.bin`

Exemplos:
- `leandro_exemploESP32.bin`
- `leandro_exemploESP32C3.bin`
- `ana_estufa.bin`

No exemplo PlatformIO, o usuario padrao do script e `leandro`. Para gerar outro nome sem editar o script, defina uma variavel de ambiente antes da compilacao:
- `IOTFEAGRI_OTA_USER=ana`
- `IOTFEAGRI_OTA_PROFILE=estufa`
- ou `IOTFEAGRI_OTA_GROUP=ana_estufa` para informar o grupo completo.

O campo `Versao` informado na dashboard e salvo no `manifest.json`. Apos uma OTA concluida com sucesso, a biblioteca persiste essa versao na NVS e passa a reporta-la no `heartbeat` e no `FW_STATUS`. Se o `manifest.json` tiver a mesma versao que a placa ja reporta, a biblioteca publica `FW_STATUS` com `state = "skipped"` e nao regrava o firmware.

Durante a OTA, a biblioteca publica `FW_STATUS` em `feagri/firmware/update/status` com estados intermediarios:
- `manifest_url`: URL de manifest que sera testada
- `starting`: manifest carregado, com versao e MD5 quando disponiveis
- `ota_url`: URL do binario OTA que sera baixado
- `downloading`: HTTP 200 recebido e progresso de escrita, com `progress`, `written` e `total`
- `updated`: gravacao finalizada, antes do reboot
- `error`: erro de manifest, download, escrita, MD5 ou tamanho
- `skipped`: versao do manifest igual a versao ja instalada

Isso permite que a dashboard detecte corretamente o grupo e grave o `manifest.json` no namespace esperado pelo OTA da biblioteca, mantendo coerência entre:
- nome do arquivo enviado
- grupo do dispositivo
- prefixo do `client_id`

### Web OTA local

A pagina de configuracao inclui uma secao `Atualizacao de firmware` para upload manual de arquivo `.bin`.

Esse fluxo usa `Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)`, valida os bytes escritos contra o tamanho recebido pelo upload, finaliza com `Update.end(true)` e reinicia a placa em caso de sucesso. Durante o Web OTA, a biblioteca pausa MQTT/heartbeat e mantem o servidor HTTP processando o upload.

Use Web OTA para testes locais e recuperacao rapida de placas em bancada. Para atualizacoes coordenadas de frota, prefira o fluxo OTA via dashboard/MQTT.

### Console serial

A biblioteca processa comandos pela Serial dentro de `node.loop()`. Abra o monitor serial em `115200` baud e digite `help`.

Comandos disponiveis:
- `help`: lista comandos.
- `status`: mostra uptime, heap, WiFi, MQTT, `client_id`, grupo e topicos principais.
- `config`: mostra host/porta MQTT, usuario, perfil, versao e servidor OTA.
- `reboot`: reinicia a placa.
- `open_portal`: abre o captive portal em `192.168.4.1`.
- `reset_config`: apaga a configuracao NVS da biblioteca e reinicia.
- `set_fw_host <host>`: define servidor de firmware.
- `set_fw_host default` ou `set_fw_host reset`: volta o servidor de firmware para o padrao, usando o Host MQTT.

Comandos MQTT comuns ja tratados pela biblioteca antes do callback do usuario:

| Comando | Argumento | Acao |
| --- | --- | --- |
| `UPDATE` | opcional | Inicia OTA via dashboard/MQTT. |
| `update_firmware` | opcional | Inicia OTA via dashboard/MQTT. |
| `trigger_update` | opcional | Inicia OTA via dashboard/MQTT. |
| `set_fw_host` | `host`, `default` ou `reset` | Define ou limpa o servidor de firmware. |
| `set_firmware_host` | `host`, `default` ou `reset` | Alias de `set_fw_host`. |
| `get_fw_host` | vazio | Publica o servidor de firmware atual no status. |
| `get_firmware_host` | vazio | Alias de `get_fw_host`. |
| `set_firmware_version` | versao | Salva a versao de firmware reportada. |
| `set_fw_version` | versao | Alias de `set_firmware_version`. |
| `get_firmware_version` | vazio | Publica a versao atual em `FW_STATUS`. |
| `get_fw_version` | vazio | Alias de `get_firmware_version`. |
| `reboot` | vazio | Reinicia a placa. |
| `status` | vazio | Publica WiFi, MQTT, heap e IP no status. |
| `config` | vazio | Publica resumo de configuracao no status. |
| `open_portal` | vazio | Abre o captive portal em `192.168.4.1`. |
| `close_portal` | vazio | Fecha o captive portal e volta para STA. |
| `set_web_auth` | `usuario senha` ou `usuario:senha` | Altera credenciais do portal. |
| `set_web_credentials` | `usuario senha` ou `usuario:senha` | Alias de `set_web_auth`. |
| `set_portal_credentials` | `usuario senha` ou `usuario:senha` | Alias de `set_web_auth`. |

O comando `reset_config` fica restrito ao console serial e nao e executado por MQTT.

---

## 🙋 Contribuição e Suporte

Encontrou um bug ou tem uma sugestão de melhoria? 
- Abra uma [**Issue**](https://github.com/morais-unicamp/iotFeagri/issues) detalhando o problema.
- Contribuições via *Pull Requests* são muito bem-vindas!

## 🎓 Créditos e Licença

Desenvolvido por **Leandro Morais** na **Faculdade de Engenharia Agrícola (FEAGRI) - UNICAMP**.

Este projeto está sob a licença **MIT** - veja o arquivo [LICENSE](LICENSE) para detalhes. O uso é livre para fins acadêmicos e de pesquisa, solicitando-se a citação da fonte original.

## 📚 Como Citar

Se você utilizar esta biblioteca em seu trabalho acadêmico ou profissional, utilize o botão **"Cite this repository"** na barra lateral direita do GitHub para obter a citação nos formatos APA ou BibTeX. Para trabalhos seguindo as normas brasileiras (**ABNT**), utilize:

> MORAIS, Leandro. **iotFeagri**: Biblioteca de Provisionamento e Telemetria para Ecossistema IoT FEAGRI. Versão 1.1.0. Campinas: FEAGRI/UNICAMP, 2026. Disponível em: <https://github.com/morais-unicamp/iotFeagri>.

> Morais, L. (2026). *iotFeagri: Biblioteca de Provisionamento e Telemetria para Ecossistema IoT FEAGRI* (Versão 1.1.0) [Software]. Disponível em https://github.com/morais-unicamp/iotFeagri

---
*Dashboard de Monitoramento: [leandro144.feagri.unicamp.br](http://leandro144.feagri.unicamp.br)*  
> ⚠️ **Nota de Acesso**: A dashboard é um recurso interno e está acessível apenas para computadores conectados à **rede cabeada** ou **VPN da UNICAMP**.
