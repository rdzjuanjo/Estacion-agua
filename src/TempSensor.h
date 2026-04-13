// TempSensor.h - Lectura de temperatura DS18B20 via OneWire
#ifndef TEMPSENSOR_H
#define TEMPSENSOR_H

#include <OneWire.h>
#include <DallasTemperature.h>
#include "Config.h"

class TempSensor {
public:
    TempSensor()
        : _ow(PIN_DS18B20)
        , _dt(&_ow)
        , _lastTemp(NAN)
        , _lastRequest(0)
        , _conversionReady(false)
    {}

    void begin() {
        _dt.begin();
        _dt.setResolution(12);
        _dt.setWaitForConversion(false);  // async, no bloquear el loop
        _dt.requestTemperatures();        // primera solicitud
        _lastRequest = millis();
        _conversionReady = false;
    }

    // Llamar en cada iteración del loop (cada ~100 ms es suficiente)
    void loop() {
        unsigned long now = millis();

        if (!_conversionReady && (now - _lastRequest >= 750)) {
            // La conversión de 12 bits tarda ~750 ms; ya está lista
            float t = _dt.getTempCByIndex(0);
            if (t != DEVICE_DISCONNECTED_C && t != -127.0f) {
                _lastTemp = t;
            }
            // Solicitar la siguiente conversión
            _dt.requestTemperatures();
            _lastRequest = now;
        }
    }

    // Devuelve NAN si nunca se obtuvo una lectura válida
    float getTemperature() const {
        return _lastTemp;
    }

private:
    OneWire          _ow;
    DallasTemperature _dt;
    float            _lastTemp;
    unsigned long    _lastRequest;
    bool             _conversionReady;
};

#endif // TEMPSENSOR_H
