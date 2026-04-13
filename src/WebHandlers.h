// WebHandlers.h - Handlers HTTP, WebSocket y HTML para sensor de pH y Conductividad
#ifndef WEBHANDLERS_H
#define WEBHANDLERS_H

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ElegantOTA.h>
#include <Wire.h>
#include <Adafruit_MAX1704X.h>
#include "leerph.h"
#include "Config.h"
#include "TempSensor.h"

// ── Declaraciones externas (definidas en el .ino) ────────────────────────────
extern AsyncWebSocket   ws;
extern AsyncWebServer   server;

// ── Sensores globales ─────────────────────────────────────────────────────────
static TempSensor tempSensor;
static Adafruit_MAX17048 batteryGauge;
static bool tempSensorReady = false;
static bool batteryGaugeReady = false;

// ============================================================================
//  INICIALIZACION DE SENSORES
// ============================================================================

void initAdditionalSensors() {
  // Inicializar sensor de temperatura
  tempSensor.begin();
  tempSensorReady = true;
  Serial.println("[WEBHANDLERS] Sensor de temperatura iniciado");

  // Inicializar medidor de batería
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (batteryGauge.begin()) {
    batteryGaugeReady = true;
    Serial.printf("[WEBHANDLERS] MAX17048 detectado (chip 0x%X)\n", batteryGauge.getChipID());
  } else {
    batteryGaugeReady = false;
    Serial.println("[WEBHANDLERS] MAX17048 no detectado");
  }
}

void updateSensorReadings() {
  if (tempSensorReady) {
    tempSensor.loop();
  }
}

float getTemperature() {
  if (!tempSensorReady) return NAN;
  return tempSensor.getTemperature();
}

int getBatteryPercent() {
  if (!batteryGaugeReady) return -1; // Indicador de no conectado
  float pct = batteryGauge.cellPercent();
  if (isnan(pct)) return -1;
  return (int)constrain(pct, 0.0f, 100.0f);
}

String getTemperatureString() {
  float temp = getTemperature();
  if (isnan(temp)) return "NC";
  return String(temp, 1) + "°C";
}

String getBatteryString() {
  int bat = getBatteryPercent();
  if (bat < 0) return "NC";
  return String(bat) + "%";
}

// ============================================================================
//  FUNCIONES HELPER
// ============================================================================

float calculateSlope47() {
  int ph4, ph7, ph10;
  bool isDirectRelation;
  loadCalibration(ph4, ph7, ph10, isDirectRelation);
  if (ph4 == 0 || ph7 == 0) return 0;
  return (7.0f - 4.0f) / (float)(ph7 - ph4);
}

float calculateSlope710() {
  int ph4, ph7, ph10;
  bool isDirectRelation;
  loadCalibration(ph4, ph7, ph10, isDirectRelation);
  if (ph7 == 0 || ph10 == 0) return 0;
  return (10.0f - 7.0f) / (float)(ph10 - ph7);
}

// Color hex segun rango de pH
String getPHColor(float ph) {
  if (ph < 3.0f)  return "#f85149";
  if (ph < 5.0f)  return "#ff8c00";
  if (ph < 6.5f)  return "#d29922";
  if (ph < 7.5f)  return "#3fb950";
  if (ph < 9.0f)  return "#58a6ff";
  if (ph < 11.0f) return "#bc8cff";
  return "#f85149";
}

String getPHLabel(float ph) {
  if (ph < 3.0f)  return "MUY ACIDO";
  if (ph < 5.0f)  return "ACIDO";
  if (ph < 6.5f)  return "LIGERAMENTE ACIDO";
  if (ph < 7.5f)  return "NEUTRO";
  if (ph < 9.0f)  return "BASICO";
  if (ph < 11.0f) return "MUY BASICO";
  return "EXTREMO";
}

// JSON completo del estado del sensor
String buildSensorJson() {
  float ph = getpHValue();
  float conductivity = getConductivityPPM();
  float conductivityRaw = getConductivityValue();
  float temp = getTemperature();
  int battery = getBatteryPercent();
  
  String json = "{";
  json += "\"sensorValue\":"    + String((int)getSensorValue());
  json += ",\"pHValue\":"       + String(ph, 2);
  json += ",\"phColor\":\""     + getPHColor(ph)  + "\"";
  json += ",\"phLabel\":\""     + getPHLabel(ph)  + "\"";
  json += ",\"slope47\":"       + String(calculateSlope47(),  4);
  json += ",\"slope710\":"      + String(calculateSlope710(), 4);
  json += ",\"conductivityRaw\":" + String((int)conductivityRaw);
  json += ",\"conductivityPPM\":" + String(conductivity, 1);
  json += ",\"temperature\":"   + (isnan(temp) ? "null" : String(temp, 1));
  json += ",\"temperatureStr\":\"" + getTemperatureString() + "\"";
  json += ",\"battery\":"       + (battery < 0 ? "null" : String(battery));
  json += ",\"batteryStr\":\""  + getBatteryString() + "\"";
  json += ",\"uptime\":"        + String(millis() / 1000);
  json += "}";
  
  return json;
}

// ============================================================================
//  WEBSOCKET
// ============================================================================

void sendSensorDataToClient(AsyncWebSocketClient *client) {
  client->text(buildSensorJson());
}

void sendSensorData() {
  if (ws.count() > 0) ws.textAll(buildSensorJson());
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WS[%u] conectado desde %s\n",
                    client->id(), client->remoteIP().toString().c_str());
      sendSensorDataToClient(client);
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WS[%u] desconectado\n", client->id());
      break;
    default:
      break;
  }
}

// ============================================================================
//  HANDLERS API
// ============================================================================

void handleGetSlopes(AsyncWebServerRequest *request) {
  String json = "{\"slope47\":";
  json += String(calculateSlope47(), 4);
  json += ",\"slope710\":";
  json += String(calculateSlope710(), 4);
  json += "}";
  request->send(200, "application/json", json);
}

// ============================================================================
//  HANDLERS CALIBRACION
// ============================================================================

void handleCalibrar4(AsyncWebServerRequest *request) {
  int sensorValue = (int)getSensorValue();
  int ph4, ph7, ph10; bool isDirectRelation;
  loadCalibration(ph4, ph7, ph10, isDirectRelation);
  bool newRelation = detectRelation(sensorValue, ph7, ph10);
  saveCalibration(sensorValue, ph7, ph10, newRelation);
  request->send(200, "text/plain", String(sensorValue));
  Serial.printf("Cal pH4=%d rel=%s\n", sensorValue, newRelation ? "DIRECTA" : "INVERSA");
}

void handleCalibrar7(AsyncWebServerRequest *request) {
  int sensorValue = (int)getSensorValue();
  int ph4, ph7, ph10; bool isDirectRelation;
  loadCalibration(ph4, ph7, ph10, isDirectRelation);
  bool newRelation = detectRelation(ph4, sensorValue, ph10);
  saveCalibration(ph4, sensorValue, ph10, newRelation);
  request->send(200, "text/plain", String(sensorValue));
  Serial.printf("Cal pH7=%d rel=%s\n", sensorValue, newRelation ? "DIRECTA" : "INVERSA");
}

void handleCalibrar10(AsyncWebServerRequest *request) {
  int sensorValue = (int)getSensorValue();
  int ph4, ph7, ph10; bool isDirectRelation;
  loadCalibration(ph4, ph7, ph10, isDirectRelation);
  bool newRelation = detectRelation(ph4, ph7, sensorValue);
  saveCalibration(ph4, ph7, sensorValue, newRelation);
  request->send(200, "text/plain", String(sensorValue));
  Serial.printf("Cal pH10=%d rel=%s\n", sensorValue, newRelation ? "DIRECTA" : "INVERSA");
}

void handleCalibrarConductividad(AsyncWebServerRequest *request) {
  if (!request->hasParam("ppm")) {
    request->send(400, "application/json", "{\"error\":\"Falta parametro: ppm\"}");
    return;
  }
  
  float knownPPM = request->getParam("ppm")->value().toFloat();
  if (knownPPM <= 0) {
    request->send(400, "application/json", "{\"error\":\"Valor de ppm inválido\"}");
    return;
  }
  
  calibrateConductivity(knownPPM);
  
  String json = "{\"success\":true";
  json += ",\"rawValue\":" + String((int)getConductivityValue());
  json += ",\"calibratedPPM\":" + String(getConductivityPPM(), 1);
  json += ",\"referencePPM\":" + String(knownPPM, 1);
  json += "}";
  
  request->send(200, "application/json", json);
}

// ============================================================================
//  HTML PRINCIPAL (PROGMEM rawliteral)
// ============================================================================

static const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Sensor de pH y Conductividad</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;600;700&family=DM+Sans:wght@400;500;600&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg:        #0d1117;
      --surface:   #161b22;
      --surface-2: #21262d;
      --border:    #30363d;
      --accent:    #58a6ff;
      --green:     #3fb950;
      --yellow:    #d29922;
      --red:       #f85149;
      --purple:    #bc8cff;
      --text:      #e6edf3;
      --muted:     #7d8590;
      --font-mono: 'JetBrains Mono', monospace;
      --font-body: 'DM Sans', sans-serif;
      --r-sm: 6px; --r-md: 10px; --r-lg: 14px;
      --gap: 12px;
    }
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: var(--font-body);
      background: var(--bg);
      color: var(--text);
      min-height: 100vh;
      padding: 12px;
      font-size: 15px;
    }
    a { text-decoration: none; color: inherit; }
    .page { max-width: 540px; margin: 0 auto; display: flex; flex-direction: column; gap: var(--gap); }

    /* HEADER */
    .header {
      display: flex; align-items: center; justify-content: space-between;
      padding: 16px 18px;
      background: var(--surface); border: 1px solid var(--border); border-radius: var(--r-lg);
    }
    .header-title { font-family: var(--font-mono); font-size: 1.1em; font-weight: 700; letter-spacing: .05em; color: var(--accent); }
    .header-sub   { font-size: .78em; color: var(--muted); margin-top: 2px; }
    .header-right { font-family: var(--font-mono); font-size: .78em; color: var(--muted); text-align: right; }
    .dot-live {
      display: inline-block; width: 8px; height: 8px; border-radius: 50%;
      background: var(--green); box-shadow: 0 0 6px var(--green);
      animation: pulse 2s infinite; margin-right: 4px;
    }
    @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:.4} }

    /* CARD */
    .card { background: var(--surface); border: 1px solid var(--border); border-radius: var(--r-lg); overflow: hidden; }
    .card-header {
      padding: 13px 18px; border-bottom: 1px solid var(--border);
      display: flex; align-items: center; gap: 8px;
      font-family: var(--font-mono); font-size: .82em; font-weight: 600;
      text-transform: uppercase; letter-spacing: .08em; color: var(--muted);
    }
    .card-body { padding: 16px 18px; }

    /* PH DISPLAY */
    .ph-display {
      display: flex; flex-direction: column; align-items: center;
      padding: 28px 18px; gap: 10px;
    }
    .ph-label { font-family: var(--font-mono); font-size: .78em; font-weight: 600; text-transform: uppercase; letter-spacing: .1em; color: var(--muted); }
    .ph-value {
      font-family: var(--font-mono); font-size: 5em; font-weight: 700; line-height: 1;
      transition: color .4s ease;
    }
    .ph-badge {
      font-family: var(--font-mono); font-size: .72em; font-weight: 700;
      letter-spacing: .08em; text-transform: uppercase;
      padding: 5px 16px; border-radius: 100px; border: 1px solid;
      transition: color .4s, border-color .4s, background .4s;
    }
    .ph-raw { font-family: var(--font-mono); font-size: .8em; color: var(--muted); }

    /* PENDIENTES */
    .slopes-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin-top: 4px; }
    .slope-tile {
      background: var(--surface-2); border: 1px solid var(--border);
      border-radius: var(--r-sm); padding: 12px;
    }
    .slope-tile-label { font-size: .72em; color: var(--muted); margin-bottom: 4px; font-family: var(--font-mono); }
    .slope-tile-val   { font-family: var(--font-mono); font-weight: 700; font-size: .92em; }

    /* BOTONES */
    .btn {
      display: flex; align-items: center; justify-content: center; gap: 8px;
      padding: 13px 18px; border-radius: var(--r-md);
      border: 1px solid transparent;
      font-family: var(--font-mono); font-size: .8em; font-weight: 700;
      letter-spacing: .04em; text-transform: uppercase; cursor: pointer;
      transition: filter .15s, transform .1s; background: none; width: 100%;
    }
    .btn:active { transform: scale(.97); }
    .btn:hover  { filter: brightness(1.2); }
    .btn-icon   { font-size: 1.15em; }

    .btn-green  { background: #1a3a22; color: var(--green);  border-color: var(--green); }
    .btn-yellow { background: #3d2e00; color: var(--yellow); border-color: var(--yellow); }
    .btn-blue   { background: #0d2340; color: var(--accent); border-color: var(--accent); }
    .btn-accent { background: #0d2340; color: var(--accent); border-color: var(--accent); }
    .btn-purple { background: #2a1f3d; color: var(--purple); border-color: var(--purple); }
    .btn-muted  { background: var(--surface-2); color: var(--muted); border-color: var(--border); }

    /* calibration button colors (text + border only) */
    .btn-cal4  { color: #ff69b4; border-color: #ff69b4; text-transform: none; }
    .btn-cal7  { color: #ffd700; border-color: #ffd700; text-transform: none; }
    .btn-cal10 { color: #4169e1; border-color: #4169e1; text-transform: none; }

    .btn-grid-3 { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 8px; }
    /* make calibration buttons smaller so they fit on mobile screens */
    .btn-grid-3 .btn {
      width: auto;               /* allow buttons to shrink inside grid cells */
      padding: 8px 10px;         /* reduce padding to avoid overflow */
      font-size: .75em;          /* smaller text for narrow columns */
    }

    /* CAL CIRCLES */
    .cal-dot { display: inline-block; width: 10px; height: 10px; border-radius: 50%; margin-right: 4px; }
    .cal-dot-4  { background: #ff69b4; }
    .cal-dot-7  { background: #ffd700; }
    .cal-dot-10 { background: #4169e1; }

    /* CAL TILES */
    .cal-grid { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 8px; margin-bottom: 16px; }
    .cal-tile {
      background: var(--surface-2); border: 1px solid var(--border);
      border-radius: var(--r-sm); padding: 10px 8px; text-align: center;
    }
    .cal-tile-label { font-size: .7em; color: var(--muted); font-family: var(--font-mono); margin-bottom: 5px; }
    .cal-tile-val   { font-family: var(--font-mono); font-weight: 700; font-size: .88em; }

    /* INFO ROWS */
    .info-row {
      display: flex; justify-content: space-between; align-items: center;
      padding: 10px 0; border-bottom: 1px solid var(--border); font-size: .9em;
    }
    .info-row:last-child { border-bottom: none; }
    .info-label { color: var(--muted); }
    .info-value { font-family: var(--font-mono); font-weight: 600; text-align: right; }

    /* INSTRUCTIONS */
    .instructions {
      background: var(--surface-2); border: 1px solid var(--border);
      border-left: 3px solid var(--accent);
      border-radius: var(--r-sm); padding: 14px 16px;
      font-size: .82em; color: var(--muted); line-height: 1.9; margin-top: 14px;
    }
    .instructions strong { display: block; color: var(--text); margin-bottom: 6px; font-family: var(--font-mono); }

    /* FOOTER */
    .footer { text-align: center; font-family: var(--font-mono); font-size: .73em; color: var(--muted); padding: 8px 0 4px; }
  </style>
</head>
<body>
<div class="page">

  <!-- HEADER -->
  <div class="header">
    <div>
      <div class="header-title">⬡ Sensor de pH</div>
      <div class="header-sub">Medidor de pH — ESP32</div>
    </div>
    <div class="header-right">
      <span class="dot-live"></span>LIVE<br>
      <span id="uptime">--</span>
    </div>
  </div>

  <!-- ESTADO DEL SISTEMA -->
  <div class="card">
    <div class="card-header"><span>🔋</span> Estado del Sistema</div>
    <div class="card-body">
      <div class="info-row">
        <span class="info-label">Temperatura</span>
        <span id="temperatureValue" class="info-value">--</span>
      </div>
      <div class="info-row">
        <span class="info-label">Batería</span>
        <span id="batteryValue" class="info-value">--</span>
      </div>
    </div>
  </div>

  <!-- PH EN TIEMPO REAL -->
  <div class="card">
    <div class="card-header"><span>📡</span> Lectura de pH</div>
    <div class="ph-display">
      <div class="ph-label">Valor de pH</div>
      <div id="phValue" class="ph-value" style="color:#3fb950;">7.00</div>
      <div id="phBadge" class="ph-badge" style="color:#3fb950; border-color:#3fb950; background:#1a3a2222;">NEUTRO</div>
      <div class="ph-raw">ADC bruto: <span id="sensorValue">0</span></div>
    </div>
  </div>

  <!-- CONDUCTIVIDAD EN TIEMPO REAL -->
  <div class="card">
    <div class="card-header"><span>⚡</span> Lectura de Conductividad</div>
    <div class="ph-display">
      <div class="ph-label">Conductividad</div>
      <div id="condValue" class="ph-value" style="color:#58a6ff;">0.0</div>
      <div class="ph-badge" style="color:#58a6ff; border-color:#58a6ff; background:#58a6ff22;">PPM</div>
      <div class="ph-raw">ADC bruto: <span id="condSensorValue">0</span></div>
      
      <!-- Calibración de conductividad -->
      <div style="margin-top:20px; width:100%; max-width:400px;">
        <input type="number" id="condPpmInput" placeholder="Concentración conocida (ppm)" 
               style="width:100%; padding:10px 12px; background:var(--surface-2); 
                      border:1px solid var(--border); border-radius:var(--r-sm); 
                      color:var(--text); font-family:var(--font-body); font-size:.85em;"
               step="0.1" min="0">
        <button class="btn btn-accent" onclick="calibrateConductivity()" style="margin-top:8px; width:100%;">
          <span class="btn-icon">⚙️</span>CALIBRAR CONDUCTIVIDAD
        </button>
      </div>
    </div>
  </div>

  <!-- CALIBRACION -->
  <div class="card">
    <div class="card-header"><span>⚙️</span> Calibracion</div>
    <div class="card-body">

      <div class="info-row" style="margin-bottom:14px;">
        <span class="info-label">Valores de Calibración</span>
        <span class="info-value">—</span>
      </div>

      <div class="cal-grid">
        <div class="cal-tile">
          <div class="cal-tile-label"><span class="cal-dot cal-dot-4"></span>pH 4</div>
          <div id="calVal4" class="cal-tile-val" style="color:#ff69b4;">—</div>
        </div>
        <div class="cal-tile">
          <div class="cal-tile-label"><span class="cal-dot cal-dot-7"></span>pH 7</div>
          <div id="calVal7" class="cal-tile-val" style="color:#ffd700;">—</div>
        </div>
        <div class="cal-tile">
          <div class="cal-tile-label"><span class="cal-dot cal-dot-10"></span>pH 10</div>
          <div id="calVal10" class="cal-tile-val" style="color:#4169e1;">—</div>
        </div>
      </div>

      <div class="info-row" style="margin-bottom:14px; margin-top:14px;">
        <span class="info-label">Pendientes</span>
        <span class="info-value">—</span>
      </div>

      <div class="slopes-grid">
        <div class="slope-tile">
          <div class="slope-tile-label">Pendiente pH 4–7</div>
          <div id="slope47" class="slope-tile-val" style="color:var(--accent);">—</div>
        </div>
        <div class="slope-tile">
          <div class="slope-tile-label">Pendiente pH 7–10</div>
          <div id="slope710" class="slope-tile-val" style="color:var(--purple);">—</div>
        </div>
      </div>

      <div class="info-row" style="margin-bottom:14px; margin-top:14px;">
        <span class="info-label">Lectura ADC actual</span>
        <span class="info-value"><span id="adcForCal">0</span></span>
      </div>

      <div class="btn-grid-3">
        <button id="btnCal4"  class="btn btn-cal4" onclick="calibrar('4')">
          Calibrar pH 4
        </button>
        <button id="btnCal7"  class="btn btn-cal7" onclick="calibrar('7')">
          Calibrar pH 7
        </button>
        <button id="btnCal10" class="btn btn-cal10" onclick="calibrar('10')">
          Calibrar pH 10
        </button>
      </div>

      <div class="instructions">
        <strong>Instrucciones de Calibracion</strong>
        1. Sumerge el sensor en buffer pH 4<br>
        2. Espera a que el ADC se estabilice<br>
        3. Presiona "CALIBRAR pH 4"<br>
        4. Lava el electrodo entre mediciones<br>
        5. Repite con pH 7 y pH 10
      </div>
    </div>
  </div>

  <!-- SISTEMA -->
  <div class="card">
    <div class="card-header"><span>ℹ️</span> Sistema</div>
    <div class="card-body">
      <div class="info-row">
        <span class="info-label">Uptime</span>
        <span id="sysUptime" class="info-value">—</span>
      </div>
      <div class="info-row">
        <span class="info-label">Clientes WebSocket</span>
        <span id="wsClients" class="info-value">—</span>
      </div>
      <a href="/update" class="btn btn-yellow" style="margin-top:14px;">
        <span class="btn-icon">🔄</span>ACTUALIZAR FIRMWARE (OTA)
      </a>
      <a href="/logger" class="btn btn-purple" style="margin-top:8px;">
        <span class="btn-icon">📂</span>ADMINISTRAR LOGS
      </a>
      <a href="/mqtt" class="btn btn-green" style="margin-top:8px;">
        <span class="btn-icon">🌐</span>CONFIGURAR MQTT
      </a>
    </div>
  </div>

  <div class="footer"><span class="dot-live"></span>Sensor de pH — ESP32</div>

</div><!-- /page -->

<script>
(function(){
  var wsConn, retry = 2000, connected = false;

  function initWS() {
    try {
      wsConn = new WebSocket('ws://'+location.host+'/ws');
      
      wsConn.onopen = ()=> {
        console.log('WS conectado');
        connected = true;
        document.querySelector('.dot-live').style.background = '#3fb950';
      };
      
      wsConn.onclose = ()=> {
        console.log('WS cerrado'); 
        connected = false;
        document.querySelector('.dot-live').style.background = '#7d8590';
        setTimeout(initWS, retry);
      };
      
      wsConn.onerror = (e)=> {
        console.error('WS error',e); 
        connected = false;
      };
      
      wsConn.onmessage = (e)=> {
        try {
          const d = JSON.parse(e.data);
          
          // Debug temporal: mostrar datos recibidos cada 10 segundos
          if (!window.lastDebugTime || Date.now() - window.lastDebugTime > 10000) {
            window.lastDebugTime = Date.now();
            console.log('[WS] Datos recibidos:', {
              temperature: d.temperature,
              temperatureStr: d.temperatureStr,
              battery: d.battery, 
              batteryStr: d.batteryStr
            });
          }
          
          // Valores principales
          const el_ph = document.getElementById('phValue');
          const el_badge = document.getElementById('phBadge');
          const el_sensor = document.getElementById('sensorValue');
          const el_cond = document.getElementById('condValue');
          const el_condSensor = document.getElementById('condSensorValue');
          const el_uptime = document.getElementById('uptime');
          const el_sysUptime = document.getElementById('sysUptime');
          const el_adcForCal = document.getElementById('adcForCal');

          // Nuevos elementos para temperatura y batería
          const el_temperature = document.getElementById('temperatureValue');
          const el_battery = document.getElementById('batteryValue');

          if(el_ph) { el_ph.textContent = parseFloat(d.pHValue).toFixed(2); el_ph.style.color = d.phColor; }
          if(el_badge) { el_badge.textContent = d.phLabel; el_badge.style.color = d.phColor; el_badge.style.borderColor = d.phColor; el_badge.style.background = d.phColor+'22'; }
          if(el_sensor) el_sensor.textContent = d.sensorValue;
          if(el_cond) el_cond.textContent = parseFloat(d.conductivityPPM).toFixed(1);
          if(el_condSensor) el_condSensor.textContent = d.conductivityRaw;
          if(el_adcForCal) el_adcForCal.textContent = d.sensorValue;

          // Actualizar temperatura y batería
          if(el_temperature) {
            el_temperature.textContent = d.temperatureStr;
            // Debug temporal
            if (!window.lastTempDebug || Date.now() - window.lastTempDebug > 10000) {
              window.lastTempDebug = Date.now();
              console.log('[TEMP] Elemento encontrado:', el_temperature, 'Valor:', d.temperatureStr);
            }
          }
          if(el_battery) {
            el_battery.textContent = d.batteryStr;
            // Debug temporal  
            if (!window.lastBatDebug || Date.now() - window.lastBatDebug > 10000) {
              window.lastBatDebug = Date.now();
              console.log('[BAT] Elemento encontrado:', el_battery, 'Valor:', d.batteryStr);
            }
          }
          
          // Pendientes de calibración
          const s47  = document.getElementById('slope47');
          const s710 = document.getElementById('slope710');
          if (s47)  s47.textContent  = d.slope47  != 0 ? parseFloat(d.slope47).toFixed(4)  : '—';
          if (s710) s710.textContent = d.slope710 != 0 ? parseFloat(d.slope710).toFixed(4) : '—';
          
          // Uptime
          const uptimeStr = formatUptime(d.uptime);
          if(el_uptime) el_uptime.textContent = uptimeStr;
          if(el_sysUptime) el_sysUptime.textContent = uptimeStr;
          
        } catch(err) { console.error('JSON parse error:', err); }
      };
      
    } catch(ex) { console.error('initWS error:', ex); setTimeout(initWS, retry); }
  }

  function formatUptime(sec) {
    const d = Math.floor(sec / 86400);
    const h = Math.floor((sec % 86400) / 3600);  
    const m = Math.floor((sec % 3600) / 60);
    if (d > 0) return d + 'd ' + h + 'h ' + m + 'm';
    if (h > 0) return h + 'h ' + m + 'm';
    return m + 'm ' + (sec % 60) + 's';
  }

  // ── WebSocket ────────────────────────────────────────────────────────────
  function initWS() {
    wsConn = new WebSocket('ws://' + location.hostname + '/ws');
    wsConn.onopen    = () => { connected = true; };
    wsConn.onclose   = () => { connected = false; setTimeout(initWS, retry); };
    wsConn.onerror   = () => { connected = false; };
    wsConn.onmessage = (e) => { try { updateUI(JSON.parse(e.data)); } catch(err) { console.error('WebSocket parse error:', err); } };
  }

  // ── Funciones de formateo ──────────────────────────────────────────────────
  function fmtTime(seconds) {
    if (!seconds || seconds < 0) return "0s";
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    
    if (h > 0) return `${h}h ${m}m ${s}s`;
    if (m > 0) return `${m}m ${s}s`;
    return `${s}s`;
  }

  // ── Actualizar toda la UI con datos del WebSocket ──────────────────────
  function updateUI(d) {
    // pH principal
    const phEl    = document.getElementById('phValue');
    const badgeEl = document.getElementById('phBadge');
    if (phEl)    { phEl.textContent = parseFloat(d.pHValue).toFixed(2); phEl.style.color = d.phColor; }
    if (badgeEl) {
      badgeEl.textContent      = d.phLabel;
      badgeEl.style.color      = d.phColor;
      badgeEl.style.borderColor= d.phColor;
      badgeEl.style.background = d.phColor + '22';
    }
    // ADC
    const sv = document.getElementById('sensorValue');  if(sv) sv.textContent = d.sensorValue;
    const ac = document.getElementById('adcForCal');    if(ac) ac.textContent = d.sensorValue;
    // Uptime
    const ut  = document.getElementById('uptime');      if(ut)  ut.textContent  = fmtTime(d.uptime);
    const sut = document.getElementById('sysUptime');   if(sut) sut.textContent = fmtTime(d.uptime);
    // Pendientes
    const s47  = document.getElementById('slope47');
    const s710 = document.getElementById('slope710');
    if (s47)  s47.textContent  = d.slope47  != 0 ? parseFloat(d.slope47).toFixed(4)  : '—';
    if (s710) s710.textContent = d.slope710 != 0 ? parseFloat(d.slope710).toFixed(4) : '—';

    // Actualizar datos de conductividad
    const condValue = document.getElementById('condValue');
    const condSensorValue = document.getElementById('condSensorValue');
    if (condValue) condValue.textContent = parseFloat(d.conductivityPPM).toFixed(1);
    if (condSensorValue) condSensorValue.textContent = d.conductivityRaw;

    // --- actualizar temperatura y batería ---
    const tempEl = document.getElementById('temperatureValue');
    const batEl  = document.getElementById('batteryValue');
    if(tempEl) tempEl.textContent = d.temperatureStr || 'NC';
    if(batEl)  batEl.textContent  = d.batteryStr || 'NC';
  }

  // ── Calibrar Conductividad ─────────────────────────────────────────────────
  window.calibrateConductivity = function() {
    const ppmInput = document.getElementById('condPpmInput');
    const ppmValue = parseFloat(ppmInput.value);
    
    if (isNaN(ppmValue) || ppmValue <= 0) {
      alert('Por favor, ingresa un valor de PPM válido mayor a 0');
      return;
    }
    
    const confirmMsg = 'Calibrar conductividad con ' + ppmValue + ' ppm?';
    if (!confirm(confirmMsg)) return;
    
    fetch('/calibrar-conductividad?ppm=' + encodeURIComponent(ppmValue))
      .then(r => r.json())
      .then(d => {
        if (d.success) {
          alert('Calibración exitosa!\n\nValor ADC: ' + d.rawValue + 
                '\nConcentración: ' + d.calibratedPPM + ' ppm\nReferencia: ' + d.referencePPM + ' ppm');
          ppmInput.value = ''; // Limpiar input
        } else {
          alert('Error en calibración: ' + (d.error || 'Desconocido'));
        }
      })
      .catch(e => {
        alert('Error de conexión: ' + e);
      });
  };

  // ── Calibrar pH ──────────────────────────────────────────────────────────
  window.calibrar = function(pH) {
    if (!connected) { alert('Sin conexion con el ESP32'); return; }
    const lbl={'4':'pH 4','7':'pH 7','10':'pH 10'};
    if (!confirm('El sensor esta en solucion '+lbl[pH]+'?')) return;
    const btn=document.getElementById('btnCal'+pH), orig=btn.innerHTML;
    btn.innerHTML='Calibrando...'; btn.disabled=true;
    fetch('/calibrar'+pH)
      .then(r=>r.text())
      .then(val=>{
        const tile=document.getElementById('calVal'+pH); if(tile) tile.textContent=val;
        alert('Calibracion '+lbl[pH]+' guardada! ADC: '+val);
        btn.innerHTML=orig; btn.disabled=false;
      })
      .catch(e=>{alert('Error: '+e); btn.innerHTML=orig; btn.disabled=false;});
  };

  // ── Cargar valores de calibracion al inicio ──────────────────────────────
  function loadCalValues() {
    fetch('/api/get-slopes').then(r=>r.json())
      .then(d=>{
        const s47=document.getElementById('slope47');   if(s47&&d.slope47!=0)  s47.textContent=parseFloat(d.slope47).toFixed(4);
        const s710=document.getElementById('slope710'); if(s710&&d.slope710!=0) s710.textContent=parseFloat(d.slope710).toFixed(4);
      }).catch(()=>{});
  }

  // ── Arranque ─────────────────────────────────────────────────────────────
  window.addEventListener('load', ()=>{
    loadCalValues();
    initWS();
  });
})();
</script>
</body>
</html>
)rawliteral";

// ============================================================================
//  handleRoot
// ============================================================================

void handleRoot(AsyncWebServerRequest *request) {
  request->send(200, "text/html", PAGE_HTML);
}

// ============================================================================
//  initOTA — llamar desde setup() despues de registrar todas las rutas
// ============================================================================

void initOTA() {
  ElegantOTA.begin(&server);
  Serial.println("OTA habilitado en /update");
}

// ============================================================================
//  MQTT CONFIG HANDLERS
// ============================================================================

void handleMqttConfig(AsyncWebServerRequest *request) {
  Preferences p;
  p.begin("mqttcfg", /*readOnly=*/true);
  String broker   = p.getString("broker",   "broker.hivemq.com");
  int    port     = p.getInt   ("port",     1883);
  String user     = p.getString("user",     "");
  String clientid = p.getString("clientid", "estacion-agua");
  String topic    = p.getString("topic",    "estacion/agua");
#if defined(USE_WIFI)
  String ssid     = p.getString("ssid",     "");
#elif defined(USE_SIM800)
  String apn      = p.getString("apn",      "");
  String apnuser  = p.getString("apnuser",  "");
#endif
  p.end();

  String html = "<!DOCTYPE html><html lang='es'><head>"
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Configurar MQTT</title>"
    "<style>"
    "body{background:#0d1117;color:#e6edf3;font-family:'DM Sans',sans-serif;margin:0;padding:20px;}"
    "h2{color:#58a6ff;margin-bottom:20px;}label{display:block;margin-bottom:4px;color:#7d8590;font-size:.85rem;}"
    "input{width:100%;box-sizing:border-box;background:#161b22;border:1px solid #30363d;color:#e6edf3;"
    "border-radius:6px;padding:8px 10px;font-size:.95rem;margin-bottom:14px;}"
    "input:focus{outline:none;border-color:#58a6ff;}"
    ".section{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:16px;margin-bottom:16px;}"
    ".section-title{font-size:.8rem;font-weight:600;color:#58a6ff;text-transform:uppercase;"
    "letter-spacing:.06em;margin-bottom:12px;}"
    ".btn-save{background:#3fb950;border:none;color:#0d1117;font-weight:700;padding:10px 24px;"
    "border-radius:6px;cursor:pointer;font-size:.95rem;margin-right:10px;}"
    ".btn-back{background:transparent;border:1px solid #30363d;color:#7d8590;padding:10px 20px;"
    "border-radius:6px;text-decoration:none;font-size:.95rem;}"
    ".note{color:#7d8590;font-size:.8rem;margin-bottom:14px;}"
    "</style></head><body>"
    "<h2>&#127760; Configurar MQTT</h2>"
    "<form action='/mqtt/save' method='get'>";

#if defined(USE_WIFI)
  html += "<div class='section'><div class='section-title'>Red WiFi</div>"
    "<label>SSID</label><input name='ssid' value='" + ssid + "'>"
    "<label>Contrase&ntilde;a WiFi <span class='note'>(dejar en blanco para no cambiar)</span></label>"
    "<input name='wifipass' type='password' placeholder='••••••••'>"
    "</div>";
#elif defined(USE_SIM800)
  html += "<div class='section'><div class='section-title'>SIM800 / GPRS</div>"
    "<label>APN</label><input name='apn' value='" + apn + "'>"
    "<label>Usuario APN</label><input name='apnuser' value='" + apnuser + "'>"
    "<label>Contrase&ntilde;a APN <span class='note'>(dejar en blanco para no cambiar)</span></label>"
    "<input name='apnpass' type='password' placeholder='••••••••'>"
    "</div>";
#endif

  html += "<div class='section'><div class='section-title'>Broker MQTT</div>"
    "<label>Host del broker</label><input name='broker' value='" + broker + "'>"
    "<label>Puerto</label><input name='port' type='number' min='1' max='65535' value='" + String(port) + "'>"
    "<label>Usuario (opcional)</label><input name='user' value='" + user + "'>"
    "<label>Contrase&ntilde;a <span class='note'>(dejar en blanco para no cambiar)</span></label>"
    "<input name='pass' type='password' placeholder='••••••••'>"
    "</div>"
    "<div class='section'><div class='section-title'>Identificaci&oacute;n</div>"
    "<label>Client ID</label><input name='clientid' value='" + clientid + "'>"
    "<label>Prefijo de t&oacute;pico</label><input name='topic' value='" + topic + "'>"
    "<p class='note'>Los datos se publicar&aacute;n en <strong>&lt;prefijo&gt;/data</strong></p>"
    "</div>"
    "<button type='submit' class='btn-save'>GUARDAR</button>"
    "<a href='/' class='btn-back'>VOLVER</a>"
    "</form></body></html>";

  request->send(200, "text/html", html);
}

void handleMqttSave(AsyncWebServerRequest *request) {
  Preferences p;
  p.begin("mqttcfg", /*readOnly=*/false);

  if (request->hasParam("broker"))   p.putString("broker",   request->getParam("broker")->value());
  if (request->hasParam("port")) {
    int port = request->getParam("port")->value().toInt();
    if (port > 0 && port <= 65535) p.putInt("port", port);
  }
  if (request->hasParam("user"))     p.putString("user",     request->getParam("user")->value());
  if (request->hasParam("pass") && request->getParam("pass")->value().length() > 0)
    p.putString("pass",     request->getParam("pass")->value());
  if (request->hasParam("clientid")) p.putString("clientid", request->getParam("clientid")->value());
  if (request->hasParam("topic"))    p.putString("topic",    request->getParam("topic")->value());

#if defined(USE_WIFI)
  if (request->hasParam("ssid"))     p.putString("ssid",     request->getParam("ssid")->value());
  if (request->hasParam("wifipass") && request->getParam("wifipass")->value().length() > 0)
    p.putString("wifipass", request->getParam("wifipass")->value());
#elif defined(USE_SIM800)
  if (request->hasParam("apn"))      p.putString("apn",      request->getParam("apn")->value());
  if (request->hasParam("apnuser"))  p.putString("apnuser",  request->getParam("apnuser")->value());
  if (request->hasParam("apnpass") && request->getParam("apnpass")->value().length() > 0)
    p.putString("apnpass",  request->getParam("apnpass")->value());
#endif

  p.end();
  Serial.println("[MQTT] Configuración guardada en NVS");
  request->redirect("/mqtt");
}

#endif // WEBHANDLERS_H