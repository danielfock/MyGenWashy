#ifndef INTEGRATION_SETTINGS_H
#define INTEGRATION_SETTINGS_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"

#ifndef MQTT_SERVER
  #define MQTT_SERVER ""
#endif
#ifndef MQTT_USER
  #define MQTT_USER ""
#endif
#ifndef MQTT_PASS
  #define MQTT_PASS ""
#endif

#define INTEGRATION_SETTINGS_FILE "/integration.json"
#define MQTT_SERVER_MAX_LEN       64
#define MQTT_USER_MAX_LEN         48
#define MQTT_PASS_MAX_LEN         96
#define MQTT_TOPIC_MAX_LEN        64
#define MQTT_DISCOVERY_MAX_LEN    64

enum IntegrationMode : uint8_t {
    INTEGRATION_MODE_MQTT = 0,
    INTEGRATION_MODE_ESPHOME_API = 1
};

struct IntegrationSettingsData {
    uint8_t version;
    uint8_t mode;
    uint16_t mqttPort;
    char mqttServer[MQTT_SERVER_MAX_LEN];
    char mqttUser[MQTT_USER_MAX_LEN];
    char mqttPass[MQTT_PASS_MAX_LEN];
    char mqttTopicPrefix[MQTT_TOPIC_MAX_LEN];
    char mqttDiscoveryPrefix[MQTT_DISCOVERY_MAX_LEN];
};

class IntegrationSettingsStore {
public:
    void begin() {
        _setDefaults();
        _load();
    }

    const IntegrationSettingsData& current() const { return _data; }

    bool usesMqtt() const { return _data.mode == INTEGRATION_MODE_MQTT; }
    bool usesEspHomeApi() const { return _data.mode == INTEGRATION_MODE_ESPHOME_API; }

    const char* modeName() const {
        return usesEspHomeApi() ? "esphome_api" : "mqtt";
    }

    bool applyFromJson(JsonObjectConst obj, String& error) {
        IntegrationSettingsData updated = _data;

        if (obj["mode"].is<const char*>()) {
            if (!_parseMode(obj["mode"].as<const char*>(), updated.mode)) {
                error = F("Ungueltiger Modus");
                return false;
            }
        }

        if (obj["mqtt_port"].is<int>()) {
            int port = obj["mqtt_port"].as<int>();
            if (port < 1 || port > 65535) {
                error = F("MQTT-Port ausserhalb 1..65535");
                return false;
            }
            updated.mqttPort = (uint16_t)port;
        }

        if (obj["mqtt_server"].is<const char*>()) {
            _copy(updated.mqttServer, sizeof(updated.mqttServer), obj["mqtt_server"].as<const char*>());
        }
        if (obj["mqtt_user"].is<const char*>()) {
            _copy(updated.mqttUser, sizeof(updated.mqttUser), obj["mqtt_user"].as<const char*>());
        }
        if (obj["mqtt_pass"].is<const char*>()) {
            _copy(updated.mqttPass, sizeof(updated.mqttPass), obj["mqtt_pass"].as<const char*>());
        }
        if (obj["mqtt_topic_prefix"].is<const char*>()) {
            _copy(updated.mqttTopicPrefix, sizeof(updated.mqttTopicPrefix), obj["mqtt_topic_prefix"].as<const char*>());
        }
        if (obj["mqtt_discovery_prefix"].is<const char*>()) {
            _copy(updated.mqttDiscoveryPrefix, sizeof(updated.mqttDiscoveryPrefix), obj["mqtt_discovery_prefix"].as<const char*>());
        }

        if (updated.mqttTopicPrefix[0] == '\0') {
            _copy(updated.mqttTopicPrefix, sizeof(updated.mqttTopicPrefix), MQTT_TOPIC_PREFIX);
        }
        if (updated.mqttDiscoveryPrefix[0] == '\0') {
            _copy(updated.mqttDiscoveryPrefix, sizeof(updated.mqttDiscoveryPrefix), MQTT_DISCOVERY_PREFIX);
        }
        if (updated.mqttPort == 0) {
            updated.mqttPort = MQTT_PORT;
        }

        _data = updated;
        if (!_save()) {
            error = F("Speichern in LittleFS fehlgeschlagen");
            return false;
        }
        return true;
    }

private:
    IntegrationSettingsData _data;

    void _setDefaults() {
        memset(&_data, 0, sizeof(_data));
        _data.version = 1;
        _data.mode = (strlen(MQTT_SERVER) > 0) ? INTEGRATION_MODE_MQTT : INTEGRATION_MODE_ESPHOME_API;
        _data.mqttPort = MQTT_PORT;
        _copy(_data.mqttServer, sizeof(_data.mqttServer), MQTT_SERVER);
        _copy(_data.mqttUser, sizeof(_data.mqttUser), MQTT_USER);
        _copy(_data.mqttPass, sizeof(_data.mqttPass), MQTT_PASS);
        _copy(_data.mqttTopicPrefix, sizeof(_data.mqttTopicPrefix), MQTT_TOPIC_PREFIX);
        _copy(_data.mqttDiscoveryPrefix, sizeof(_data.mqttDiscoveryPrefix), MQTT_DISCOVERY_PREFIX);
    }

    bool _load() {
        File file = LittleFS.open(INTEGRATION_SETTINGS_FILE, "r");
        if (!file) {
            return false;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, file);
        file.close();
        if (err) {
            Serial.print(F("[SETTINGS] integration.json ungueltig: "));
            Serial.println(err.c_str());
            return false;
        }

        JsonObjectConst obj = doc.as<JsonObjectConst>();
        IntegrationSettingsData loaded = _data;

        if (obj["mode"].is<const char*>()) {
            _parseMode(obj["mode"].as<const char*>(), loaded.mode);
        }
        if (obj["mqtt_port"].is<int>()) {
            int port = obj["mqtt_port"].as<int>();
            if (port >= 1 && port <= 65535) {
                loaded.mqttPort = (uint16_t)port;
            }
        }
        if (obj["mqtt_server"].is<const char*>()) {
            _copy(loaded.mqttServer, sizeof(loaded.mqttServer), obj["mqtt_server"].as<const char*>());
        }
        if (obj["mqtt_user"].is<const char*>()) {
            _copy(loaded.mqttUser, sizeof(loaded.mqttUser), obj["mqtt_user"].as<const char*>());
        }
        if (obj["mqtt_pass"].is<const char*>()) {
            _copy(loaded.mqttPass, sizeof(loaded.mqttPass), obj["mqtt_pass"].as<const char*>());
        }
        if (obj["mqtt_topic_prefix"].is<const char*>()) {
            _copy(loaded.mqttTopicPrefix, sizeof(loaded.mqttTopicPrefix), obj["mqtt_topic_prefix"].as<const char*>());
        }
        if (obj["mqtt_discovery_prefix"].is<const char*>()) {
            _copy(loaded.mqttDiscoveryPrefix, sizeof(loaded.mqttDiscoveryPrefix), obj["mqtt_discovery_prefix"].as<const char*>());
        }

        _data = loaded;
        return true;
    }

    bool _save() {
        JsonDocument doc;
        doc["mode"] = modeName();
        doc["mqtt_server"] = _data.mqttServer;
        doc["mqtt_port"] = _data.mqttPort;
        doc["mqtt_user"] = _data.mqttUser;
        doc["mqtt_pass"] = _data.mqttPass;
        doc["mqtt_topic_prefix"] = _data.mqttTopicPrefix;
        doc["mqtt_discovery_prefix"] = _data.mqttDiscoveryPrefix;

        File file = LittleFS.open(INTEGRATION_SETTINGS_FILE, "w");
        if (!file) {
            return false;
        }
        serializeJson(doc, file);
        file.close();
        return true;
    }

    static void _copy(char* dst, size_t size, const char* src) {
        if (!dst || size == 0) {
            return;
        }
        if (!src) {
            dst[0] = '\0';
            return;
        }
        strlcpy(dst, src, size);
    }

    static bool _parseMode(const char* mode, uint8_t& outMode) {
        if (!mode) {
            return false;
        }
        if (strcmp(mode, "mqtt") == 0) {
            outMode = INTEGRATION_MODE_MQTT;
            return true;
        }
        if (strcmp(mode, "esphome_api") == 0 || strcmp(mode, "esphome") == 0) {
            outMode = INTEGRATION_MODE_ESPHOME_API;
            return true;
        }
        return false;
    }
};

#endif // INTEGRATION_SETTINGS_H
