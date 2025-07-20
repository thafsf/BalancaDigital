#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "HX711.h"

// =================== CONFIG WIFI (Access Point) ===================
const char* ssid = "Balanca-IoT";
const char* password = "12345678";

// =================== HX711 ===================
const int DOUT_PIN = 21;
const int SCK_PIN = 22;
HX711 scale;

// =================== WebSocket + Web Server ===================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

unsigned long lastSend = 0;
const long interval = 500; // intervalo em ms

// =================== HTML da Interface ===================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
  <title>Terminal Balança</title>
  <style>
    body {
      font-family: monospace;
      background-color: #0f172a;
      color: #38bdf8;
      margin: 0;
      padding: 1rem;
    }
    #log {
      white-space: pre-line;
      overflow-y: auto;
      max-height: 100vh;
    }
  </style>
</head>
<body>
  <h2>Terminal da Balança (bruto)</h2>
  <div id="log"></div>

  <script>
    const logEl = document.getElementById('log');
    const socket = new WebSocket(`ws://${window.location.hostname}/ws`);

    socket.onmessage = (event) => {
      const newLine = document.createElement('div');
      newLine.textContent = event.data;
      logEl.appendChild(newLine);
      logEl.scrollTop = logEl.scrollHeight;
    };
  </script>
</body>
</html>
)rawliteral";

// =================== Eventos do WebSocket ===================
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("Cliente %u conectado\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("Cliente %u desconectado\n", client->id());
  }
}

// =================== Setup ===================
void setup() {
  Serial.begin(115200);

  // Modo Access Point
  WiFi.softAP(ssid, password);
  Serial.println("Access Point iniciado!");
  Serial.print("IP local: ");
  Serial.println(WiFi.softAPIP());

  // Inicia a balança
  scale.begin(DOUT_PIN, SCK_PIN);
  scale.tare(20);
  Serial.println("Balança pronta!");

  // WebSocket
  ws.onEvent(onEvent);
  server.addHandler(&ws);

  // Página HTML
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.begin();
}

// =================== Loop ===================
void loop() {
  unsigned long now = millis();
  if (now - lastSend >= interval) {
    lastSend = now;
    if (scale.is_ready() && ws.count() > 0) {
      long raw = scale.get_value(100); // Média de 100 leituras
      ws.textAll(String(raw));
      Serial.println(raw);
    }
  }

  ws.cleanupClients();
}
