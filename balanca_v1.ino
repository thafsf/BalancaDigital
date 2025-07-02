#include <AsyncTCP.h>

#include <ESPAsyncWebServer.h>


#include <WiFi.h>
#include "HX711.h"

// =======================================================================
// =================== CONFIGURAÇÕES QUE VOCÊ DEVE ALTERAR =================
// =======================================================================

// Defina o nome e a senha para a rede Wi-Fi que a ESP32 irá criar
const char* AP_SSID = "Balanca-IoT";       // Nome da rede Wi-Fi
const char* AP_PASSWORD = "12345678"; // Senha da rede (mínimo 8 caracteres)

// Pinos do sensor de peso HX711
const int LOADCELL_DOUT_PIN = 21;
const int LOADCELL_SCK_PIN = 22;

// FATOR DE CALIBRAÇÃO
// Use o código de calibração para encontrar este valor.
float FATOR_DE_CALIBRACAO = 454250.0; 

// =======================================================================
// ==================== FIM DAS CONFIGURAÇÕES PRINCIPAIS ===================
// =======================================================================

// Objetos globais
HX711 scale;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Variáveis para controlar o envio de dados sem usar delay()
unsigned long previousMillis = 0;
const long interval = 500; // Intervalo para enviar dados de peso (em ms)

// Conteúdo da sua página HTML (code.html) - SEM ALTERAÇÕES AQUI
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Balança Digital IoT</title>
  <style>
    body {
      font-family: sans-serif;
      background-color: #f8fafc;
      margin: 0;
      padding: 0;
    }

    .container {
      max-width: 700px;
      margin: 0 auto;
      padding: 1rem;
      display: flex;
      flex-direction: column;
      justify-content: center;
      align-items: center;
      min-height: 100vh;
    }

    h1 {
      font-size: 2rem;
      font-weight: bold;
      color: #1e293b;
      margin-bottom: 0.5rem;
      text-align: center;
    }

    p {
      color: #64748b;
      text-align: center;
      margin-bottom: 2rem;
    }

    .card {
      background-color: white;
      border-radius: 1rem;
      padding: 2rem;
      box-shadow: 0 20px 25px -5px rgba(0, 0, 0, 0.1),
                  0 8px 10px -6px rgba(0, 0, 0, 0.1);
      margin-bottom: 2rem;
      width: 100%;
    }

    .status {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 2rem;
    }

    .connection-indicator {
      width: 12px;
      height: 12px;
      border-radius: 50%;
      display: inline-block;
      margin-right: 0.5rem;
    }

    .connected {
      background-color: #10b981;
      box-shadow: 0 0 10px #10b981;
    }

    .disconnected {
      background-color: #ef4444;
      box-shadow: 0 0 10px #ef4444;
    }

    .connection-text {
      font-size: 0.875rem;
      font-weight: 500;
      color: #64748b;
    }

    .digital-display {
      background: linear-gradient(145deg, #ffffff, #f1f5f9);
      box-shadow: 20px 20px 60px #d1d5db, -20px -20px 60px #ffffff;
      border-radius: 1rem;
      padding: 2rem;
      display: flex;
      flex-direction: column;
      align-items: center;
      margin-bottom: 2rem;
    }

    .weight-label {
      color: #94a3b8;
      font-weight: 500;
      margin-bottom: 0.5rem;
    }

    .weight-value {
      font-size: 3rem;
      font-weight: bold;
      color: #3b82f6;
      text-shadow: 0 0 10px rgba(59, 130, 246, 0.3);
      transition: transform 0.3s;
    }

    .weight-unit {
      color: #64748b;
    }

    .tare-button {
      background-color: #2563eb;
      color: white;
      font-weight: 600;
      padding: 0.75rem 1.5rem;
      border-radius: 9999px;
      border: none;
      cursor: pointer;
      transition: background-color 0.3s, transform 0.3s;
    }

    .tare-button:hover {
      background-color: #1d4ed8;
      transform: scale(1.05);
    }

    .tare-button:disabled {
      background-color: #93c5fd;
      cursor: not-allowed;
    }

    .footer {
      font-size: 0.875rem;
      color: #94a3b8;
      text-align: center;
      margin-top: 2rem;
    }

    .limit-warning {
      animation: pulse 2s infinite;
    }

    @keyframes pulse {
      0% { color: #ef4444; }
      50% { color: #f87171; }
      100% { color: #ef4444; }
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Balança Digital IoT</h1>
    <p>Projeto de Metrologia e Instrumentação</p>

    <div class="card">
      <div class="status">
        <div>
          <span id="connectionStatus" class="connection-indicator disconnected"></span>
          <span id="connectionText" class="connection-text">Desconectado</span>
        </div>
        <div class="connection-text">ESP32 HX711 (0–10000g)</div>
      </div>

      <div class="digital-display">
        <div class="weight-label">PESO ATUAL</div>
        <div id="weightValue" class="weight-value">0,0000</div>
        <div class="weight-unit">g</div>
      </div>

      <div style="text-align: center;">
        <button id="tareButton" class="tare-button">Tarar Balança</button>
      </div>
    </div>

    <div class="footer">
      Desenvolvido por <span class="font-medium">Flávia</span> e <span class="font-medium">Thauan</span>
    </div>
  </div>

  <script>
    let socket = null;
    const MAX_WEIGHT = 10000;
    let tareOffset = 0;

    const weightValueElement = document.getElementById('weightValue');
    const tareButton = document.getElementById('tareButton');
    const connectionStatusElement = document.getElementById('connectionStatus');
    const connectionTextElement = document.getElementById('connectionText');

    function formatWeight(weight) {
      return (weight * 1000).toFixed(4).replace('.', ',');
    }

    function updateWeightDisplay(weight) {
      const netWeight = weight - tareOffset;
      if (netWeight >= MAX_WEIGHT * 0.9) {
        weightValueElement.classList.add('limit-warning');
      } else {
        weightValueElement.classList.remove('limit-warning');
      }
      weightValueElement.textContent = formatWeight(netWeight);
      weightValueElement.style.transform = 'scale(1.1)';
      setTimeout(() => {
        weightValueElement.style.transform = 'scale(1)';
      }, 200);
    }

    function updateConnectionStatus(isConnected) {
      connectionStatusElement.classList.toggle('connected', isConnected);
      connectionStatusElement.classList.toggle('disconnected', !isConnected);
      connectionTextElement.textContent = isConnected ? 'Conectado' : 'Desconectado';
    }

    function connectWebSocket() {
      const gateway = `ws://${window.location.hostname}/ws`;
      console.log("Tentando conectar a: " + gateway);
      socket = new WebSocket(gateway);
      socket.onopen = () => {
        console.log("Conexão estabelecida.");
        updateConnectionStatus(true);
      };
      socket.onclose = () => {
        console.log("Conexão encerrada. Tentando reconectar...");
        updateConnectionStatus(false);
        setTimeout(connectWebSocket, 2000);
      };
      socket.onmessage = (event) => {
        const weight = parseFloat(event.data);
        if (!isNaN(weight)) {
          updateWeightDisplay(weight);
        }
      };
    }

    tareButton.addEventListener('click', () => {
      if (socket && socket.readyState === WebSocket.OPEN) {
        const currentDisplay = parseFloat(weightValueElement.textContent.replace(',', '.'));
        tareOffset += currentDisplay;
        weightValueElement.textContent = '0,0000';
        socket.send('TARE');

        tareButton.textContent = 'Tarando...';
        tareButton.disabled = true;

        setTimeout(() => {
          tareButton.textContent = 'Tara concluída';
          setTimeout(() => {
            tareButton.textContent = 'Tarar Balança';
            tareButton.disabled = false;
          }, 1000);
        }, 800);
      }
    });

    window.addEventListener('load', () => {
      connectWebSocket();
    });
  </script>
</body>
</html>

)rawliteral";

// Função para tratar eventos do WebSocket - SEM ALTERAÇÕES AQUI
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("Cliente WebSocket #%u conectado de %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("Cliente WebSocket #%u desconectado\n", client->id());
      break;
    case WS_EVT_DATA:
      if (len == 4 && strncmp((char*)data, "TARE", 4) == 0) {
        Serial.println("Comando TARE recebido. Zerando a balança...");
        scale.tare();
      }
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

// =======================================================================
// =================== SEÇÃO SETUP() MODIFICADA ==========================
// =======================================================================
void setup() {
  Serial.begin(115200);

  // Inicia a ESP32 no modo Access Point (AP)
  Serial.print("Criando Access Point...");
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  
  Serial.println("AP Criado!");
  Serial.print("Endereço IP do AP: ");
  Serial.println(WiFi.softAPIP()); // O IP padrão é 192.168.4.1

  // Inicia o sensor de peso
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(FATOR_DE_CALIBRACAO);
  scale.tare(); // Zera a balança na inicialização
  Serial.println("Balança pronta.");

  // Anexa o manipulador de eventos do WebSocket
  ws.onEvent(onEvent);
  server.addHandler(&ws);

  // Configura o servidor web para servir a página HTML
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // Inicia o servidor
  server.begin();
}
// =======================================================================
// =================== FIM DA SEÇÃO SETUP() MODIFICADA ===================
// =======================================================================


// Função loop()
void loop() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    
    if (scale.is_ready() && ws.count() > 0) {
      float peso = scale.get_units(10);
      ws.textAll(String(peso, 7)); //ex 1,34502kg -> 1345,02g
    }
  }

  ws.cleanupClients();
}
