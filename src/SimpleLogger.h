// SimpleLogger.h - Logger ultraliviano para modo campo
// Solo escribe una línea CSV sin metadata pesada in-memory
#ifndef SIMPLELOGGER_H
#define SIMPLELOGGER_H

#include <Arduino.h>
#include <LittleFS.h>

class SimpleLogger {
public:
    SimpleLogger() {}

    // Agrega una línea CSV al archivo. Crea el archivo si no existe.
    // Si existe, agrega al final. No mantiene metadata en RAM.
    static bool appendCSV(const char* filename, const char* csvLine) {
        if (!LittleFS.begin()) return false;

        // Abrir archivo en modo append
        File file = LittleFS.open(filename, "a");
        if (!file) {
            Serial.printf("[SimpleLogger] Error abriendo %s\n", filename);
            return false;
        }

        // Si el archivo está vacío, agregar header
        if (file.size() == 0) {
            file.println("timestamp,ph_mean,ph_cv,cond_mean,cond_cv,temp_mean,temp_cv,bat_pct,storage_pct,mqtt_sent");
        }

        // Agregar timestamp + línea de datos
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            char timestamp[32];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
            file.printf("%s,%s\n", timestamp, csvLine);
        } else {
            // Sin tiempo, usar millis()
            file.printf("%lu,%s\n", millis(), csvLine);
        }

        file.close();
        return true;
    }

    // Versión optimizada para datos del modo campo
    static bool logFieldData(const char* filename, 
                            float phMean, float phCV,
                            float condMean, float condCV, 
                            float tempMean, float tempCV,
                            int batPct, int storPct, bool mqttSent = false) {
        char csvLine[128];
        snprintf(csvLine, sizeof(csvLine),
                 "%.3f,%.2f,%.1f,%.2f,%.2f,%.2f,%d,%d,%s", 
                 phMean, phCV, condMean, condCV, 
                 tempMean, tempCV, batPct, storPct, 
                 mqttSent ? "SI" : "NO");
        
        return appendCSV(filename, csvLine);
    }
};

#endif // SIMPLELOGGER_H