#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "config.h"
#include "nano_bridge.h"
#include "water_level.h"
#include "wash_programs.h"
#include "program_storage.h"
#include "wash_program.h"

// ============================================================================
// MyGenWashy - ESP32 Hauptcontroller v2.0
//
// Neu in v2:
//   - Vordefinierte Waschprogramme (20/30/40/60/90 Grad, Schnell, Schleudern)
//   - Benutzerdefinierte Programme (LittleFS JSON-Speicher)
//   - Erweiterte Web-UI mit Programmverwaltung
//   - MQTT Auto-Discovery fuer Home Assistant (Select, Buttons, Sensoren)
// ============================================================================

// --- WiFi Credentials ---
#if __has_include("wifi_credentials.h")
  #include "wifi_credentials.h"
#else
  #warning "wifi_credentials.h nicht gefunden - verwende defaults"
  #define WIFI_SSID WIFI_SSID_DEFAULT
  #define WIFI_PASS WIFI_PASS_DEFAULT
#endif

#ifndef MQTT_SERVER
  #define MQTT_SERVER ""
#endif
#ifndef MQTT_USER
  #define MQTT_USER ""
#endif
#ifndef MQTT_PASS
  #define MQTT_PASS ""
#endif

#include "integration_settings.h"

// --- Globale Objekte ---
NanoBridge nano;
WaterLevel waterLevel;
WashProgram wash;
ProgramStorage programs;
IntegrationSettingsStore integration;
WebServer webserver(WEBSERVER_PORT);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// --- Timing ---
unsigned long lastI2CRead = 0;
unsigned long lastI2CWrite = 0;
unsigned long lastMqttPublish = 0;
unsigned long lastWifiCheck = 0;
bool mqttConfigured = false;
bool mqttDiscoverySent = false;
int8_t selectedProgramIdx = 0;

// --- Forward Declarations ---
void setupWifi();
void setupWebserver();
void setupMqtt();
String mqttTopic(const char* suffix);
String mqttDiscoveryTopic(const char* component, const char* id);
bool mqttPublish(const char* suffix, const char* payload, bool retained = true);
bool integrationUsesMqtt();
void handleMqttMessage(char* topic, byte* payload, unsigned int length);
void publishMqttStatus();
void publishMqttDiscovery();
void reconnectMqtt();
String buildProgramOptionsJson();

bool integrationUsesMqtt() {
    return integration.current().mode == INTEGRATION_MODE_MQTT;
}

String mqttTopic(const char* suffix) {
    String topic = integration.current().mqttTopicPrefix;
    if (topic.length() == 0) {
        topic = MQTT_TOPIC_PREFIX;
    }
    if (suffix && suffix[0] != '\0') {
        topic += "/";
        topic += suffix;
    }
    return topic;
}

String mqttDiscoveryTopic(const char* component, const char* id) {
    String topic = integration.current().mqttDiscoveryPrefix;
    if (topic.length() == 0) {
        topic = MQTT_DISCOVERY_PREFIX;
    }
    topic += "/";
    topic += component;
    topic += "/mygenwashy/";
    topic += id;
    topic += "/config";
    return topic;
}

bool mqttPublish(const char* suffix, const char* payload, bool retained) {
    String topic = mqttTopic(suffix);
    return mqtt.publish(topic.c_str(), payload, retained);
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);
    Serial.println(F("\n========================================"));
    Serial.println(F("  MyGenWashy - ESP32 Controller v2.0"));
    Serial.println(F("  Waschprogramme + Smart Home"));
    Serial.println(F("========================================"));

    nano.begin();
    waterLevel.begin();
    programs.begin();
    integration.begin();
    wash.begin(&nano, &waterLevel);

    // Erstes Programm vorauswaehlen
    if (programs.count() > 0) {
        selectedProgramIdx = 2;  // "Bunt 40" als Default
        if (selectedProgramIdx >= programs.count()) selectedProgramIdx = 0;
    }

    setupWifi();
    setupWebserver();
    setupMqtt();

    Serial.print(F("Integration: "));
    Serial.println(integration.current().mode == INTEGRATION_MODE_ESPHOME_API ?
        F("ESPHome API Alternative") : F("MQTT"));

    if (nano.readStatus()) {
        Serial.println(F("Nano Basis-Controller verbunden"));
    } else {
        Serial.println(F("WARNUNG: Nano nicht erreichbar!"));
    }

    Serial.print(F("Programme geladen: "));
    Serial.println(programs.count());
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
    unsigned long now = millis();

    if (now - lastI2CRead >= I2C_POLL_INTERVAL_MS) {
        lastI2CRead = now;
        nano.readStatus();
    }

    wash.update();

    if (now - lastI2CWrite >= I2C_WRITE_INTERVAL_MS) {
        lastI2CWrite = now;
        nano.writeControl();
    }

    webserver.handleClient();

    if (mqttConfigured) {
        if (!mqtt.connected()) {
            reconnectMqtt();
        }
        mqtt.loop();
        if (now - lastMqttPublish >= 2000) {
            lastMqttPublish = now;
            publishMqttStatus();
        }
    }

    if (now - lastWifiCheck >= 30000) {
        lastWifiCheck = now;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("WiFi verloren, reconnect..."));
            WiFi.reconnect();
        }
    }

    // Debug
    static unsigned long lastDebug = 0;
    if (now - lastDebug >= 5000) {
        lastDebug = now;
        Serial.printf("[ESP] Prog=%s State=%s %d%% T=%.1fC RPM=%d Nano=%s\n",
            wash.getProgramName(), wash.getStateName(), wash.getProgress(),
            nano.status().temperature, nano.status().rpm,
            nano.isConnected() ? "OK" : "LOST");
    }
}

// ============================================================================
// WIFI
// ============================================================================
void setupWifi() {
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.mode(WIFI_STA);

    if (strlen(WIFI_SSID) > 0 && strlen(WIFI_PASS) > 0) {
        Serial.printf("WiFi: %s ...", WIFI_SSID);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
            delay(250);
            Serial.print(".");
        }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("WiFi OK, IP: %s\n", WiFi.localIP().toString().c_str());
            if (MDNS.begin(WIFI_HOSTNAME)) {
                MDNS.addService("http", "tcp", WEBSERVER_PORT);
                Serial.printf("mDNS: http://%s.local\n", WIFI_HOSTNAME);
            }
        } else {
            Serial.println(F("WiFi fehlgeschlagen → AP-Modus"));
            WiFi.mode(WIFI_AP);
            WiFi.softAP(WIFI_SSID_DEFAULT, "mygenwashy");
            Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
        }
    } else {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(WIFI_SSID_DEFAULT, "mygenwashy");
    }
}

// ============================================================================
// WEBSERVER
// ============================================================================
void setupWebserver() {

    // --- API: Status ---
    webserver.on("/api/status", HTTP_GET, []() {
        JsonDocument doc;
        const NanoStatus& ns = nano.status();
        doc["prog_name"]     = wash.getProgramName();
        doc["wash_state"]    = wash.getStateName();
        doc["wash_state_id"] = (uint8_t)wash.getState();
        doc["wash_error"]    = wash.getErrorName();
        doc["progress"]      = wash.getProgress();
        doc["elapsed_min"]   = (uint16_t)(wash.getElapsedMs() / 60000UL);
        doc["target_temp"]   = wash.getTargetTemp();
        doc["wash_rpm"]      = wash.getWashRPM();
        doc["spin_rpm"]      = wash.getSpinRPM();
        doc["wash_motion_profile"] = wash.getWashMotionProfile();
        doc["wash_motion_name"] = wash.getWashMotionProfileName();
        doc["wash_run_sec"]  = wash.getWashRunSec();
        doc["wash_pause_sec"] = wash.getWashPauseSec();
        doc["temperature"]   = serialized(String(ns.temperature, 1));
        doc["rpm"]           = ns.rpm;
        doc["water_low"]     = waterLevel.isLow();
        doc["water_high"]    = waterLevel.isHigh();
        doc["door_locked"]   = nano.isDoorLocked();
        doc["nano_ok"]       = nano.isConnected();
        doc["nano_error"]    = ns.error;
        doc["selected_prog"] = selectedProgramIdx;
        doc["integration_mode"] = integration.current().mode == INTEGRATION_MODE_ESPHOME_API ? "esphome_api" : "mqtt";
        doc["mqtt_configured"] = mqttConfigured;
        doc["mqtt_connected"]  = mqtt.connected();
        doc["wifi_rssi"]     = WiFi.RSSI();
        doc["uptime_sec"]    = (uint32_t)(millis() / 1000);
        String json;
        serializeJson(doc, json);
        webserver.send(200, "application/json", json);
    });

    // --- API: Programmliste ---
    webserver.on("/api/programs", HTTP_GET, []() {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (uint8_t i = 0; i < programs.count(); i++) {
            const WashProgramDef* p = programs.getProgram(i);
            JsonObject obj = arr.add<JsonObject>();
            obj["idx"]    = i;
            obj["name"]   = p->name;
            obj["temp"]   = p->tempC;
            obj["wrpm"]   = p->washRPM;
            obj["srpm"]   = p->spinRPM;
            obj["wmin"]   = p->washMinutes;
            obj["smin"]   = p->spinMinutes;
            obj["wmode"]  = p->washMotionProfile;
            obj["wmode_name"] = washMotionProfileName(p->washMotionProfile);
            obj["won"]    = p->washRunSec;
            obj["woff"]   = p->washPauseSec;
            obj["pre"]    = p->prewash;
            obj["rinse"]  = p->extraRinse;
            obj["custom"] = p->isCustom;
            obj["est"]    = estimatedDurationMin(*p);
        }
        String json;
        serializeJson(doc, json);
        webserver.send(200, "application/json", json);
    });

    // --- API: Programm auswaehlen ---
    webserver.on("/api/select", HTTP_POST, []() {
        if (webserver.hasArg("idx")) {
            int idx = webserver.arg("idx").toInt();
            if (idx >= 0 && idx < programs.count()) {
                selectedProgramIdx = idx;
                webserver.send(200, "application/json", "{\"ok\":true}");
                return;
            }
        }
        webserver.send(400, "application/json", "{\"ok\":false}");
    });

    // --- API: Start (gewaehltes Programm) ---
    webserver.on("/api/start", HTTP_POST, []() {
        const WashProgramDef* p = programs.getProgram(selectedProgramIdx);
        if (p && wash.startProgram(*p)) {
            webserver.send(200, "application/json", "{\"ok\":true}");
        } else {
            webserver.send(400, "application/json", "{\"ok\":false,\"error\":\"Start fehlgeschlagen\"}");
        }
    });

    webserver.on("/api/stop", HTTP_POST, []() {
        wash.stop();
        webserver.send(200, "application/json", "{\"ok\":true}");
    });

    webserver.on("/api/pause", HTTP_POST, []() {
        wash.pause();
        webserver.send(200, "application/json", "{\"ok\":true}");
    });

    webserver.on("/api/resume", HTTP_POST, []() {
        wash.resume();
        webserver.send(200, "application/json", "{\"ok\":true}");
    });

    webserver.on("/api/reset", HTTP_POST, []() {
        wash.resetError();
        webserver.send(200, "application/json", "{\"ok\":true}");
    });

    // --- API: Custom-Programm erstellen ---
    webserver.on("/api/programs/add", HTTP_POST, []() {
        if (!webserver.hasArg("plain")) {
            webserver.send(400, "application/json", "{\"ok\":false,\"error\":\"Kein Body\"}");
            return;
        }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, webserver.arg("plain"));
        if (err) {
            webserver.send(400, "application/json", "{\"ok\":false,\"error\":\"JSON ungueltig\"}");
            return;
        }
        WashProgramDef p;
        memset(&p, 0, sizeof(p));
        strlcpy(p.name, doc["name"] | "", PROGRAM_NAME_MAX);
        p.tempC       = doc["temp"] | 40;
        p.washRPM     = doc["wrpm"] | 55;
        p.spinRPM     = doc["srpm"] | 1200;
        p.washMinutes = doc["wmin"] | 20;
        p.spinMinutes = doc["smin"] | 5;
        p.washMotionProfile = doc["wmode"] | (uint8_t)WMOTION_NORMAL;
        p.washRunSec  = doc["won"] | 12;
        p.washPauseSec = doc["woff"] | 3;
        p.prewash     = doc["pre"]  | false;
        p.extraRinse  = doc["rinse"]| false;
        p.isCustom    = true;

        if (programs.addCustomProgram(p)) {
            webserver.send(200, "application/json", "{\"ok\":true}");
        } else {
            webserver.send(400, "application/json", "{\"ok\":false,\"error\":\"Hinzufuegen fehlgeschlagen\"}");
        }
    });

    // --- API: Custom-Programm loeschen ---
    webserver.on("/api/programs/delete", HTTP_POST, []() {
        if (webserver.hasArg("idx")) {
            int idx = webserver.arg("idx").toInt();
            if (programs.deleteCustomProgram(idx)) {
                if (selectedProgramIdx >= programs.count()) {
                    selectedProgramIdx = 0;
                }
                webserver.send(200, "application/json", "{\"ok\":true}");
                return;
            }
        }
        webserver.send(400, "application/json", "{\"ok\":false}");
    });

    webserver.on("/api/integration", HTTP_GET, []() {
        JsonDocument doc;
        const IntegrationSettingsData& cfg = integration.current();
        doc["mode"] = integration.modeName();
        doc["mqtt_server"] = cfg.mqttServer;
        doc["mqtt_port"] = cfg.mqttPort;
        doc["mqtt_user"] = cfg.mqttUser;
        doc["mqtt_pass"] = cfg.mqttPass;
        doc["mqtt_topic_prefix"] = cfg.mqttTopicPrefix;
        doc["mqtt_discovery_prefix"] = cfg.mqttDiscoveryPrefix;
        doc["mqtt_configured"] = mqttConfigured;
        doc["mqtt_connected"] = mqtt.connected();
        doc["esphome_api_supported"] = false;
        doc["esphome_api_note"] =
            "Die native ESPHome-API ist nicht Teil dieser Arduino-Firmware. "
            "Der Modus dient als Alternativeinstellung und deaktiviert MQTT.";
        String json;
        serializeJson(doc, json);
        webserver.send(200, "application/json", json);
    });

    webserver.on("/api/integration", HTTP_POST, []() {
        if (!webserver.hasArg("plain")) {
            webserver.send(400, "application/json", "{\"ok\":false,\"error\":\"Kein JSON-Body\"}");
            return;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, webserver.arg("plain"));
        if (err) {
            webserver.send(400, "application/json", "{\"ok\":false,\"error\":\"JSON ungueltig\"}");
            return;
        }

        String settingsError;
        if (!integration.applyFromJson(doc.as<JsonObjectConst>(), settingsError)) {
            String json = String("{\"ok\":false,\"error\":\"") + settingsError + "\"}";
            webserver.send(400, "application/json", json);
            return;
        }

        setupMqtt();

        String message = integration.current().mode == INTEGRATION_MODE_ESPHOME_API
            ? "Integration gespeichert. MQTT ist deaktiviert."
            : "Integration gespeichert. MQTT-Konfiguration wurde uebernommen.";
        String json = String("{\"ok\":true,\"message\":\"") + message + "\"}";
        webserver.send(200, "application/json", json);
    });

    // --- Haupt-UI ---
    webserver.on("/", HTTP_GET, []() {
        String html = F(R"rawhtml(
<!DOCTYPE html><html><head>
<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>MyGenWashy</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,-apple-system,sans-serif;background:#0f172a;color:#e2e8f0;padding:12px;max-width:520px;margin:0 auto}
h1{font-size:1.3em;text-align:center;padding:12px 0;color:#38bdf8}
.card{background:#1e293b;border-radius:12px;padding:14px;margin-bottom:10px;border:1px solid #334155}
.card h2{font-size:.85em;color:#94a3b8;margin-bottom:8px;text-transform:uppercase;letter-spacing:.5px}
.status{font-size:1.6em;font-weight:700;color:#f1f5f9}
.prog-name{font-size:1em;color:#38bdf8;margin-top:4px}
.progress{background:#334155;border-radius:8px;height:22px;margin:8px 0;overflow:hidden}
.pbar{background:linear-gradient(90deg,#2563eb,#06b6d4);height:100%;border-radius:8px;transition:width .5s}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:6px}
.grid3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px}
.val{font-size:1.1em;font-weight:600;color:#f8fafc}
.label{font-size:.75em;color:#64748b}
.btn{display:inline-block;padding:10px 16px;border:none;border-radius:8px;font-size:.9em;cursor:pointer;margin:3px;color:#fff;font-weight:600;transition:opacity .2s}
.btn:active{opacity:.7}
.btn-start{background:#22c55e}.btn-stop{background:#ef4444}
.btn-pause{background:#f59e0b}.btn-reset{background:#6366f1}
.err{background:#7f1d1d;color:#fca5a5;padding:10px;border-radius:8px;margin:6px 0;font-size:.9em}
.ok{color:#4ade80}.warn{color:#fbbf24}
select,input{background:#0f172a;color:#e2e8f0;border:1px solid #475569;border-radius:6px;padding:8px;width:100%;font-size:.9em;margin:3px 0}
select:focus,input:focus{outline:none;border-color:#38bdf8}
.prog-detail{font-size:.8em;color:#94a3b8;margin:4px 0}
.tabs{display:flex;gap:4px;margin-bottom:8px}
.tab{flex:1;padding:8px;text-align:center;background:#334155;border-radius:8px 8px 0 0;cursor:pointer;font-size:.85em;color:#94a3b8;border:none}
.tab.active{background:#1e293b;color:#38bdf8;font-weight:600}
.panel{display:none}.panel.active{display:block}
.del-btn{background:#7f1d1d;color:#fca5a5;border:none;border-radius:4px;padding:2px 8px;cursor:pointer;font-size:.8em;float:right}
.form-row{margin:6px 0}
.form-row label{font-size:.8em;color:#94a3b8;display:block;margin-bottom:2px}
.chk{width:auto;margin-right:6px}
.nano-info{font-size:.75em;color:#64748b;margin-top:8px}
.hint{font-size:.8em;color:#94a3b8;margin-top:6px;line-height:1.4}
</style></head><body>
<h1>MyGenWashy</h1>

<div class='card'>
<h2>Status</h2>
<div id='st' class='status'>--</div>
<div id='pn' class='prog-name'></div>
<div class='progress'><div id='pb' class='pbar' style='width:0%'></div></div>
<div id='prog' style='font-size:.85em;color:#94a3b8'>0%</div>
<div id='err' class='err' style='display:none'></div>
</div>

<div class='card grid'>
<div><div id='temp' class='val'>--</div><div class='label'>Temperatur</div></div>
<div><div id='rpm' class='val'>--</div><div class='label'>Drehzahl</div></div>
<div><div id='wl' class='val'>--</div><div class='label'>Wasserstand</div></div>
<div><div id='nano' class='val'>--</div><div class='label'>Controller</div></div>
</div>

<div class='card'>
<div class='tabs'>
<button class='tab active' onclick='showTab(0)'>Programm</button>
<button class='tab' onclick='showTab(1)'>Neu erstellen</button>
<button class='tab' onclick='showTab(2)'>Integration</button>
</div>

<div id='tab0' class='panel active'>
<h2>Programm waehlen</h2>
<select id='psel' onchange='selProg(this.value)'></select>
<div id='pdet' class='prog-detail'></div>
<div style='text-align:center;margin-top:8px'>
<button class='btn btn-start' onclick='cmd("start")'>Start</button>
<button class='btn btn-pause' onclick='cmd("pause")'>Pause</button>
<button class='btn btn-stop' onclick='cmd("stop")'>Stop</button>
<button class='btn btn-reset' onclick='cmd("reset")'>Reset</button>
</div>
</div>

<div id='tab1' class='panel'>
<h2>Eigenes Programm</h2>
<div class='form-row'><label>Name</label><input id='cp_name' maxlength='23' placeholder='z.B. Feinwaesche 30'></div>
<div class='grid'>
<div class='form-row'><label>Temperatur (C)</label><input id='cp_temp' type='number' min='0' max='95' value='40'></div>
<div class='form-row'><label>Wasch-RPM</label><input id='cp_wrpm' type='number' min='20' max='120' value='55'></div>
<div class='form-row'><label>Schleuder-RPM</label><input id='cp_srpm' type='number' min='0' max='1500' value='1200'></div>
<div class='form-row'><label>Waschzeit (min)</label><input id='cp_wmin' type='number' min='0' max='120' value='20'></div>
<div class='form-row'><label>Schleuderzeit (min)</label><input id='cp_smin' type='number' min='0' max='30' value='5'></div>
<div class='form-row'><label>Bewegungsprofil</label><select id='cp_wmode'>
  <option value='0'>Normal</option>
  <option value='1'>Schonend</option>
  <option value='2'>Wolle</option>
  <option value='3'>Benutzerdefiniert</option>
</select></div>
<div class='form-row'><label>Laufzeit je Zyklus (s)</label><input id='cp_won' type='number' min='3' max='120' value='12'></div>
<div class='form-row'><label>Pause je Zyklus (s)</label><input id='cp_woff' type='number' min='0' max='120' value='3'></div>
</div>
<div class='form-row'><label><input type='checkbox' id='cp_pre' class='chk'>Vorwaesche</label></div>
<div class='form-row'><label><input type='checkbox' id='cp_rinse' class='chk'>Extra-Spuelgang</label></div>
<div class='hint'>Reale Waschbewegung wird hier als Lauf/Pause-Rhythmus abgebildet. Ein echter Richtungswechsel ist mit der aktuellen Hardware noch nicht verfuegbar.</div>
<div style='text-align:center;margin-top:8px'>
<button class='btn' style='background:#2563eb' onclick='addProg()'>Speichern</button>
</div>
</div>

<div id='tab2' class='panel'>
<h2>Home Assistant Anbindung</h2>
<div class='form-row'>
  <label>Modus</label>
  <select id='int_mode'>
    <option value='mqtt'>MQTT</option>
    <option value='esphome_api'>ESPHome API Alternative</option>
  </select>
</div>
<div class='grid'>
  <div class='form-row'><label>MQTT Server</label><input id='mqtt_server' placeholder='192.168.x.x'></div>
  <div class='form-row'><label>MQTT Port</label><input id='mqtt_port' type='number' min='1' max='65535' value='1883'></div>
  <div class='form-row'><label>MQTT User</label><input id='mqtt_user'></div>
  <div class='form-row'><label>MQTT Passwort</label><input id='mqtt_pass' type='password'></div>
  <div class='form-row'><label>Topic Prefix</label><input id='mqtt_topic_prefix' value='mygenwashy'></div>
  <div class='form-row'><label>Discovery Prefix</label><input id='mqtt_discovery_prefix' value='homeassistant'></div>
</div>
<div style='text-align:center;margin-top:8px'>
  <button class='btn' style='background:#2563eb' onclick='saveIntegration()'>Integration speichern</button>
</div>
<div class='hint' id='integration_hint'>
  MQTT kann hier direkt konfiguriert werden. Die ESPHome-API-Alternative deaktiviert MQTT in dieser Firmware.
</div>
<div class='nano-info' id='integration_state'></div>
</div>
</div>

<div class='card nano-info' id='nanoinfo'></div>

<script>
var progs=[];
var integrationCfg=null;
var motionDefaults={
  0:{wrpm:55,won:12,woff:3},
  1:{wrpm:40,won:5,woff:10},
  2:{wrpm:25,won:3,woff:27}
};
function showTab(n){
  document.querySelectorAll('.tab').forEach((t,i)=>t.classList.toggle('active',i==n));
  document.querySelectorAll('.panel').forEach((p,i)=>p.classList.toggle('active',i==n));
}
function applyMotionPreset(){
  var mode=parseInt(document.getElementById('cp_wmode').value||'0');
  var preset=motionDefaults[mode];
  if(!preset)return;
  document.getElementById('cp_wrpm').value=preset.wrpm;
  document.getElementById('cp_won').value=preset.won;
  document.getElementById('cp_woff').value=preset.woff;
}
function updateIntegrationUi(){
  var mode=document.getElementById('int_mode').value;
  var disabled=(mode!=='mqtt');
  ['mqtt_server','mqtt_port','mqtt_user','mqtt_pass','mqtt_topic_prefix','mqtt_discovery_prefix']
    .forEach(id=>document.getElementById(id).disabled=disabled);
  document.getElementById('integration_hint').textContent=
    disabled
      ? 'Hinweis: Die native ESPHome-API ist nicht in dieser Arduino-Firmware implementiert. Dieser Modus deaktiviert MQTT und laesst die lokale Web-UI aktiv.'
      : 'MQTT ist aktivierbar. Server, Port und Topics werden in LittleFS gespeichert und nach Neustart wieder geladen.';
}
function loadIntegration(){
  fetch('/api/integration').then(r=>r.json()).then(d=>{
    integrationCfg=d;
    document.getElementById('int_mode').value=d.mode||'mqtt';
    document.getElementById('mqtt_server').value=d.mqtt_server||'';
    document.getElementById('mqtt_port').value=d.mqtt_port||1883;
    document.getElementById('mqtt_user').value=d.mqtt_user||'';
    document.getElementById('mqtt_pass').value=d.mqtt_pass||'';
    document.getElementById('mqtt_topic_prefix').value=d.mqtt_topic_prefix||'mygenwashy';
    document.getElementById('mqtt_discovery_prefix').value=d.mqtt_discovery_prefix||'homeassistant';
    document.getElementById('integration_state').textContent=
      'Modus: '+(d.mode||'mqtt')+' | MQTT konfiguriert: '+(d.mqtt_configured?'Ja':'Nein')+
      ' | MQTT verbunden: '+(d.mqtt_connected?'Ja':'Nein');
    updateIntegrationUi();
  }).catch(()=>{});
}
function saveIntegration(){
  var body={
    mode:document.getElementById('int_mode').value,
    mqtt_server:document.getElementById('mqtt_server').value,
    mqtt_port:parseInt(document.getElementById('mqtt_port').value||'1883'),
    mqtt_user:document.getElementById('mqtt_user').value,
    mqtt_pass:document.getElementById('mqtt_pass').value,
    mqtt_topic_prefix:document.getElementById('mqtt_topic_prefix').value,
    mqtt_discovery_prefix:document.getElementById('mqtt_discovery_prefix').value
  };
  fetch('/api/integration',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(r=>r.json()).then(d=>{
      if(!d.ok){alert('Fehler: '+(d.error||'unbekannt'));return;}
      alert(d.message||'Gespeichert');
      loadIntegration();
      upd();
    });
}
function loadProgs(){
  fetch('/api/programs').then(r=>r.json()).then(d=>{
    progs=d;
    var s=document.getElementById('psel');
    s.innerHTML='';
    d.forEach(p=>{
      var o=document.createElement('option');
      o.value=p.idx;
      o.textContent=p.name+(p.custom?' *':'')+' (~'+p.est+' min)';
      s.appendChild(o);
    });
    fetch('/api/status').then(r=>r.json()).then(st=>{
      s.value=st.selected_prog;
      showDetail(st.selected_prog);
    });
  });
}
function showDetail(idx){
  var p=progs.find(x=>x.idx==parseInt(idx));
  if(!p)return;
  var h=p.temp+'°C';
  if(p.wmin>0){
    h+=' | Waschen: '+p.wrpm+' RPM x '+p.wmin+' min';
    h+=' | Rhythmus: '+(p.wmode_name||'Normal')+' ('+p.won+'s/'+p.woff+'s)';
  }
  if(p.smin>0){
    h+=' | Schleudern: '+p.srpm+' RPM x '+p.smin+' min';
  }
  if(p.pre)h+=' | Vorwaesche';
  if(p.rinse)h+=' | Extra-Spuelung';
  if(p.custom)h+='<br><button class="del-btn" onclick="delProg('+p.idx+')">Loeschen</button>';
  document.getElementById('pdet').innerHTML=h;
}
function selProg(v){
  fetch('/api/select',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'idx='+v});
  showDetail(v);
}
function cmd(c){fetch('/api/'+c,{method:'POST'}).then(()=>upd())}
function addProg(){
  var b={name:document.getElementById('cp_name').value,
    temp:parseInt(document.getElementById('cp_temp').value),
    wrpm:parseInt(document.getElementById('cp_wrpm').value),
    srpm:parseInt(document.getElementById('cp_srpm').value),
    wmin:parseInt(document.getElementById('cp_wmin').value),
    smin:parseInt(document.getElementById('cp_smin').value),
    wmode:parseInt(document.getElementById('cp_wmode').value||'0'),
    won:parseInt(document.getElementById('cp_won').value||'12'),
    woff:parseInt(document.getElementById('cp_woff').value||'3'),
    pre:document.getElementById('cp_pre').checked,
    rinse:document.getElementById('cp_rinse').checked};
  if(!b.name){alert('Bitte Name eingeben');return;}
  fetch('/api/programs/add',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)})
  .then(r=>r.json()).then(d=>{
    if(d.ok){document.getElementById('cp_name').value='';loadProgs();showTab(0);}
    else alert('Fehler: '+(d.error||'unbekannt'));
  });
}
function delProg(idx){
  if(!confirm('Programm loeschen?'))return;
  fetch('/api/programs/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'idx='+idx})
  .then(()=>loadProgs());
}
function upd(){fetch('/api/status').then(r=>r.json()).then(d=>{
  document.getElementById('st').textContent=d.wash_state;
  document.getElementById('pn').textContent=d.prog_name||'';
  document.getElementById('pb').style.width=d.progress+'%';
  document.getElementById('prog').textContent=d.progress+'% — '+d.elapsed_min+' min';
  document.getElementById('temp').textContent=d.temperature+' °C';
  document.getElementById('rpm').textContent=d.rpm+' RPM';
  var wl=(d.water_low?'LOW ':'')+(d.water_high?'HIGH':'');
  document.getElementById('wl').textContent=wl||'Leer';
  document.getElementById('nano').innerHTML=d.nano_ok?
    '<span class="ok">OK</span>':'<span class="warn">Getrennt</span>';
  var ee=document.getElementById('err');
  if(d.wash_error&&d.wash_error!='Kein Fehler'){ee.textContent=d.wash_error;ee.style.display='block';}
  else{ee.style.display='none';}
  document.getElementById('nanoinfo').textContent=
    'WiFi: '+d.wifi_rssi+' dBm | Uptime: '+Math.floor(d.uptime_sec/60)+' min | Nano-Fehler: '+d.nano_error+
    ' | Integration: '+(d.integration_mode||'mqtt')+' | MQTT: '+(d.mqtt_connected?'verbunden':'aus/getrennt');
}).catch(()=>{})}
document.getElementById('int_mode').addEventListener('change',updateIntegrationUi);
document.getElementById('cp_wmode').addEventListener('change',applyMotionPreset);
loadProgs();
loadIntegration();
setInterval(upd,1500);upd();
</script></body></html>
)rawhtml");
        webserver.send(200, "text/html", html);
    });

    webserver.begin();
    Serial.printf("Webserver auf Port %d\n", WEBSERVER_PORT);
}

// ============================================================================
// MQTT — Home Assistant Auto-Discovery
// ============================================================================
void setupMqtt() {
    mqtt.disconnect();
    mqttConfigured = false;
    mqttDiscoverySent = false;

    if (!integrationUsesMqtt()) {
        Serial.println(F("Integration: ESPHome API Alternative gewaehlt, MQTT deaktiviert"));
        return;
    }

    if (strlen(integration.current().mqttServer) == 0) {
        mqttConfigured = false;
        Serial.println(F("MQTT: Kein Server (optional)"));
        return;
    }

    mqtt.setServer(integration.current().mqttServer, integration.current().mqttPort);
    mqtt.setCallback(handleMqttMessage);
    mqtt.setBufferSize(1024);
    mqttConfigured = true;
    Serial.printf("MQTT: %s:%u\n", integration.current().mqttServer, integration.current().mqttPort);
}

void reconnectMqtt() {
    static unsigned long lastAttempt = 0;
    if (millis() - lastAttempt < 5000) return;
    lastAttempt = millis();
    if (WiFi.status() != WL_CONNECTED) return;

    Serial.print(F("MQTT reconnect... "));
    bool ok;
    if (strlen(integration.current().mqttUser) > 0) {
        ok = mqtt.connect(WIFI_HOSTNAME, integration.current().mqttUser, integration.current().mqttPass);
    } else {
        ok = mqtt.connect(WIFI_HOSTNAME);
    }
    if (ok) {
        Serial.println(F("OK"));
        String cmdTopic = mqttTopic("cmd/#");
        String selectTopic = mqttTopic("select/program/set");
        mqtt.subscribe(cmdTopic.c_str());
        mqtt.subscribe(selectTopic.c_str());
        mqttDiscoverySent = false;
        publishMqttDiscovery();
    } else {
        Serial.printf("Fehler rc=%d\n", mqtt.state());
    }
}

void handleMqttMessage(char* topic, byte* payload, unsigned int length) {
    String t = String(topic);
    String p = "";
    for (unsigned int i = 0; i < length; i++) p += (char)payload[i];

    Serial.printf("MQTT rx: %s = %s\n", topic, p.c_str());

    if (t.endsWith("/cmd/start")) {
        const WashProgramDef* prog = programs.getProgram(selectedProgramIdx);
        if (prog) wash.startProgram(*prog);
    }
    if (t.endsWith("/cmd/stop"))   wash.stop();
    if (t.endsWith("/cmd/pause"))  wash.pause();
    if (t.endsWith("/cmd/resume")) wash.resume();
    if (t.endsWith("/cmd/reset"))  wash.resetError();

    // Programmwahl per Name (HA Select Entity)
    if (t.endsWith("/select/program/set")) {
        int8_t idx = programs.findByName(p.c_str());
        if (idx >= 0) {
            selectedProgramIdx = idx;
            mqttPublish("select/program/state", p.c_str(), true);
        }
    }
}

void publishMqttStatus() {
    if (!mqtt.connected()) return;
    const NanoStatus& ns = nano.status();

    // JSON Status
    JsonDocument doc;
    doc["state"]       = wash.getStateName();
    doc["program"]     = wash.getProgramName();
    doc["error"]       = wash.getErrorName();
    doc["progress"]    = wash.getProgress();
    doc["temperature"] = serialized(String(ns.temperature, 1));
    doc["rpm"]         = ns.rpm;
    doc["water_low"]   = waterLevel.isLow();
    doc["water_high"]  = waterLevel.isHigh();
    doc["door_locked"] = nano.isDoorLocked();
    doc["nano_ok"]     = nano.isConnected();
    doc["target_temp"] = wash.getTargetTemp();
    String json;
    serializeJson(doc, json);
    mqttPublish("status", json.c_str(), true);

    // Einzelne Werte fuer HA-Entities
    mqttPublish("sensor/temperature/state", String(ns.temperature, 1).c_str(), true);
    mqttPublish("sensor/rpm/state", String(ns.rpm).c_str(), true);
    mqttPublish("sensor/progress/state", String(wash.getProgress()).c_str(), true);
    mqttPublish("sensor/state/state", wash.getStateName(), true);
    mqttPublish("sensor/program/state", wash.getProgramName(), true);

    // Binary sensors
    mqttPublish("binary_sensor/door/state", nano.isDoorLocked() ? "ON" : "OFF", true);
    mqttPublish("binary_sensor/nano/state", nano.isConnected() ? "ON" : "OFF", true);
    mqttPublish("binary_sensor/error/state", (wash.getError() != WERR_NONE) ? "ON" : "OFF", true);
    mqttPublish("binary_sensor/water_low/state", waterLevel.isLow() ? "ON" : "OFF", true);
    mqttPublish("binary_sensor/water_high/state", waterLevel.isHigh() ? "ON" : "OFF", true);

    // Select state
    const WashProgramDef* sp = programs.getProgram(selectedProgramIdx);
    if (sp) mqttPublish("select/program/state", sp->name, true);
}

// HA MQTT Auto-Discovery mit Device-Info
void _publishDiscovery(const char* component, const char* id, JsonDocument& doc) {
    // Device info (gleich fuer alle Entities)
    JsonObject dev = doc["dev"].to<JsonObject>();
    dev["ids"][0]  = "mygenwashy";
    dev["name"]    = "MyGenWashy";
    dev["mf"]      = "MayerMakes / TuttleButtle";
    dev["mdl"]     = "MyGenWashy v2";
    dev["sw"]      = "2.0";

    String topic = mqttDiscoveryTopic(component, id);
    String json;
    serializeJson(doc, json);
    mqtt.publish(topic.c_str(), json.c_str(), true);
}

void publishMqttDiscovery() {
    if (!mqtt.connected() || mqttDiscoverySent) return;
    mqttDiscoverySent = true;
    Serial.println(F("MQTT: Sende HA Discovery..."));

    // --- Sensoren ---
    {
        JsonDocument d; d["name"]="Temperatur"; d["uniq_id"]="mgw_temp";
        d["stat_t"]=mqttTopic("sensor/temperature/state");
        d["unit_of_meas"]="°C"; d["dev_cla"]="temperature"; d["ic"]="mdi:thermometer-water";
        _publishDiscovery("sensor","temperature",d);
    }
    {
        JsonDocument d; d["name"]="Drehzahl"; d["uniq_id"]="mgw_rpm";
        d["stat_t"]=mqttTopic("sensor/rpm/state");
        d["unit_of_meas"]="RPM"; d["ic"]="mdi:fan";
        _publishDiscovery("sensor","rpm",d);
    }
    {
        JsonDocument d; d["name"]="Fortschritt"; d["uniq_id"]="mgw_progress";
        d["stat_t"]=mqttTopic("sensor/progress/state");
        d["unit_of_meas"]="%"; d["ic"]="mdi:progress-check";
        _publishDiscovery("sensor","progress",d);
    }
    {
        JsonDocument d; d["name"]="Status"; d["uniq_id"]="mgw_state";
        d["stat_t"]=mqttTopic("sensor/state/state");
        d["ic"]="mdi:washing-machine";
        _publishDiscovery("sensor","state",d);
    }
    {
        JsonDocument d; d["name"]="Programm"; d["uniq_id"]="mgw_program";
        d["stat_t"]=mqttTopic("sensor/program/state");
        d["ic"]="mdi:playlist-play";
        _publishDiscovery("sensor","program",d);
    }

    // --- Binary Sensors ---
    {
        JsonDocument d; d["name"]="Tuer verriegelt"; d["uniq_id"]="mgw_door";
        d["stat_t"]=mqttTopic("binary_sensor/door/state");
        d["dev_cla"]="lock"; d["ic"]="mdi:door-closed-lock";
        _publishDiscovery("binary_sensor","door",d);
    }
    {
        JsonDocument d; d["name"]="Controller verbunden"; d["uniq_id"]="mgw_nano";
        d["stat_t"]=mqttTopic("binary_sensor/nano/state");
        d["dev_cla"]="connectivity"; d["ic"]="mdi:chip";
        _publishDiscovery("binary_sensor","nano",d);
    }
    {
        JsonDocument d; d["name"]="Fehler"; d["uniq_id"]="mgw_error";
        d["stat_t"]=mqttTopic("binary_sensor/error/state");
        d["dev_cla"]="problem"; d["ic"]="mdi:alert";
        _publishDiscovery("binary_sensor","error",d);
    }
    {
        JsonDocument d; d["name"]="Wasserstand LOW";
        d["uniq_id"]="mgw_water_low";
        d["stat_t"]=mqttTopic("binary_sensor/water_low/state");
        d["ic"]="mdi:waves-arrow-up";
        _publishDiscovery("binary_sensor","water_low",d);
    }
    {
        JsonDocument d; d["name"]="Wasserstand HIGH";
        d["uniq_id"]="mgw_water_high";
        d["stat_t"]=mqttTopic("binary_sensor/water_high/state");
        d["ic"]="mdi:waves-arrow-up";
        _publishDiscovery("binary_sensor","water_high",d);
    }

    // --- Buttons ---
    const char* btns[][3] = {
        {"Start",  "mgw_btn_start",  "cmd/start"},
        {"Stop",   "mgw_btn_stop",   "cmd/stop"},
        {"Pause",  "mgw_btn_pause",  "cmd/pause"},
        {"Resume", "mgw_btn_resume", "cmd/resume"},
        {"Reset",  "mgw_btn_reset",  "cmd/reset"},
    };
    for (int i = 0; i < 5; i++) {
        JsonDocument d;
        d["name"] = btns[i][0];
        d["uniq_id"] = btns[i][1];
        d["cmd_t"] = mqttTopic(btns[i][2]);
        d["ic"] = (i==0) ? "mdi:play" : (i==1) ? "mdi:stop" : (i==2) ? "mdi:pause" : (i==3) ? "mdi:play-pause" : "mdi:restart";
        _publishDiscovery("button", btns[i][1], d);
    }

    // --- Select: Programmwahl ---
    {
        JsonDocument d; d["name"]="Waschprogramm"; d["uniq_id"]="mgw_sel_prog";
        d["cmd_t"]=mqttTopic("select/program/set");
        d["stat_t"]=mqttTopic("select/program/state");
        d["ic"]="mdi:washing-machine";
        JsonArray opts = d["options"].to<JsonArray>();
        for (uint8_t i = 0; i < programs.count(); i++) {
            const WashProgramDef* p = programs.getProgram(i);
            opts.add(p->name);
        }
        _publishDiscovery("select","program",d);
    }

    Serial.printf("MQTT: %d Discovery-Messages gesendet\n",
        5 + 5 + 5 + 1);  // sensors + binary + buttons + select
}
