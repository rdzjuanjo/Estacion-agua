// Sensor de pH y Conductividad esp32
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ElegantOTA.h>
#include <Preferences.h>
#include "Config.h"
#include "leerph.h"
#include "WebHandlers.h"
#include "FieldMode.h"
#include "SimpleLoggerWS.h"  // Reemplaza DataLoggerWS pesado con SimpleLoggerWS liviano

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// ── Configuración AP ────────────────────────────────────────────────────────
const char *AP_SSID = "Sensor de pH y Conductividad";
const char *AP_PASS = "";

IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

// ── Objetos globales ────────────────────────────────────────────────────────
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer      dnsServer;
SimpleLoggerWS logger;

enum DeviceMode {
  MODE_CONFIG = 0,
  MODE_FIELD  = 1,
};

DeviceMode currentMode = MODE_CONFIG;

// ── Timing WebSocket ────────────────────────────────────────────────────────
unsigned long previousWsMillis = 0;
const unsigned long wsInterval = 200;

// ── Timing Serial Logging ───────────────────────────────────────────────────
unsigned long previousSerialMillis = 0;
const unsigned long serialInterval = 2000;  // cada 2 segundos

static void setBuiltinLed(bool on) {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
}

static void blinkBuiltinLed(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    setBuiltinLed(true);
    delay(onMs);
    setBuiltinLed(false);
    delay(offMs);
  }
}

static void saveMode(DeviceMode mode) {
  Preferences modePrefs;
  modePrefs.begin(PREF_NS_MODE, false);
  modePrefs.putBool(PREF_KEY_ISFIELD, mode == MODE_FIELD);
  modePrefs.end();
}

static DeviceMode loadMode() {
  Preferences modePrefs;
  modePrefs.begin(PREF_NS_MODE, true);
  bool hasSavedMode = modePrefs.isKey(PREF_KEY_ISFIELD);
  bool savedIsField = modePrefs.getBool(PREF_KEY_ISFIELD, false);
  modePrefs.end();

  return hasSavedMode && savedIsField ? MODE_FIELD : MODE_CONFIG;
}

static void requestModeChangeAndRestart(DeviceMode nextMode) {
  Serial.printf("[MODO] Cambio solicitado: %s -> %s\n",
                currentMode == MODE_FIELD ? "CAMPO" : "CONFIG",
                nextMode == MODE_FIELD ? "CAMPO" : "CONFIG");

  // Indicador visual previo al reinicio.
  blinkBuiltinLed(8, 120, 120);

  saveMode(nextMode);
  Serial.println("[MODO] Reiniciando para aplicar cambio...");
  Serial.flush();
  delay(50);
  ESP.restart();
}

static void handleBootButtonInConfig() {
  bool pressed = (digitalRead(PIN_BOOT_BUTTON) == LOW);

  if (pressed && !bootPressed) {
    bootPressed = true;
    bootPressStart = millis();
    return;
  }

  if (!pressed && bootPressed) {
    bootPressed = false;
    return;
  }

  if (pressed && bootPressed && (millis() - bootPressStart >= BOOT_HOLD_MS)) {
    bootPressed = false;
    requestModeChangeAndRestart(MODE_FIELD);
  }
}

// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

  //currentMode = MODE_CONFIG;
  currentMode = loadMode();
  Serial.printf("[MODO] Inicio en: %s\n", currentMode == MODE_FIELD ? "CAMPO" : "CONFIG");

  // ── MOSFET: encendido siempre (modo config necesita los sensores) ──────────
  pinMode(PIN_MOSFET, OUTPUT);
  digitalWrite(PIN_MOSFET, HIGH);
  
  if (currentMode == MODE_FIELD) {
    setBuiltinLed(false);
    runFieldMode();  // nunca retorna; entra a deep-sleep al terminar
  }

  // En modo configuración el LED builtin queda encendido de forma fija.
  setBuiltinLed(true);

  setupleerph();

  // Inicializar sensores adicionales (temperatura y batería)
  initAdditionalSensors();

  // Inicializar LittleFS para SimpleLoggerWS
  if (!LittleFS.begin(true)) {
    Serial.println("[CONFIG] Error al inicializar LittleFS");
  } else {
    Serial.println("[CONFIG] LittleFS inicializado correctamente");
  }

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
  server.on("/mqtt",                   HTTP_GET, handleMqttConfig);
  server.on("/mqtt/save",              HTTP_GET, handleMqttSave);

  // SimpleLoggerWS: rutas en /logger para consultar/descargar logs de campo
  logger.beginWebServer(server);

  server.begin();
  initOTA();  // habilita /update para OTA firmware

  Serial.printf("\nSSID: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void loop() {
  if (currentMode == MODE_CONFIG) {
    handleBootButtonInConfig();
  }

  loopleerph();
  updateSensorReadings();  // Actualizar sensores adicionales (temperatura)
  dnsServer.processNextRequest();
  ws.cleanupClients();
  ElegantOTA.loop();

  unsigned long now = millis();
  
  // Envío de datos por WebSocket
  if (now - previousWsMillis >= wsInterval) {
    previousWsMillis = now;
    sendSensorData();
  }
  
  // Logging serial en modo CONFIG
  if (currentMode == MODE_CONFIG && now - previousSerialMillis >= serialInterval) {
    previousSerialMillis = now;
    
    float ph = getpHValue();
    float cond = getConductivityPPM();
    float temp = getTemperature();
    int bat = getBatteryPercent();
    
    Serial.print("[CONFIG] pH: ");
    Serial.print(ph, 2);
    Serial.print(" | Cond: ");
    Serial.print(cond, 1);
    Serial.print(" ppm | Temp: ");
    if (isnan(temp)) {
      Serial.print("NC");
    } else {
      Serial.print(temp, 1);
      Serial.print("°C");
    }
    Serial.print(" | Bat: ");
    if (bat < 0) {
      Serial.println("NC");
    } else {
      Serial.print(bat);
      Serial.println("%");
    }
  }
}
