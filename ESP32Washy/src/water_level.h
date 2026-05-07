#ifndef WATER_LEVEL_H
#define WATER_LEVEL_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// Wasserstand-Sensor Modul (ESP32)
//
// Drei Betriebsmodi (gesteuert ueber WATER_LEVEL_MODE in config.h):
//
//   Mode 0 — FLOAT_SWITCH (Produktion)
//     Echte Schwimmerschalter an GPIO34 (LOW) und GPIO35 (HIGH).
//     Active-Low mit externen 10k Pull-ups nach 3.3V.
//
//   Mode 1 — POTI_TEST (Hardware-Testbetrieb)
//     Poti an GPIO32 (ADC1_CH4), 12-bit Aufloesung.
//     3 Zonen: 0..1364 = Leer, 1365..2729 = LOW, 2730..4095 = LOW+HIGH.
//
//   Mode 2 — nur Timer (Software-Test, kein Sensor noetig)
//     Wasserstand wird rein zeitgesteuert simuliert.
//     Die Ablaufsteuerung meldet per notifyFilling()/notifyDraining()
//     ob gerade gefuellt oder abgepumpt wird. Das Modul simuliert
//     daraus realistische Wasserstandswechsel:
//       Fuellen: LOW nach 10s, HIGH nach 20s
//       Abpumpen: Leer nach 20s
//     Im Ruhezustand meldet es "Leer".
//
// ============================================================================

class WaterLevel {
public:
    void begin() {
#if WATER_LEVEL_MODE == 0
        // GPIO34/35 sind input-only — KEIN interner Pull-up moeglich
        // Externe 10k Pull-ups nach 3.3V sind Pflicht!
        pinMode(PIN_WATER_LOW, INPUT);
        pinMode(PIN_WATER_HIGH, INPUT);
        Serial.println(F("[WATER] Mode 0: Schwimmerschalter (GPIO34/35)"));
#elif WATER_LEVEL_MODE == 1
        pinMode(PIN_POTI_WATER, INPUT);
        analogReadResolution(12);
        Serial.println(F("[WATER] Mode 1: Poti-Test (GPIO32)"));
#elif WATER_LEVEL_MODE == 2
        _filling = false;
        _draining = false;
        _fillStartMs = 0;
        _drainStartMs = 0;
        Serial.println(F("[WATER] Mode 2: nur Timer (kein Sensor)"));
#endif
    }

    // --- Hauptabfragen ---

    bool isLow() const {
#if WATER_LEVEL_MODE == 0
        return digitalRead(PIN_WATER_LOW) == WATER_SENSOR_ACTIVE;
#elif WATER_LEVEL_MODE == 1
        return _readPoti() >= POTI_WATER_LOW_THRESH;
#else
        return _simIsLow();
#endif
    }

    bool isHigh() const {
#if WATER_LEVEL_MODE == 0
        return digitalRead(PIN_WATER_HIGH) == WATER_SENSOR_ACTIVE;
#elif WATER_LEVEL_MODE == 1
        return _readPoti() >= POTI_WATER_HIGH_THRESH;
#else
        return _simIsHigh();
#endif
    }

    // Bitfeld fuer Status-API (kompatibel mit WLEVEL_LOW / WLEVEL_HIGH)
    uint8_t getLevelBits() const {
        uint8_t bits = 0;
        if (isLow())  bits |= WLEVEL_LOW;
        if (isHigh()) bits |= WLEVEL_HIGH;
        return bits;
    }

    // --- Benachrichtigungen von der Ablaufsteuerung ---
    // Werden nur im Timer-Modus (Mode 2) ausgewertet.
    // In Mode 0/1 sind es No-Ops.

    void notifyFilling(bool on) {
#if WATER_LEVEL_MODE == 2
        if (on && !_filling) {
            _fillStartMs = millis();
            _draining = false;
        }
        _filling = on;
#else
        (void)on;
#endif
    }

    void notifyDraining(bool on) {
#if WATER_LEVEL_MODE == 2
        if (on && !_draining) {
            _drainStartMs = millis();
            _filling = false;
        }
        _draining = on;
#else
        (void)on;
#endif
    }

    // --- Info ---
    const char* getModeName() const {
#if WATER_LEVEL_MODE == 0
        return "Schwimmerschalter";
#elif WATER_LEVEL_MODE == 1
        return "Poti-Test";
#else
        return "nur Timer";
#endif
    }

    uint8_t getMode() const { return WATER_LEVEL_MODE; }

private:

#if WATER_LEVEL_MODE == 1
    static uint16_t _readPoti() {
        uint32_t sum = 0;
        for (uint8_t i = 0; i < 4; i++) {
            sum += analogRead(PIN_POTI_WATER);
            delayMicroseconds(50);
        }
        return (uint16_t)(sum / 4);
    }
#endif

#if WATER_LEVEL_MODE == 2
    bool _filling;
    bool _draining;
    unsigned long _fillStartMs;
    unsigned long _drainStartMs;

    // nur Timer: Fuellen
    //   Nach SIM_FILL_LOW_MS  → LOW aktiv (Wasser bis Mindestfuellstand)
    //   Nach SIM_FILL_HIGH_MS → HIGH aktiv (Trommel voll)
    // Abpumpen:
    //   Sofort → LOW noch aktiv (Wasser vorhanden)
    //   Nach SIM_DRAIN_EMPTY_MS → Leer (LOW weg)
    // Ruhezustand: Leer

    bool _simIsLow() const {
        if (_filling) {
            return (millis() - _fillStartMs) >= SIM_FILL_LOW_MS;
        }
        if (_draining) {
            // Wasser sinkt: LOW bleibt aktiv bis Drain-Timer abgelaufen
            return (millis() - _drainStartMs) < SIM_DRAIN_EMPTY_MS;
        }
        return false;  // Ruhezustand = Leer
    }

    bool _simIsHigh() const {
        if (_filling) {
            return (millis() - _fillStartMs) >= SIM_FILL_HIGH_MS;
        }
        // Beim Abpumpen und im Ruhezustand: HIGH nie aktiv
        return false;
    }
#endif
};

#endif // WATER_LEVEL_H
