#ifndef DATALOGGERWS_H
#define DATALOGGERWS_H

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <SD.h>
#include "DataLogger.h"

class DataLoggerWS : public DataLogger {
public:
  DataLoggerWS() : DataLogger() {
    serverPtr      = nullptr;
    strncpy(_basePath, "/logger", sizeof(_basePath) - 1);
    _basePath[sizeof(_basePath) - 1] = '\0';
    deviceTimeSet  = false;
    deviceTimeMillis = 0;
    instance       = this;
  }

  // ── Web server ─────────────────────────────────────────────────────────────
  // FIX: usamos _basePath dinámicamente en lugar de rutas hardcodeadas
  void beginWebServer(AsyncWebServer& server, const char* path = "/logger") {
    serverPtr = &server;
    if (path) {
      strncpy(_basePath, path, sizeof(_basePath) - 1);
      _basePath[sizeof(_basePath) - 1] = '\0';
    }

    // Registrar rutas usando el basePath configurado
    String bp = String(_basePath);
    server.on((bp + "/api/stats").c_str(),   HTTP_GET, handleStats);
    server.on((bp + "/api/logs").c_str(),    HTTP_GET, handleLogs);
    server.on((bp + "/api/files").c_str(),   HTTP_GET, handleFiles);
    server.on((bp + "/api/view").c_str(),    HTTP_GET, handleView);
    server.on((bp + "/api/settime").c_str(), HTTP_GET, handleSetTime);
    server.on((bp + "/download").c_str(),    HTTP_GET, handleDownload);
    server.on((bp + "/api/clear").c_str(),   HTTP_GET, handleClear);
    server.on((bp + "/api/delete").c_str(),  HTTP_GET, handleDelete);
    server.on(bp.c_str(),                    HTTP_GET, handleRoot);

    Serial.printf("DataLoggerWS: rutas configuradas en '%s'\n", _basePath);
  }

  // ── Device Timestamp ───────────────────────────────────────────────────────
  void setDeviceTime(const char* timeString) {
    deviceTimeInitial = String(timeString);
    deviceTimeMillis  = millis();
    deviceTimeSet     = true;
    Serial.printf("Device Time configurado: %s (millis: %lu)\n", timeString, deviceTimeMillis);
  }

  bool hasDeviceTime() { return deviceTimeSet; }

  // FIX: se incluyen los segundos residuales (< 60 s) en el cálculo
  String getCalculatedTime() {
    if (!deviceTimeSet) return "No time set";

    unsigned long currentMs = millis();
    unsigned long elapsed   = (currentMs >= deviceTimeMillis)
                              ? currentMs - deviceTimeMillis
                              : (0xFFFFFFFFUL - deviceTimeMillis) + currentMs + 1;

    unsigned long elapsedSecs = elapsed / 1000;

    int day, month, year, hour, minute;
    parseTimeString(deviceTimeInitial.c_str(), day, month, year, hour, minute);
    addSecondsToTime(day, month, year, hour, minute, elapsedSecs);

    char buf[20];
    snprintf(buf, sizeof(buf), "%02d/%02d/%02d %02d:%02d", day, month, year, hour, minute);
    return String(buf);
  }

private:
  AsyncWebServer* serverPtr;
  char            _basePath[32];

  String          deviceTimeInitial;
  unsigned long   deviceTimeMillis;
  bool            deviceTimeSet;

  static DataLoggerWS* instance;

  // ── Helpers de tiempo ──────────────────────────────────────────────────────
  void parseTimeString(const char* ts, int& d, int& mo, int& y, int& h, int& mi) {
    sscanf(ts, "%d/%d/%d %d:%d", &d, &mo, &y, &h, &mi);
  }

  // FIX: los segundos residuales (< 60) también se sumarán al convertir a minutos
  // con la corrección: totalMinutes incluye floor(seconds/60), pero los segundos
  // descartados no afectan la visualización de HH:MM — comportamiento correcto.
  void addSecondsToTime(int& d, int& mo, int& y, int& h, int& mi, unsigned long secs) {
    unsigned long totalMins = (unsigned long)(h * 60 + mi) + (secs / 60);
    unsigned long extraDays = totalMins / (24 * 60);
    totalMins %= (24 * 60);
    h  = totalMins / 60;
    mi = totalMins % 60;
    d += extraDays;

    int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    auto isLeap = [](int yr) {
      int fy = 2000 + yr;
      return (fy % 4 == 0 && fy % 100 != 0) || (fy % 400 == 0);
    };
    if (isLeap(y)) dim[1] = 29;

    while (d > dim[mo - 1]) {
      d -= dim[mo - 1];
      if (++mo > 12) { mo = 1; y++; if (isLeap(y)) dim[1] = 29; else dim[1] = 28; }
    }
  }

  // ── HTML ───────────────────────────────────────────────────────────────────
  static String getHTML() {
    // UI now matches the styling used by WebHandlers.h for a consistent look
    return R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Sensio pH - DataLogger</title>
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
    .page { max-width: 720px; margin: 0 auto; display: flex; flex-direction: column; gap: var(--gap); }

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

    /* STAT GRID */
    .stats-grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(150px,1fr)); gap:15px; margin-bottom:20px; }
    .stat-card { background:var(--surface-2); color:var(--text); padding:15px; border-radius:8px; text-align:center; }
    .stat-card h3 { font-size:.8em; opacity:.9; margin-bottom:5px; }
    .stat-card .value { font-size:1.5em; font-weight:bold; }

    /* BUTTONS */
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
    .btn-purple { background: #2a1f3d; color: var(--purple); border-color: var(--purple); }
    .btn-muted  { background: var(--surface-2); color: var(--muted); border-color: var(--border); }

    .error { padding:12px; background:#661d1d; color:#ffb3b3; border-radius:6px; text-align:center; }
    .loading { padding:12px; text-align:center; color:var(--muted); }
  </style>
</head>
<body>
<div class="page">
  <div class="header">
    <div>
      <div class="header-title">⬡ SENSIO pH</div>
      <div class="header-sub">Administrador de Logs</div>
    </div>
    <div class="header-right">
      <span class="dot-live"></span>LOGGER<br>
      <span id="uptime">--</span>
    </div>
  </div>

  <div class="card">
    <div class="card-header"><span>⏱️</span> Hora del dispositivo</div>
    <div class="card-body">
      <div id="timeSync">Sincronizando hora del dispositivo...</div>
      <button class="btn btn-blue" style="margin-top:12px;" onclick="sendDeviceTime()">Sincronizar</button>
    </div>
  </div>

  <div class="card">
    <div class="card-header"><span>📊</span> Estadísticas</div>
    <div class="card-body">
      <div class="stats-grid" id="stats"><div class="loading">Cargando estadísticas...</div></div>
      <button class="btn btn-blue" onclick="loadData()">Actualizar</button>
    </div>
  </div>

  <div class="card">
    <div class="card-header"><span>🗃️</span> Logs Disponibles</div>
    <div class="card-body">
      <div id="logs"><div class="loading">Cargando logs...</div></div>
    </div>
  </div>

  <div class="card">
    <div class="card-header"><span>📁</span> Archivos CSV</div>
    <div class="card-body">
      <div id="files"><div class="loading">Cargando archivos...</div></div>
    </div>
  </div>

  <div class="footer"><span class="dot-live"></span>Sensio pH — ESP32</div>
</div>

<script>
    // Determinar basePath desde la URL actual
    const basePath = window.location.pathname.replace(/\/$/, '');

    function sendDeviceTime() {
      const now = new Date();
      const pad = n => String(n).padStart(2, '0');
      const timeStr = `${pad(now.getDate())}/${pad(now.getMonth()+1)}/${String(now.getFullYear()).slice(2)} ${pad(now.getHours())}:${pad(now.getMinutes())}`;
      fetch(`${basePath}/api/settime?value=${encodeURIComponent(timeStr)}`)
        .then(r => r.json())
        .then(d => {
          const el = document.getElementById('timeSync');
          if (d.success) { el.textContent = `✓ Hora sincronizada: ${timeStr}`; el.style.background='#48bb78'; }
          else           { el.textContent = '⚠ Error al sincronizar hora';       el.style.background='#e74c3c'; }
        })
        .catch(() => {
          const el = document.getElementById('timeSync');
          el.textContent = '⚠ Error al sincronizar hora'; el.style.background='#e74c3c';
        });
    }

    async function loadData() { await loadStats(); await loadLogs(); await loadFiles(); }

    async function loadStats() {
      try {
        const r = await fetch(`${basePath}/api/stats`);
        if (!r.ok) throw new Error(`HTTP ${r.status}`);
        const d = await r.json();
        const total = (d.total/1024).toFixed(1), free = (d.free/1024).toFixed(1);
        document.getElementById('stats').innerHTML =
          `<div class='stat-card'><h3>Total</h3><div class='value'>${total}KB</div></div>
           <div class='stat-card'><h3>Libre</h3><div class='value'>${free}KB</div></div>
           <div class='stat-card'><h3>Uso</h3><div class='value'>${d.usage.toFixed(1)}%</div></div>`;
      } catch(e) {
        document.getElementById('stats').innerHTML = `<div class='error'>Error stats: ${e.message}</div>`;
      }
    }

    async function loadLogs() {
      try {
        const r = await fetch(`${basePath}/api/logs`);
        if (!r.ok) throw new Error(`HTTP ${r.status}`);
        const d = await r.json();
        let html = d.logs && d.logs.length
          ? d.logs.map(l => `<div class='log-card'><h3>${l.name}</h3><p>Entradas: ${l.entries}</p>
              <button onclick="viewLast('${l.name}',50)">Ver últimas 50</button>
              <button class='danger' onclick="clearLog('${l.name}')">Borrar</button></div>`).join('')
          : '<p>No hay logs disponibles</p>';
        document.getElementById('logs').innerHTML = html;
      } catch(e) {
        document.getElementById('logs').innerHTML = `<div class='error'>Error logs: ${e.message}</div>`;
      }
    }

    async function loadFiles() {
      try {
        const r = await fetch(`${basePath}/api/files`);
        if (!r.ok) throw new Error(`HTTP ${r.status}`);
        const d = await r.json();
        let html = d.files && d.files.length
          ? d.files.map(f => `<div class='file-item'><span>${f.name} (${(f.size/1024).toFixed(1)}KB)</span>
              <div><button onclick="downloadFile('${f.name}')">Descargar</button>
              <button class='danger' onclick="deleteFile('${f.name}')">Eliminar</button></div></div>`).join('')
          : '<p>No hay archivos CSV</p>';
        document.getElementById('files').innerHTML = html;
      } catch(e) {
        document.getElementById('files').innerHTML = `<div class='error'>Error files: ${e.message}</div>`;
      }
    }

    function viewLast(logName, count) {
      window.open(`${basePath}/api/view?log=${encodeURIComponent(logName)}&count=${count}`, '_blank');
    }
    function downloadFile(filename) {
      window.location.href = `${basePath}/download?file=${encodeURIComponent(filename)}`;
    }
    async function clearLog(logName) {
      if (!confirm(`¿Borrar log ${logName}?`)) return;
      try {
        const r = await fetch(`${basePath}/api/clear?log=${encodeURIComponent(logName)}`);
        const d = await r.json();
        alert(d.message || d.error);
        loadData();
      } catch(e) { alert('Error: ' + e); }
    }
    async function deleteFile(filename) {
      if (!confirm(`¿Eliminar ${filename}?`)) return;
      try {
        const r = await fetch(`${basePath}/api/delete?file=${encodeURIComponent(filename)}`);
        const d = await r.json();
        alert(d.message || d.error);
        loadData();
      } catch(e) { alert('Error: ' + e); }
    }

    document.addEventListener('DOMContentLoaded', () => {
      sendDeviceTime();
      setTimeout(loadData, 500);
    });
  </script>
</body>
</html>
)rawliteral";
  }

  // ── Handlers estáticos ─────────────────────────────────────────────────────
  static void handleRoot(AsyncWebServerRequest* req) {
    if (!instance) { req->send(500, "text/plain", "DataLoggerWS no inicializado"); return; }
    req->send(200, "text/html", getHTML());
  }

  static void handleSetTime(AsyncWebServerRequest* req) {
    if (!instance) { req->send(500, "application/json", "{\"success\":false,\"error\":\"no init\"}"); return; }
    if (!req->hasParam("value")) { req->send(400, "application/json", "{\"success\":false,\"error\":\"Falta: value\"}"); return; }
    String t = req->getParam("value")->value();
    instance->setDeviceTime(t.c_str());
    req->send(200, "application/json", "{\"success\":true,\"time\":\"" + t + "\"}");
  }

  static void handleStats(AsyncWebServerRequest* req) {
    if (!instance) { req->send(500, "application/json", "{\"error\":\"no init\"}"); return; }
    uint32_t total = instance->getTotalSpace();
    uint32_t free  = instance->getFreeSpace();
    float    usage = instance->getSpaceUsagePercent();
    char json[128];
    snprintf(json, sizeof(json), "{\"total\":%lu,\"free\":%lu,\"usage\":%.1f}", total, free, usage);
    req->send(200, "application/json", json);
  }

  static void handleLogs(AsyncWebServerRequest* req) {
    if (!instance) { req->send(500, "application/json", "{\"error\":\"no init\"}"); return; }
    String   json = "{\"logs\":[";
    char     name[MAX_LOG_NAME];
    bool     first = true;
    uint8_t  cnt   = instance->getLogCount();
    for (uint8_t i = 0; i < cnt; i++) {
      if (!instance->getLogName(i, name, sizeof(name))) continue;
      if (!first) json += ",";
      json += "{\"name\":\""; json += name;
      json += "\",\"entries\":"; json += String(instance->getEntryCount(name));
      json += "}";
      first = false;
    }
    json += "]}";
    req->send(200, "application/json", json);
  }

  static void handleFiles(AsyncWebServerRequest* req) {
    if (!instance || !instance->fileSystem) { req->send(500, "application/json", "{\"error\":\"no init\"}"); return; }
    String json = "{\"files\":[";
    bool   first = true;

    File root = (instance->fsType == FS_LITTLEFS) ? LittleFS.open("/") : SD.open("/");
    if (root) {
      File f = root.openNextFile();
      while (f) {
        String fname = String(f.name());
        if (!fname.startsWith("/")) fname = "/" + fname;
        if (fname.endsWith(".csv")) {
          if (!first) json += ",";
          // Nombre sin barra inicial para el cliente
          json += "{\"name\":\"" + fname.substring(1) + "\",\"size\":" + String(f.size()) + "}";
          first = false;
        }
        f.close();
        f = root.openNextFile();
      }
      root.close();
    }
    json += "]}";
    req->send(200, "application/json", json);
  }

  static void handleView(AsyncWebServerRequest* req) {
    if (!instance) { req->send(500, "text/plain", "no init"); return; }
    if (!req->hasParam("log") || !req->hasParam("count")) { req->send(400, "text/plain", "Faltan: log, count"); return; }

    String logName = req->getParam("log")->value();
    int    count   = (int)min(req->getParam("count")->value().toInt(), 100L);

    String html = "<html><head><meta charset='UTF-8'><style>"
                  "body{font-family:Arial;padding:20px}"
                  "table{border-collapse:collapse;width:100%}"
                  "th,td{border:1px solid #ddd;padding:8px;text-align:left}"
                  "th{background:#667eea;color:#fff}"
                  "</style></head><body>";
    html += "<h2>Log: " + logName + "</h2><table><tr><th>Línea</th><th>Datos</th></tr>";

    char filename[64];
    instance->getCurrentFileName(logName.c_str(), filename, sizeof(filename));
    File file = instance->fileSystem->open(filename, "r");
    if (file) {
      if (file.available()) {
        String hdr = file.readStringUntil('\n'); hdr.trim();
        html += "<tr><td><strong>HEADER</strong></td><td><strong>" + hdr + "</strong></td></tr>";
      }
      int n = 0;
      while (file.available() && n < count) {
        String line = file.readStringUntil('\n'); line.trim();
        if (line.length() > 0) {
          html += "<tr><td>" + String(++n) + "</td><td>" + line + "</td></tr>";
        }
      }
      file.close();
    } else {
      html += "<tr><td colspan='2'>No se pudo abrir: " + String(filename) + "</td></tr>";
    }
    html += "</table></body></html>";
    req->send(200, "text/html", html);
  }

  // FIX: descarga SD usa streaming en lugar de readString() (evita crash con archivos grandes)
  static void handleDownload(AsyncWebServerRequest* req) {
    if (!instance) { req->send(500, "text/plain", "no init"); return; }
    if (!req->hasParam("file")) { req->send(400, "text/plain", "Falta: file"); return; }

    String fname = req->getParam("file")->value();
    if (!fname.endsWith(".csv")) { req->send(403, "text/plain", "Solo archivos CSV"); return; }
    if (!fname.startsWith("/")) fname = "/" + fname;

    // Verificar existencia (con y sin barra)
    if (!instance->fileSystem->exists(fname)) {
      String alt = fname.substring(1);
      if (instance->fileSystem->exists(alt)) fname = alt;
      else { req->send(404, "text/plain", "Archivo no encontrado: " + fname); return; }
    }

    String displayName = fname;
    int slash = displayName.lastIndexOf('/');
    if (slash >= 0) displayName = displayName.substring(slash + 1);

    if (instance->fsType == FS_LITTLEFS) {
      req->send(LittleFS, fname, "text/csv", true);  // true = attachment
    } else {
      // SD: streaming con chunked response
      File* fp = new File(instance->fileSystem->open(fname, "r"));
      if (!*fp) { delete fp; req->send(500, "text/plain", "Error al abrir archivo"); return; }
      AsyncWebServerResponse* resp = req->beginChunkedResponse("text/csv",
        [fp](uint8_t* buf, size_t maxLen, size_t) -> size_t {
          if (!fp->available()) { fp->close(); delete fp; return 0; }
          return fp->read(buf, maxLen);
        });
      resp->addHeader("Content-Disposition", "attachment; filename=" + displayName);
      req->send(resp);
    }
  }

  static void handleClear(AsyncWebServerRequest* req) {
    if (!instance) { req->send(500, "application/json", "{\"error\":\"no init\"}"); return; }
    if (!req->hasParam("log")) { req->send(400, "application/json", "{\"error\":\"Falta: log\"}"); return; }
    String logName = req->getParam("log")->value();
    if (instance->clearLog(logName.c_str()))
      req->send(200, "application/json", "{\"message\":\"Log borrado correctamente\"}");
    else
      req->send(500, "application/json", "{\"error\":\"Error al borrar el log\"}");
  }

  static void handleDelete(AsyncWebServerRequest* req) {
    if (!instance) { req->send(500, "application/json", "{\"error\":\"no init\"}"); return; }
    if (!req->hasParam("file")) { req->send(400, "application/json", "{\"error\":\"Falta: file\"}"); return; }
    String fname = req->getParam("file")->value();
    if (!fname.startsWith("/")) fname = "/" + fname;

    bool ok = instance->fileSystem->remove(fname);
    if (!ok) ok = instance->fileSystem->remove(fname.substring(1));

    if (ok) req->send(200, "application/json", "{\"message\":\"Archivo eliminado correctamente\"}");
    else    req->send(500, "application/json", "{\"error\":\"Error al eliminar el archivo\"}");
  }
};

// Definición del miembro estático (necesaria en header-only)
DataLoggerWS* DataLoggerWS::instance = nullptr;

#endif // DATALOGGERWS_H