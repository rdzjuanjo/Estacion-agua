// MQTTManager.h - Conexión MQTT sobre WiFi o SIM800 (seleccionable por build flag)
//
// Uso: compilar con -DUSE_WIFI  para WiFi
//      compilar con -DUSE_SIM800 para módulo GSM SIM800
//
// Credenciales cargadas de NVS namespace "mqttcfg":
//   broker, port (int), user, pass, clientid, topic
//   WiFi:  ssid, wifipass
//   SIM800: apn, apnuser, apnpass
#ifndef MQTTMANAGER_H
#define MQTTMANAGER_H

#include <Preferences.h>
#include <PubSubClient.h>
#include "Config.h"

#if defined(USE_SIM800)
  #define TINY_GSM_MODEM_SIM800
  #include <TinyGsmClient.h>
#elif defined(USE_WIFI)
  #include <WiFi.h>
#else
  #error "Define USE_WIFI o USE_SIM800 en build_flags"
#endif

// ── Longitudes máximas de los campos NVS ─────────────────────────────────────
#define MQTT_STR_LEN  64
#define MQTT_TOPIC_LEN 64

class MQTTManager {
public:
    MQTTManager()
#if defined(USE_SIM800)
        : _serial(1)
        , _modem(_serial)
        , _gsmClient(_modem)
        , _mqtt(_gsmClient)
#else
        : _mqtt(_wifiClient)
#endif
    {}

    // Carga configuración de NVS e inicializa el stack de red.
    // NO intenta conectar todavía.
    void begin() {
        _loadConfig();

#if defined(USE_SIM800)
        _serial.begin(9600, SERIAL_8N1, SIM800_RX_PIN, SIM800_TX_PIN);
        Serial.println("[GSM] Inicializando modem...");
        if (!_modem.init()) {
            Serial.println("[GSM] Error al inicializar modem");
        }
#endif
        _mqtt.setServer(_broker, _port);
        _mqtt.setKeepAlive(30);
        _mqtt.setSocketTimeout(10);
    }

    // Intenta conectar a la red y al broker MQTT.
    // Retorna true si la conexión fue exitosa.
    // maxRetries: número máximo de intentos (cada uno separado por MQTT_RETRY_DELAY_MS)
    bool connect(int maxRetries = MQTT_RETRIES) {
#if defined(USE_SIM800)
        if (!_connectGPRS()) {
            return false;
        }
#else
        if (!_connectWiFi(maxRetries)) {
            return false;
        }
#endif
        for (int i = 0; i < maxRetries; i++) {
            Serial.printf("[MQTT] Intento %d/%d → %s:%d (id=%s)\n",
                          i + 1, maxRetries, _broker, _port, _clientId);
            bool ok;
            if (_user[0] != '\0') {
                ok = _mqtt.connect(_clientId, _user, _pass);
            } else {
                ok = _mqtt.connect(_clientId);
            }
            if (ok) {
                Serial.println("[MQTT] Conectado");
                return true;
            }
            Serial.printf("[MQTT] Fallo, rc=%d\n", _mqtt.state());
            delay(MQTT_RETRY_DELAY_MS);
        }
        return false;
    }

    // Publica un mensaje. Retorna true si tuvo éxito.
    bool publish(const char* topic, const char* payload) {
        if (!_mqtt.connected()) return false;
        return _mqtt.publish(topic, payload, /*retained=*/false);
    }

    // Construye el topic completo: "<prefix>/<suffix>"
    // Reserva un buffer temporal — válido hasta la próxima llamada.
    const char* topic(const char* suffix) {
        snprintf(_topicBuf, sizeof(_topicBuf), "%s/%s", _topicPrefix, suffix);
        return _topicBuf;
    }

    void disconnect() {
        if (_mqtt.connected()) _mqtt.disconnect();
        delay(100);
#if defined(USE_WIFI)
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
#elif defined(USE_SIM800)
        _modem.gprsDisconnect();
        _modem.poweroff();
#endif
    }

    // ── Accesores de configuración (para la página web) ─────────────────────
    const char* getBroker()      const { return _broker; }
    int         getPort()        const { return _port; }
    const char* getUser()        const { return _user; }
    const char* getClientId()    const { return _clientId; }
    const char* getTopicPrefix() const { return _topicPrefix; }

#if defined(USE_WIFI)
    const char* getSsid()        const { return _ssid; }
#elif defined(USE_SIM800)
    const char* getApn()         const { return _apn; }
    const char* getApnUser()     const { return _apnUser; }
#endif

private:
    // ── Config cargada de NVS ────────────────────────────────────────────────
    char _broker[MQTT_STR_LEN]      = "broker.hivemq.com";
    int  _port                      = 1883;
    char _user[MQTT_STR_LEN]        = "";
    char _pass[MQTT_STR_LEN]        = "";
    char _clientId[MQTT_STR_LEN]    = "estacion-agua";
    char _topicPrefix[MQTT_TOPIC_LEN] = "estacion/agua";
    char _topicBuf[MQTT_TOPIC_LEN + 32];

#if defined(USE_WIFI)
    char        _ssid[MQTT_STR_LEN]     = "";
    char        _wifiPass[MQTT_STR_LEN] = "";
    WiFiClient  _wifiClient;
#elif defined(USE_SIM800)
    char              _apn[MQTT_STR_LEN]     = "";
    char              _apnUser[MQTT_STR_LEN] = "";
    char              _apnPass[MQTT_STR_LEN] = "";
    HardwareSerial    _serial;
    TinyGsm           _modem;
    TinyGsmClient     _gsmClient;
#endif

    PubSubClient _mqtt;

    // ── NVS ──────────────────────────────────────────────────────────────────
    void _loadConfig() {
        Preferences p;
        p.begin("mqttcfg", /*readOnly=*/true);
        p.getString("broker",   _broker,      sizeof(_broker));
        _port = p.getInt("port", _port);
        p.getString("user",     _user,        sizeof(_user));
        p.getString("pass",     _pass,        sizeof(_pass));
        p.getString("clientid", _clientId,    sizeof(_clientId));
        p.getString("topic",    _topicPrefix, sizeof(_topicPrefix));
#if defined(USE_WIFI)
        p.getString("ssid",     _ssid,        sizeof(_ssid));
        p.getString("wifipass", _wifiPass,    sizeof(_wifiPass));
#elif defined(USE_SIM800)
        p.getString("apn",      _apn,         sizeof(_apn));
        p.getString("apnuser",  _apnUser,     sizeof(_apnUser));
        p.getString("apnpass",  _apnPass,     sizeof(_apnPass));
#endif
        p.end();
    }

#if defined(USE_WIFI)
    bool _connectWiFi(int maxRetries) {
        if (_ssid[0] == '\0') {
            Serial.println("[WiFi] SSID no configurado");
            return false;
        }
        Serial.printf("[WiFi] Conectando a %s\n", _ssid);
        WiFi.mode(WIFI_STA);
        WiFi.begin(_ssid, _wifiPass);
        for (int i = 0; i < maxRetries * 10; i++) {  // ~maxRetries*2 s
            if (WiFi.isConnected()) {
                Serial.printf("[WiFi] Conectado, IP: %s\n",
                              WiFi.localIP().toString().c_str());
                return true;
            }
            delay(200);
            yield();  // Resetear watchdog para evitar TG1WDT_SYS_RESET
        }
        Serial.println("[WiFi] Timeout al conectar");
        return false;
    }
#elif defined(USE_SIM800)
    bool _connectGPRS() {
        Serial.println("[GSM] Esperando red...");
        if (!_modem.waitForNetwork(20000)) {
            Serial.println("[GSM] Sin red");
            return false;
        }
        Serial.printf("[GSM] Conectando GPRS (APN: %s)\n", _apn);
        if (!_modem.gprsConnect(_apn, _apnUser, _apnPass)) {
            Serial.println("[GSM] Fallo GPRS");
            return false;
        }
        Serial.println("[GSM] GPRS conectado");
        return true;
    }
#endif
};

#endif // MQTTMANAGER_H
