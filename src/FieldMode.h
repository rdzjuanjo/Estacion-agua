// FieldMode.h - Lógica de medición en campo con deep-sleep y publicación MQTT
//
// Flujo:
//   1. Encender periféricos (MOSFET)
//   2. Esperar arranque SIM800 (si USA_SIM800)
//   3. Conectar a MQTT (max MQTT_RETRIES intentos)
//   4. Muestrear 30 s: 20 s warm-up + 10 s de datos
//   5. Calcular media y CV (coeficiente de variación) de cada variable
//   6. Publicar JSON con resultados
//   7. Apagar periféricos y entrar a deep-sleep (30 min)
#ifndef FIELDMODE_H
#define FIELDMODE_H

#include <Arduino.h>
#include <math.h>
#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_MAX1704X.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include "Config.h"
#include "leerph.h"
#include "TempSensor.h"
#include "MQTTManager.h"
#include "SimpleLogger.h"  // Reemplaza DataLogger pesado con SimpleLogger liviano

// ── Variables para detección del botón de boot en modo FIELD ─────────────────
const int PIN_BOOT_BUTTON = 0;   // BOOT en ESP32 DevKit
const unsigned long BOOT_HOLD_MS = 2500;

bool bootPressed = false;
unsigned long bootPressStart = 0;

// ── Configuración del modo persistente ───────────────────────────────────────
const char* PREF_NS_MODE     = "mode";
const char* PREF_KEY_ISFIELD = "is_field";

static void requestModeChangeToConfig() {
  Serial.println("[CAMPO] Botón de boot detectado - cambiando a modo CONFIG...");
  
  // Apagar periféricos antes del reinicio
  digitalWrite(PIN_MOSFET, LOW);
  
  // Guardar modo CONFIG
  Preferences modePrefs;
  modePrefs.begin(PREF_NS_MODE, false);
  modePrefs.putBool(PREF_KEY_ISFIELD, false);
  modePrefs.end();
  
  Serial.println("[CAMPO] Reiniciando para aplicar cambio...");
  Serial.flush();
  delay(50);
  ESP.restart();
}

static void handleBootButtonInField() {
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
    requestModeChangeToConfig();
  }
}

// ── Helpers estadísticos ──────────────────────────────────────────────────────

static float _mean(const float* buf, int n) {
    if (n == 0) return NAN;
    double sum = 0;
    for (int i = 0; i < n; i++) sum += buf[i];
    return (float)(sum / n);
}

static float _cv(const float* buf, int n) {
    if (n < 2) return 0.0f;
    float m = _mean(buf, n);
    if (m == 0.0f) return 0.0f;
    double sq = 0;
    for (int i = 0; i < n; i++) {
        double d = buf[i] - m;
        sq += d * d;
    }
    float stddev = (float)sqrt(sq / (n - 1));
    return (stddev / fabsf(m)) * 100.0f;  // CV en %
}

static Adafruit_MAX17048 _max17048;
static bool _max17048Ready = false;

static void _initBatteryGauge() {
    if (_max17048Ready) return;

    Serial.println("[CAMPO] Inicializando I2C para MAX17048...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    
    // Pequeño delay para asegurar que I2C esté listo
    delay(100);
    
    if (!_max17048.begin()) {
        Serial.println("[CAMPO] ERROR: MAX17048 no detectado en direcciones I2C");
        Serial.println("[CAMPO] Verificar conexiones SDA=21, SCL=22");
        return;
    }

    _max17048Ready = true;
    Serial.printf("[CAMPO] MAX17048 detectado correctamente (chip 0x%X)\n", _max17048.getChipID());
}

// Lee porcentaje de batería desde MAX17048 y devuelve 0-100.
// Si no está disponible, devuelve 0 para no bloquear el flujo en campo.
static int _readBatteryPct() {
    _initBatteryGauge();  
    if (!_max17048Ready) {
        Serial.println("[CAMPO] ERROR: MAX17048 no está listo, retornando batería 0%");
        return 0;
    }

    // Intentar múltiples lecturas con delays para dar tiempo al chip
    for (int attempt = 1; attempt <= 3; attempt++) {
        Serial.printf("[CAMPO] Intento %d de lectura batería...\n", attempt);
        
        float voltage = _max17048.cellVoltage();
        float pct = _max17048.cellPercent();
        
        Serial.printf("[CAMPO] Voltaje: %.3fV, Porcentaje: %.2f%%\n", voltage, pct);
        
        if (!isnan(pct) && pct > 0.0f) {
            int batteryPct = (int)constrain(pct, 0.0f, 100.0f);
            Serial.printf("[CAMPO] ✓ Batería válida en intento %d: %.2f%% -> %d%%\n", attempt, pct, batteryPct);
            return batteryPct;
        }
        
        Serial.printf("[CAMPO] ✗ Lectura inválida en intento %d (%.2f%%), reintentando...\n", attempt, pct);
        delay(500); // Pausa entre intentos
    }

    Serial.println("[CAMPO] ERROR: Todas las lecturas del MAX17048 fallaron, retornando 0%");
    return 0;
}

// ── Número máximo de muestras en la ventana de datos ─────────────────────────
// (SAMPLE_TOTAL_MS - SAMPLE_STABILIZE_MS) / SAMPLE_INTERVAL_MS = 100
#define MAX_SAMPLES  ((int)((SAMPLE_TOTAL_MS - SAMPLE_STABILIZE_MS) / SAMPLE_INTERVAL_MS) + 10)

// ── Arrays para muestreo (en memoria estática para evitar stack overflow) ───
static float _phBuf[MAX_SAMPLES];
static float _condBuf[MAX_SAMPLES];
static float _tempBuf[MAX_SAMPLES];

// ── Función principal del modo campo ─────────────────────────────────────────

void runFieldMode() {
    Serial.println("\n=== MODO CAMPO ===");
    Serial.flush();
    delay(100);

    // 0. Verificar causa del wake-up
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0:
            Serial.println("[CAMPO] Despertado por botón de boot - cambiando a modo CONFIG...");
            requestModeChangeToConfig();
            return;
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("[CAMPO] Despertado por timer (ciclo normal)");
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            Serial.println("[CAMPO] Inicio inicial (no deep sleep previo)");
            break;
    }

    // Configurar botón para detección durante muestreo y como wake-up source
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

    // 1. Encender periféricos
    pinMode(PIN_MOSFET, OUTPUT);
    digitalWrite(PIN_MOSFET, HIGH);
    Serial.println("[CAMPO] Periféricos encendidos");

    // 2. Pausa de arranque — solo necesaria con SIM800
#if defined(USE_SIM800)
    Serial.printf("[CAMPO] Pausa %lu ms para arranque SIM800...\n", SIM800_BOOT_MS);
    delay(SIM800_BOOT_MS);
#endif

    // 3. Leer batería TEMPRANO (antes de que otros sensores interfieran con I2C)
    Serial.println("[CAMPO] Leyendo batería antes de inicializar otros sensores...");
    int batPct = _readBatteryPct();
    Serial.printf("[CAMPO] Batería leída temprano: %d%%\n", batPct);

    // 4. Inicializar LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("[CAMPO] Error al montar LittleFS");
    }

    // 5. Inicializar sensores
    setupleerph();
    
    TempSensor tempSensor;
    tempSensor.begin();
    Serial.println("[CAMPO] Sensores inicializados");

    // 6. Intentar conectar MQTT (opcional)
    MQTTManager mqtt;
    mqtt.begin();

    bool mqttConnected = mqtt.connect(MQTT_RETRIES);
    if (mqttConnected) {
        Serial.println("[CAMPO] MQTT conectado");
    } else {
        Serial.println("[CAMPO] MQTT no disponible - continuando con muestreo local");
    }

    // 7. Bucle de muestreo
    int count = 0;

    Serial.printf("[CAMPO] Muestreando %lu s (warm-up %lu s + datos %lu s)...\n",
                  SAMPLE_TOTAL_MS / 1000,
                  SAMPLE_STABILIZE_MS / 1000,
                  (SAMPLE_TOTAL_MS - SAMPLE_STABILIZE_MS) / 1000);

    unsigned long start = millis();
    unsigned long lastSample = start;

    while (millis() - start < SAMPLE_TOTAL_MS) {
        unsigned long now = millis();

        if (now - lastSample >= SAMPLE_INTERVAL_MS) {
            lastSample = now;
            loopleerph();
            tempSensor.loop();

            // Solo acumular datos después del período de estabilización
            if ((now - start) >= SAMPLE_STABILIZE_MS && count < MAX_SAMPLES) {
                _phBuf[count]   = getpHValue();
                _condBuf[count] = getConductivityPPM();
                float t         = tempSensor.getTemperature();
                _tempBuf[count] = isnan(t) ? 0.0f : t;
                count++;
            }
        } else {
            delay(1);  // cede al scheduler para no disparar el task watchdog
        }
        
        // Verificar botón de boot para cambio de modo
        handleBootButtonInField();
    }

    Serial.printf("[CAMPO] Muestras acumuladas: %d\n", count);

    // 8. Calcular estadísticas
    float phMean   = _mean(_phBuf,   count);
    float phCV     = _cv(_phBuf,     count);
    float condMean = _mean(_condBuf, count);
    float condCV   = _cv(_condBuf,   count);
    float tempMean = _mean(_tempBuf, count);
    float tempCV   = _cv(_tempBuf,   count);

    // 9. Espacio de almacenamiento LIBRE (no usado)
    int storPct = 0;
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    
    if (totalBytes > 0) {
        float freeBytes = (float)(totalBytes - usedBytes);
        storPct = (int)((freeBytes / totalBytes) * 100.0f);
        Serial.printf("[CAMPO] Almacenamiento: %u bytes usados de %u total\n", usedBytes, totalBytes);
        Serial.printf("[CAMPO] Espacio libre: %u bytes (%.1f%%)\n", (totalBytes - usedBytes), (freeBytes / totalBytes) * 100.0f);
    } else {
        Serial.println("[CAMPO] ERROR: No se pudo obtener información del sistema de archivos");
        storPct = 0;
    }

    Serial.printf("[CAMPO] Batería: %d %%  Almacenamiento libre: %d %%\n", batPct, storPct);

    // 12. Construir JSON para MQTT (storage = porcentaje de espacio libre)
    char payload[320];
    snprintf(payload, sizeof(payload),
             "{\"ph\":{\"mean\":%.3f,\"cv\":%.2f},"
             "\"cond\":{\"mean\":%.1f,\"cv\":%.2f},"
             "\"temp\":{\"mean\":%.2f,\"cv\":%.2f},"
             "\"bat\":%d,\"storage\":%d}",
             phMean, phCV,
             condMean, condCV,
             tempMean, tempCV,
             batPct, storPct);

    Serial.printf("[CAMPO] Payload: %s\n", payload);

    // 13. Publicar datos vía MQTT (solo si hay conexión)
    bool mqttSuccess = false;
    if (mqttConnected) {
        mqttSuccess = mqtt.publish(mqtt.topic("data"), payload);
        Serial.printf("[CAMPO] Publicación MQTT: %s\n", mqttSuccess ? "OK" : "FALLO");
        mqtt.disconnect();
    } else {
        Serial.println("[CAMPO] MQTT no disponible");
    }

    // 14. Guardar en SimpleLogger con estado de envío MQTT
    Serial.println("[CAMPO] Guardando datos localmente...");
    char logFilename[64];
    snprintf(logFilename, sizeof(logFilename), "/%s.csv", FIELD_LOG_NAME);
    
    if (SimpleLogger::logFieldData(logFilename, phMean, phCV, condMean, condCV, 
                                   tempMean, tempCV, batPct, storPct, mqttSuccess)) {
        Serial.printf("[CAMPO] Datos guardados localmente (MQTT: %s)\n", 
                     mqttSuccess ? "ENVIADO" : "NO ENVIADO");
    } else {
        Serial.println("[CAMPO] Error al guardar localmente");
    }

    Serial.printf("[CAMPO] Ciclo completado. Entrando a deep-sleep por %llu min...\n", DEEP_SLEEP_MINUTES);
    Serial.flush();
    delay(100);

    digitalWrite(PIN_MOSFET, LOW);
    
    // Configurar botón de boot como fuente de wake-up (presión = LOW)
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_US);
    
    Serial.println("[CAMPO] Wake-up configurado: Timer (30min) + Botón boot");
    Serial.flush();
    delay(50);
    
    esp_deep_sleep_start();
}

#endif // FIELDMODE_H
