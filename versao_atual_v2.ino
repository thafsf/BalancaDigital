#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "HX711.h"

// =======================================================================
// =================== CONFIGURAÇÕES DO WI-FI E PINOS =====================
// =======================================================================

const char* AP_SSID = "Balanca-IoT";
const char* AP_PASSWORD = "12345678";
const int LOADCELL_DOUT_PIN = 21;
const int LOADCELL_SCK_PIN = 22;

// =======================================================================
// ================ ETAPA DE CALIBRAÇÃO MULTIPONTO =====================
// =======================================================================

// 1. Defina uma estrutura para armazenar os pontos de calibração
struct CalibrationPoint {
  long raw;      // Leitura bruta do sensor (que você anotou)
  float weight;  // Peso conhecido em gramas (ex: 100.0)
};

// 2. INSIRA AQUI OS DADOS QUE VOCÊ COLETOU NA ETAPA 1
//    O primeiro ponto DEVE ser {0, 0.0}.
//    Adicione quantos pontos quiser, em ordem crescente de peso.
const CalibrationPoint calibration_points[] = {
  {9650, 20.6},
  {99050, 220.3},
  {474590, 1050.9},
  {575400, 1271.3},
  {878100, 1945.1} // Exemplo com 1kg
  // Adicione mais pontos aqui se tiver...
};

// Calcula o número de pontos de calibração que você inseriu
const int num_calibration_points = sizeof(calibration_points) / sizeof(CalibrationPoint);

// =======================================================================

// Objetos globais e HTML (sem alterações)
HX711 scale;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
unsigned long previousMillis = 0;
const long interval = 500;
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>Balança Digital IoT</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
            background-color: #f8fafc;
            margin: 0;
            padding: 0;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
        }

        .container {
            max-width: 450px;
            width: 100%;
            margin: 1rem;
        }

        .card {
            background-color: white;
            border-radius: 1.5rem;
            padding: 2rem;
            box-shadow: 0 20px 25px -5px rgba(0, 0, 0, 0.1), 0 8px 10px -6px rgba(0, 0, 0, 0.1);
        }

        .status {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 2rem;
        }

        .connection-status {
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }
        
        .connection-indicator {
            width: 12px;
            height: 12px;
            border-radius: 50%;
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
            text-align: center;
            margin: 2rem 0;
        }

        .weight-value {
            font-size: 3.5rem;
            font-weight: 700;
            color: #3b82f6;
            line-height: 1.1;
            font-variant-numeric: tabular-nums;
        }
        
        .weight-unit {
            font-size: 1.5rem;
            color: #64748b;
            margin-left: 0.25rem;
        }

        .tare-button {
            display: block;
            width: 100%;
            background-color: #2563eb;
            color: white;
            font-weight: 600;
            padding: 0.875rem;
            border-radius: 0.75rem;
            border: none;
            cursor: pointer;
            transition: background-color 0.2s, transform 0.2s;
            margin-top: 2rem;
        }

        .tare-button:hover { background-color: #1d4ed8; }
        .tare-button:active { transform: scale(0.98); }
        .tare-button:disabled { background-color: #93c5fd; cursor: not-allowed; }

        .footer {
            font-size: 0.875rem;
            color: #94a3b8;
            text-align: center;
            margin-top: 2rem;
        }

        /* --- Estilos para o novo mostrador SVG --- */
        .analog-gauge {
            width: 100%;
            max-width: 350px;
            margin: 1rem auto 0;
        }
        .gauge-background {
            fill: #f1f5f9;
            stroke: #e2e8f0;
            stroke-width: 1;
        }
        .gauge-tick {
            stroke: #94a3b8;
            stroke-width: 1.5;
        }
        .gauge-tick-major {
            stroke: #334155;
            stroke-width: 2;
        }
        .gauge-label {
            font-size: 10px;
            fill: #334155;
            text-anchor: middle;
            font-weight: 500;
        }
        .gauge-pointer {
            fill: #ef4444;
            stroke: #b91c1c;
            stroke-width: 0.5;
            transition: transform 0.5s cubic-bezier(0.68, -0.6, 0.32, 1.6);
        }
    </style>
</head>
<body>
    <div class="container">
      <h1>Balança Digital IoT</h1>
      <p>Projeto de Metrologia e Instrumentação</p>
      
        <div class="card">
            <div class="status">
                <div class="connection-status">
                    <span id="connectionStatus" class="connection-indicator disconnected"></span>
                    <span id="connectionText" class="connection-text">Desconectado</span>
                </div>
                <div class="connection-text">ESP32 HX711</div>
            </div>

            <div class="digital-display">
                <span id="weightValue" class="weight-value">0,00</span><span class="weight-unit">g</span>
            </div>

            <div class="analog-gauge">
                <svg viewBox="0 0 200 110" id="gaugeSvg">
                    <defs>
                        <path id="gaugeArc" d="M 10 100 A 90 90 0 0 1 190 100" fill="none" />
                    </defs>
                    <use href="#gaugeArc" class="gauge-background" />
                    <g id="gaugeTicks"></g>
                    <g id="gaugeLabels"></g>
                    <path id="gaugePointer" d="M 100 100 L 99 15 L 101 15 Z" class="gauge-pointer" />
                </svg>
            </div>
            
            <button id="tareButton" class="tare-button">Tarar Balança</button>
        </div>

        <div class="footer">
            Desenvolvido por <span style="font-weight: 500;">Flávia</span> e <span style="font-weight: 500;">Thauan</span>
        </div>
    </div>

    <script>
        // --- Elementos do DOM ---
        const weightValueElement = document.getElementById('weightValue');
        const tareButton = document.getElementById('tareButton');
        const connectionStatusElement = document.getElementById('connectionStatus');
        const connectionTextElement = document.getElementById('connectionText');
        const gaugePointer = document.getElementById('gaugePointer');
        const gaugeTicks = document.getElementById('gaugeTicks');
        const gaugeLabels = document.getElementById('gaugeLabels');

        // --- Configurações da Balança ---
        const MAX_DISPLAY_WEIGHT = 10000; // ALTERADO: Peso máximo do mostrador para 10000g
        let socket = null;

        // --- Funções Auxiliares ---
        function polarToCartesian(centerX, centerY, radius, angleInDegrees) {
            const angleInRadians = (angleInDegrees - 180) * Math.PI / 180.0;
            return {
                x: centerX + (radius * Math.cos(angleInRadians)),
                y: centerY + (radius * Math.sin(angleInRadians))
            };
        }

        // --- Funções Principais ---
        function createScaleMarkers() {
            // Limpa marcadores antigos, se houver
            gaugeTicks.innerHTML = '';
            gaugeLabels.innerHTML = '';

            // ALTERADO: Lógica para criar as marcas e rótulos
            for (let i = 0; i <= MAX_DISPLAY_WEIGHT; i += 500) { // Um risco a cada 500g
                const angle = (i / MAX_DISPLAY_WEIGHT) * 180;
                const isMajorTick = (i % 1000 === 0); // Marcas maiores a cada 1000g

                const startRadius = isMajorTick ? 80 : 85;
                const endRadius = 90;

                const startPoint = polarToCartesian(100, 100, startRadius, angle);
                const endPoint = polarToCartesian(100, 100, endRadius, angle);

                const tick = document.createElementNS("http://www.w3.org/2000/svg", "line");
                tick.setAttribute('x1', startPoint.x);
                tick.setAttribute('y1', startPoint.y);
                tick.setAttribute('x2', endPoint.x);
                tick.setAttribute('y2', endPoint.y);
                tick.setAttribute('class', isMajorTick ? 'gauge-tick-major' : 'gauge-tick');
                gaugeTicks.appendChild(tick);

                if (isMajorTick) {
                    const labelRadius = 70;
                    const labelPoint = polarToCartesian(100, 100, labelRadius, angle);
                    const label = document.createElementNS("http://www.w3.org/2000/svg", "text");
                    label.setAttribute('x', labelPoint.x);
                    label.setAttribute('y', labelPoint.y);
                    label.setAttribute('class', 'gauge-label');
                    label.textContent = i / 1000 + 'k'; // Mostra em 'k' para kg
                    gaugeLabels.appendChild(label);
                }
            }
        }

        function updateWeightDisplay(weight) {
            const displayWeight = Math.max(0, weight);
            
            weightValueElement.textContent = displayWeight.toFixed(2).replace('.', ',');

            // ALTERADO: Mapeamento usa o novo MAX_DISPLAY_WEIGHT
            let angle = (displayWeight / MAX_DISPLAY_WEIGHT) * 180 - 90;
            angle = Math.min(90, Math.max(-90, angle));

            gaugePointer.style.transform = `rotate(${angle}deg)`;
            gaugePointer.style.transformOrigin = '100px 100px';
        }

        function updateConnectionStatus(isConnected) {
            connectionStatusElement.classList.toggle('connected', isConnected);
            connectionStatusElement.classList.toggle('disconnected', !isConnected);
            connectionTextElement.textContent = isConnected ? 'Conectado' : 'Desconectado';
        }

        function connectWebSocket() {
            const gateway = `ws://${window.location.hostname}/ws`;
            socket = new WebSocket(gateway);

            socket.onopen = () => {
                updateConnectionStatus(true);
            };
            socket.onclose = () => {
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

        // --- Event Listeners ---
        tareButton.addEventListener('click', () => {
            if (socket && socket.readyState === WebSocket.OPEN) {
                socket.send('TARE');
                
                tareButton.textContent = 'Tarando...';
                tareButton.disabled = true;
                setTimeout(() => {
                    tareButton.textContent = 'Tarar Balança';
                    tareButton.disabled = false;
                }, 1500);
            }
        });

        window.addEventListener('load', () => {
            createScaleMarkers();
            updateWeightDisplay(0);
            connectWebSocket();
        });
    </script>
</body>
</html>
)rawliteral";


// Função que calcula o peso usando interpolação linear
float getCalibratedWeight(long raw_reading) {
  // Se a leitura for menor que o primeiro ponto, retorna 0
  if (raw_reading <= calibration_points[0].raw) {
    return 0.0;
  }
  
  // Procura o segmento correto na tabela de calibração
  for (int i = 0; i < num_calibration_points - 1; i++) {
    // Verifica se a leitura está entre o ponto 'i' e o próximo ponto 'i+1'
    if (raw_reading >= calibration_points[i].raw && raw_reading < calibration_points[i+1].raw) {
      
      // Pontos do segmento (x0, y0) e (x1, y1)
      long x0 = calibration_points[i].raw;
      float y0 = calibration_points[i].weight;
      long x1 = calibration_points[i+1].raw;
      float y1 = calibration_points[i+1].weight;
      
      // Fórmula da interpolação linear: y = y0 + (x - x0) * (y1 - y0) / (x1 - x0)
      return y0 + (float)(raw_reading - x0) * (y1 - y0) / (float)(x1 - x0);
    }
  }
  
  // Se a leitura for maior que o último ponto, fazemos uma extrapolação
  // usando os dois últimos pontos para manter a escala.
  long x0 = calibration_points[num_calibration_points - 2].raw;
  float y0 = calibration_points[num_calibration_points - 2].weight;
  long x1 = calibration_points[num_calibration_points - 1].raw;
  float y1 = calibration_points[num_calibration_points - 1].weight;
  
  return y0 + (float)(raw_reading - x0) * (y1 - y0) / (float)(x1 - x0);
}

// Função de eventos do WebSocket (agora zera a balança usando scale.tare())
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) {
        if (len == 4 && strncmp((char*)data, "TARE", 4) == 0) {
            Serial.println("Comando TARE recebido. Zerando a balança...");
            scale.tare(20); // Zera a balança com uma média de 20 leituras
        }
    }
}


void setup() {
  Serial.begin(115200);

  // Inicia Wi-Fi, etc. (sem alterações)
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println("Access Point Criado!");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  // Inicia o sensor de peso
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  
  // IMPORTANTE: Não usamos mais set_scale(). Apenas taramos.
  scale.tare(20); 
  
  Serial.println("Balança pronta com calibração multiponto.");

  // Configurações do servidor (sem alterações)
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });
  server.begin();
}


void loop() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    
    if (scale.is_ready() && ws.count() > 0) {
      // 1. Pega a leitura bruta (com offset da tara já aplicado)
      long raw_value = scale.get_value(100);
      
      // 2. Converte a leitura bruta para peso usando nossa nova função
      float peso_em_gramas = getCalibratedWeight(raw_value);
      
      // 3. Envia o peso em gramas para a interface web
      ws.textAll(String(peso_em_gramas, 2)); // Envia com 2 casas decimais

      Serial.println(raw_value);
      Serial.println(peso_em_gramas, 2);
    }

  }

  ws.cleanupClients();
}
