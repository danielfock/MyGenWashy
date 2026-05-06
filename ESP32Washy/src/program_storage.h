#ifndef PROGRAM_STORAGE_H
#define PROGRAM_STORAGE_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "wash_programs.h"

// ============================================================================
// Programm-Speicher (LittleFS + JSON)
//
// Speichert benutzerdefinierte Waschprogramme als JSON auf dem ESP32-Flash.
// Die vordefinierten Programme kommen aus wash_programs.h (ROM).
//
// Datei: /programs.json
// Format: Array von Programm-Objekten
//
// Ueberlebt Neustarts und OTA-Updates (LittleFS wird nicht ueberschrieben).
// ============================================================================

#define PROGRAMS_FILE "/programs.json"

class ProgramStorage {
public:
    void begin() {
        if (!LittleFS.begin(true)) {  // true = formatOnFail
            Serial.println(F("[STORAGE] LittleFS mount fehlgeschlagen!"));
            _ready = false;
            return;
        }
        _ready = true;
        Serial.println(F("[STORAGE] LittleFS bereit"));

        // Vordefinierte Programme laden
        _totalCount = NUM_DEFAULT_PROGRAMS;
        for (uint8_t i = 0; i < NUM_DEFAULT_PROGRAMS; i++) {
            memcpy(&_programs[i], &DEFAULT_PROGRAMS[i], sizeof(WashProgramDef));
        }

        // Benutzerdefinierte Programme aus Flash laden
        _loadCustomPrograms();

        Serial.print(F("[STORAGE] "));
        Serial.print(_totalCount);
        Serial.println(F(" Programme geladen"));
    }

    // --- Getter ---
    uint8_t count() const { return _totalCount; }

    const WashProgramDef* getProgram(uint8_t index) const {
        if (index >= _totalCount) return nullptr;
        return &_programs[index];
    }

    // Programm nach Name finden (gibt Index zurueck, -1 wenn nicht gefunden)
    int8_t findByName(const char* name) const {
        for (uint8_t i = 0; i < _totalCount; i++) {
            if (strcmp(_programs[i].name, name) == 0) return i;
        }
        return -1;
    }

    // Liste aller Programmnamen (fuer MQTT Select / Web-UI)
    String getProgramList() const {
        String list = "[";
        for (uint8_t i = 0; i < _totalCount; i++) {
            if (i > 0) list += ",";
            list += "\"";
            list += _programs[i].name;
            list += "\"";
        }
        list += "]";
        return list;
    }

    // --- Benutzerdefinierte Programme verwalten ---

    bool addCustomProgram(const WashProgramDef& prog) {
        WashProgramDef normalized = prog;
        normalizeWashMotion(normalized);
        if (_totalCount >= MAX_TOTAL_PROGRAMS) {
            Serial.println(F("[STORAGE] Max Programme erreicht"));
            return false;
        }
        if (!isValidProgram(normalized)) {
            Serial.println(F("[STORAGE] Ungueltige Programmdaten"));
            return false;
        }
        // Duplikat-Check
        if (findByName(normalized.name) >= 0) {
            Serial.println(F("[STORAGE] Name existiert bereits"));
            return false;
        }

        memcpy(&_programs[_totalCount], &normalized, sizeof(WashProgramDef));
        _programs[_totalCount].isCustom = true;
        _totalCount++;

        _saveCustomPrograms();
        Serial.print(F("[STORAGE] Programm hinzugefuegt: "));
        Serial.println(normalized.name);
        return true;
    }

    bool updateCustomProgram(uint8_t index, const WashProgramDef& prog) {
        WashProgramDef normalized = prog;
        normalizeWashMotion(normalized);
        if (index >= _totalCount) return false;
        if (!_programs[index].isCustom) {
            Serial.println(F("[STORAGE] Standard-Programme nicht aenderbar"));
            return false;
        }
        if (!isValidProgram(normalized)) return false;

        memcpy(&_programs[index], &normalized, sizeof(WashProgramDef));
        _programs[index].isCustom = true;
        _saveCustomPrograms();
        return true;
    }

    bool deleteCustomProgram(uint8_t index) {
        if (index >= _totalCount) return false;
        if (!_programs[index].isCustom) {
            Serial.println(F("[STORAGE] Standard-Programme nicht loeschbar"));
            return false;
        }

        Serial.print(F("[STORAGE] Loesche: "));
        Serial.println(_programs[index].name);

        // Luecke schliessen
        for (uint8_t i = index; i < _totalCount - 1; i++) {
            memcpy(&_programs[i], &_programs[i + 1], sizeof(WashProgramDef));
        }
        _totalCount--;

        _saveCustomPrograms();
        return true;
    }

private:
    bool _ready = false;
    WashProgramDef _programs[MAX_TOTAL_PROGRAMS];
    uint8_t _totalCount = 0;

    void _loadCustomPrograms() {
        if (!_ready) return;

        File file = LittleFS.open(PROGRAMS_FILE, "r");
        if (!file) {
            Serial.println(F("[STORAGE] Keine gespeicherten Programme"));
            return;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, file);
        file.close();

        if (err) {
            Serial.print(F("[STORAGE] JSON Parse-Fehler: "));
            Serial.println(err.c_str());
            return;
        }

        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject obj : arr) {
            if (_totalCount >= MAX_TOTAL_PROGRAMS) break;

            WashProgramDef p;
            memset(&p, 0, sizeof(p));
            bool legacyProgram = !obj["wmode"].is<int>() && !obj["won"].is<int>() && !obj["woff"].is<int>();
            strlcpy(p.name, obj["name"] | "?", PROGRAM_NAME_MAX);
            p.tempC       = obj["temp"] | 40;
            p.washRPM     = obj["wrpm"] | 55;
            p.spinRPM     = obj["srpm"] | 1200;
            p.washMinutes = obj["wmin"] | 20;
            p.spinMinutes = obj["smin"] | 5;
            p.washMotionProfile = obj["wmode"] | (uint8_t)WMOTION_NORMAL;
            p.washRunSec  = obj["won"] | 0;
            p.washPauseSec = obj["woff"] | 0;
            p.prewash     = obj["pre"]  | false;
            p.extraRinse  = obj["rinse"]| false;
            p.isCustom    = true;
            normalizeWashMotion(p, legacyProgram);

            if (isValidProgram(p)) {
                memcpy(&_programs[_totalCount], &p, sizeof(WashProgramDef));
                _totalCount++;
            }
        }
    }

    void _saveCustomPrograms() {
        if (!_ready) return;

        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (uint8_t i = 0; i < _totalCount; i++) {
            if (!_programs[i].isCustom) continue;

            JsonObject obj = arr.add<JsonObject>();
            obj["name"]  = _programs[i].name;
            obj["temp"]  = _programs[i].tempC;
            obj["wrpm"]  = _programs[i].washRPM;
            obj["srpm"]  = _programs[i].spinRPM;
            obj["wmin"]  = _programs[i].washMinutes;
            obj["smin"]  = _programs[i].spinMinutes;
            obj["wmode"] = _programs[i].washMotionProfile;
            obj["won"]   = _programs[i].washRunSec;
            obj["woff"]  = _programs[i].washPauseSec;
            obj["pre"]   = _programs[i].prewash;
            obj["rinse"] = _programs[i].extraRinse;
        }

        File file = LittleFS.open(PROGRAMS_FILE, "w");
        if (!file) {
            Serial.println(F("[STORAGE] Datei schreiben fehlgeschlagen!"));
            return;
        }
        serializeJson(doc, file);
        file.close();

        Serial.println(F("[STORAGE] Programme gespeichert"));
    }
};

#endif // PROGRAM_STORAGE_H
