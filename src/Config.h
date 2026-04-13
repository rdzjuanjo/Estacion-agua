// Config.h - Pines y constantes del sistema
#ifndef CONFIG_H
#define CONFIG_H

// ── Pines ────────────────────────────────────────────────────────────────────
#define PIN_MOSFET       32   // HIGH = periféricos encendidos (pH, cond, SIM800)
#define PIN_DS18B20      23   // Bus OneWire del sensor de temperatura DS18B20

// Pines UART para SIM800 (solo aplica con -DUSE_SIM800)
#define SIM800_RX_PIN    16
#define SIM800_TX_PIN    17

// MAX17048 (medidor de batería por I2C)
// Pines por defecto para ESP32 DevKit: SDA=21, SCL=22
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// ── Timing modo campo ─────────────────────────────────────────────────────────
#define DEEP_SLEEP_MINUTES  30ULL
#define DEEP_SLEEP_US       (DEEP_SLEEP_MINUTES * 60ULL * 1000000ULL)

#define SAMPLE_TOTAL_MS     30000UL   // Duración total del período de muestreo
#define SAMPLE_STABILIZE_MS 20000UL   // Primeros N ms de warm-up (se descartan)
#define SAMPLE_INTERVAL_MS  100UL     // Intervalo entre lecturas

// ── MQTT ──────────────────────────────────────────────────────────────────────
#define MQTT_RETRIES        5
#define MQTT_RETRY_DELAY_MS 2000

// ── SIM800 ────────────────────────────────────────────────────────────────────
#define SIM800_BOOT_MS      10000UL   // Pausa tras encender MOSFET para que arranque

// ── DataLogger (respaldo local) ───────────────────────────────────────────────
#define FIELD_LOG_NAME        "datos"
#define FIELD_LOG_MAX_ENTRIES  2000   // ~2.5 años a 30 min/ciclo

#endif // CONFIG_H
