# iotFeagri Kit - ESP32 🚀

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![FEAGRI](https://img.shields.io/badge/FEAGRI-UNICAMP-blue.svg)](https://www.feagri.unicamp.br/)

Biblioteca e Kit de Desenvolvimento para simplificar o provisionamento e telemetria de sensores no ecossistema **IoT FEAGRI - UNICAMP**.

## 🌟 Principais Recursos
- **Provisionamento via Portal**: Configure Rede WiFi e Credenciais MQTT via celular (Captive Portal em `192.168.4.1`).
- **Persistência NVS**: Credenciais são salvas permanentemente na memória do ESP32.
- **Auto-OTA**: Atualização de firmware automática via Dashboard.
- **Suporte Multi-IDE**: Templates prontos para **Arduino IDE** e **PlatformIO (VS Code)**.
- **Sincronização de Tempo**: Relógio interno (RTC) sincronizado automaticamente com os servidores da FEAGRI.

---

## 🛠️ Como Começar (Gravação de Fábrica)

Para quem deseja apenas colocar a placa em funcionamento imediatamente:

1. Baixe o arquivo binário em [`releases/iotFeagri_factory.bin`](releases/iotFeagri_factory.bin).
2. Grave no seu ESP32 usando o [Espressif Flash Download Tool](https://www.espressif.com/en/support/download/other-tools) ou via Web Serial.
3. No celular, conecte-se à rede WiFi `IOT_FEAGRI_XXXX`.
4. Acesse `192.168.4.1` e preencha as credenciais.

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
  node.begin(); // Inicializa WiFi, MQTT e Portal
  node.setFirmwareVersion("v1.1.0");
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
