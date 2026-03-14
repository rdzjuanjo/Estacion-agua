// Sensor de pH y Conductividad esp32
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ElegantOTA.h>
#include "leerph.h"
#include "WebHandlers.h"

// ── Configuración AP ────────────────────────────────────────────────────────
const char *AP_SSID = "Sensor de pH y Conductividad";
const char *AP_PASS = "";

IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

// ── Objetos globales ────────────────────────────────────────────────────────
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer      dnsServer;

// ── Timing WebSocket ────────────────────────────────────────────────────────
unsigned long previousWsMillis = 0;
const unsigned long wsInterval = 200;

// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(100);

  setupleerph();

  // Portal cautivo
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.start(53, "*", apIP);

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect("http://" + apIP.toString() + "/");
  });

  // WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Rutas
  server.on("/",                       HTTP_GET, handleRoot);
  server.on("/calibrar4",              HTTP_GET, handleCalibrar4);
  server.on("/calibrar7",              HTTP_GET, handleCalibrar7);
  server.on("/calibrar10",             HTTP_GET, handleCalibrar10);
  server.on("/calibrar-conductividad", HTTP_GET, handleCalibrarConductividad);
  server.on("/api/get-slopes",         HTTP_GET, handleGetSlopes);

  server.begin();
  initOTA();  // habilita /update para OTA firmware

  Serial.printf("\nSSID: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void loop() {
  loopleerph();
  dnsServer.processNextRequest();
  ws.cleanupClients();
  ElegantOTA.loop();

  unsigned long now = millis();
  if (now - previousWsMillis >= wsInterval) {
    previousWsMillis = now;
    sendSensorData();
  }
}
