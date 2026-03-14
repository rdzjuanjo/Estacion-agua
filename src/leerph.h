// leerph.h - Biblioteca para lectura de sensores con ADS1115
#ifndef LEERPH_H
#define LEERPH_H

#include <Adafruit_ADS1X15.h>
#include <Preferences.h>

Adafruit_ADS1115 ads;
Preferences       prefs;

// Namespace y claves usadas en NVS
#define PREFS_NS   "phcal"
#define KEY_PH4    "ph4"
#define KEY_PH7    "ph7"
#define KEY_PH10   "ph10"
#define KEY_DIRECT "direct"
// Claves para conductividad
#define KEY_CONDUCTIVITY_REF    "cond_ref"
#define KEY_CONDUCTIVITY_FACTOR "cond_factor"

// Valores por defecto de calibración
#define DEFAULT_PH4  200
#define DEFAULT_PH7  300
#define DEFAULT_PH10 400

// Timing del filtro
unsigned long previousMillis = 0;
const unsigned long interval = 100;

// Filtro exponencial para pH
const float alpha   = 0.05f;
float filteredValue = 0.0f;
bool  firstReading  = true;
float pHvalue       = 0.0f;

// Filtro exponencial para conductividad
float filteredConductivity = 0.0f;
bool  firstConductivityReading = true;
float conductivityPPM = 0.0f;

// Calibración en memoria (cargada una sola vez en setupleerph)
static int  _ph4 = DEFAULT_PH4, _ph7 = DEFAULT_PH7, _ph10 = DEFAULT_PH10;
static bool _isDirectRelation = true;

// Calibración de conductividad en memoria
static float _conductivityReference = 1000.0f; // ppm de referencia
static float _conductivityFactor = 1.0f;       // factor de conversión

// ── Prototipos ────────────────────────────────────────────────────────────────
void  setupleerph();
void  loopleerph();
float getSensorValue();
float getpHValue();
float getConductivityValue();
float getConductivityPPM();
void  loadCalibration(int& ph4, int& ph7, int& ph10, bool& isDirect);
void  saveCalibration(int ph4, int ph7, int ph10, bool isDirect);
void  loadConductivityCalibration(float& reference, float& factor);
void  saveConductivityCalibration(float reference, float factor);
void  calibrateConductivity(float knownPPM);
bool  detectRelation(int ph4, int ph7, int ph10);

// ── Implementaciones ──────────────────────────────────────────────────────────

void loadCalibration(int& ph4, int& ph7, int& ph10, bool& isDirect) {
  prefs.begin(PREFS_NS, /*readOnly=*/true);
  bool firstBoot = !prefs.isKey(KEY_PH4);  // verificar ANTES de end()
  ph4      = prefs.getInt (KEY_PH4,    DEFAULT_PH4);
  ph7      = prefs.getInt (KEY_PH7,    DEFAULT_PH7);
  ph10     = prefs.getInt (KEY_PH10,   DEFAULT_PH10);
  isDirect = prefs.getBool(KEY_DIRECT, true);
  prefs.end();

  if (firstBoot) {
    Serial.println("Preferences vacías - guardando valores por defecto");
    saveCalibration(ph4, ph7, ph10, isDirect);
  }
}

void saveCalibration(int ph4, int ph7, int ph10, bool isDirect) {
  prefs.begin(PREFS_NS, /*readOnly=*/false);
  prefs.putInt (KEY_PH4,    ph4);
  prefs.putInt (KEY_PH7,    ph7);
  prefs.putInt (KEY_PH10,   ph10);
  prefs.putBool(KEY_DIRECT, isDirect);
  prefs.end();
  Serial.printf("Calibración guardada: ph4=%d ph7=%d ph10=%d directa=%s\n",
                ph4, ph7, ph10, isDirect ? "si" : "no");

  // Actualizar caché en memoria
  _ph4 = ph4; _ph7 = ph7; _ph10 = ph10; _isDirectRelation = isDirect;
}

void loadConductivityCalibration(float& reference, float& factor) {
  prefs.begin(PREFS_NS, /*readOnly=*/true);
  reference = prefs.getFloat(KEY_CONDUCTIVITY_REF, 1000.0f);
  factor = prefs.getFloat(KEY_CONDUCTIVITY_FACTOR, 1.0f);
  prefs.end();
}

void saveConductivityCalibration(float reference, float factor) {
  prefs.begin(PREFS_NS, /*readOnly=*/false);
  prefs.putFloat(KEY_CONDUCTIVITY_REF, reference);
  prefs.putFloat(KEY_CONDUCTIVITY_FACTOR, factor);
  prefs.end();
  Serial.printf("Calibración conductividad guardada: ref=%.1f ppm factor=%.4f\n",
                reference, factor);

  // Actualizar caché en memoria
  _conductivityReference = reference;
  _conductivityFactor = factor;
}

void calibrateConductivity(float knownPPM) {
  if (filteredConductivity <= 0) {
    Serial.println("Error: lectura de conductividad inválida");
    return;
  }
  float newFactor = knownPPM / filteredConductivity;
  saveConductivityCalibration(knownPPM, newFactor);
  Serial.printf("Conductividad calibrada: %.1f ADC = %.1f ppm (factor=%.4f)\n",
                filteredConductivity, knownPPM, newFactor);
}

bool detectRelation(int ph4, int ph7, int ph10) {
  if (ph4 != 0 && ph7 != 0 && ph10 != 0) {
    if (ph4 < ph7 && ph7 < ph10) return true;
    if (ph4 > ph7 && ph7 > ph10) return false;
  }
  return true;  // asumir directa si no hay datos suficientes
}

void setupleerph() {
  // Preferences usa NVS internamente; no requiere begin/init global
  loadCalibration(_ph4, _ph7, _ph10, _isDirectRelation);
  Serial.printf("Calibración cargada: ph4=%d ph7=%d ph10=%d directa=%s\n",
                _ph4, _ph7, _ph10, _isDirectRelation ? "si" : "no");

  // Cargar calibración de conductividad
  loadConductivityCalibration(_conductivityReference, _conductivityFactor);
  Serial.printf("Calibración conductividad: ref=%.1f ppm factor=%.4f\n",
                _conductivityReference, _conductivityFactor);

  pinMode(34, INPUT);

  if (!ads.begin(0x48))
    Serial.println("Error: No se pudo inicializar ADS1115 (0x48)");

  ads.setGain(GAIN_TWOTHIRDS);
}

void loopleerph() {
  unsigned long now = millis();
  if (now - previousMillis < interval) return;
  previousMillis = now;

  // Leer pH desde A0
  float raw = ads.readADC_SingleEnded(0);

  // Filtro exponencial para pH
  filteredValue = firstReading ? raw : (alpha * raw + (1.0f - alpha) * filteredValue);
  firstReading  = false;

  // Leer conductividad desde A1
  float rawConductivity = ads.readADC_SingleEnded(1);

  // Filtro exponencial para conductividad
  filteredConductivity = firstConductivityReading ? rawConductivity : 
                         (alpha * rawConductivity + (1.0f - alpha) * filteredConductivity);
  firstConductivityReading = false;

  // ── Calcular pH ─────────────────────────────────────────────────────────────
  // FIX: la lógica de inversión era incorrecta.
  // Para relación inversa: a mayor lectura → menor pH.
  // Simplemente invertimos la asignación de los extremos al tramo correcto.
  //
  // Sea v = filteredValue, y los puntos de calibración:
  //   ph4  → valor ADC que corresponde a pH 4
  //   ph7  → valor ADC que corresponde a pH 7
  //   ph10 → valor ADC que corresponde a pH 10
  //
  // Relación directa:  ph4 < ph7 < ph10 (a mayor lectura → mayor pH)
  // Relación inversa:  ph4 > ph7 > ph10 (a mayor lectura → menor pH)
  //
  // En ambos casos se hace interpolación lineal por tramos alrededor de pH 7:
  //   Tramo bajo (entre pH 4 y pH 7):
  //     pH = 7 + (v - ph7) * 3.0 / (ph7 - ph4)    [pendiente puede ser +/-]
  //   Tramo alto (entre pH 7 y pH 10):
  //     pH = 7 + (v - ph7) * 3.0 / (ph10 - ph7)
  //
  // No es necesario intercambiar los valores; la fórmula funciona para ambas
  // relaciones siempre que los denominadores sean distintos de cero.

  float p = 7.0f;
  float denom_lo = (float)(_ph7 - _ph4);
  float denom_hi = (float)(_ph10 - _ph7);

  if (denom_lo == 0.0f || denom_hi == 0.0f) {
    // Calibración inválida: evitar división por cero
    p = 7.0f;
  } else {
    // Determinamos en qué tramo cae según la relación
    bool inLowSegment = _isDirectRelation
                        ? filteredValue <= _ph7     // directa: v<=ph7 → tramo bajo
                        : filteredValue >= _ph7;    // inversa: v>=ph7 → tramo bajo (pH<7)

    if (inLowSegment)
      p = 7.0f + (filteredValue - (float)_ph7) * 3.0f / denom_lo;
    else
      p = 7.0f + (filteredValue - (float)_ph7) * 3.0f / denom_hi;
  }

  pHvalue = constrain(p, 0.0f, 14.0f);

  // ── Calcular Conductividad en PPM ──────────────────────────────────────────
  conductivityPPM = filteredConductivity * _conductivityFactor;

  // Debug cada 10 s
  static unsigned long lastDbg = 0;
  if (now - lastDbg > 10000) {
    lastDbg = now;
    Serial.printf("=== LECTURA === pH: raw=%.1f filt=%.1f pH=%.2f | Cond: raw=%.1f filt=%.1f ppm=%.1f\n", 
                  raw, filteredValue, pHvalue, rawConductivity, filteredConductivity, conductivityPPM);
  }
}

float getSensorValue() { return filteredValue; }
float getpHValue()     { return pHvalue;       }
float getConductivityValue() { return filteredConductivity; }
float getConductivityPPM()   { return conductivityPPM; }

#endif // LEERPH_H