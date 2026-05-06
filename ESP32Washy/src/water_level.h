#ifndef WATER_LEVEL_H
#define WATER_LEVEL_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// Wasserstand-Sensor Modul (ESP32)
//
// Liest die Schwimmerschalter (oder Poti im Testmodus) direkt am ESP32.
// Zwei Sensoren:
//   - LOW:  Wasser hat Mindestfuellstand erreicht
//   - HIGH: Trommel ist voll
//
// Im WATER_POTI_TEST Modus wird ein Poti an GPIO32 gelesen:
//   0..1364   = Leer (kein Sensor aktiv)
//   1365..2729 = LOW aktiv
//   2730..4095 = LOW + HIGH aktiv
// ============================================================================

class WaterLevel {
public:
    void begin() {
#if WATER_POTI_TEST
        pinMode(PIN_POTI_WATER, INPUT);
        // ESP32 ADC: 12-bit default (0..4095)
        analogReadResolution(12);
        Serial.println(F("[WATER] Poti-Testmodus (GPIO32)"));
#else
        // GPIO34 und GPIO35 sind input-only, kein interner Pullup moeglich!
        // Externer Pullup (10k nach 3.3V) erforderlich.
        pinMode(PIN_WATER_LOW, INPUT);
        pinMode(PIN_WATER_HIGH, INPUT);
        Serial.println(F("[WATER] Schwimmerschalter (GPIO34/35)"));
#endif
    }

    bool isLow() const {
#if WATER_POTI_TEST
        return _readPoti() >= POTI_WATER_LOW_THRESH;
#else
        return digitalRead(PIN_WATER_LOW) == WATER_SENSOR_ACTIVE;
#endif
    }

    bool isHigh() const {
#if WATER_POTI_TEST
        return _readPoti() >= POTI_WATER_HIGH_THRESH;
#else
        return digitalRead(PIN_WATER_HIGH) == WATER_SENSOR_ACTIVE;
#endif
    }

    // Bitfeld fuer I2C / Status (kompatibel mit WLEVEL_LOW / WLEVEL_HIGH)
    uint8_t getLevelBits() const {
        uint8_t bits = 0;
        if (isLow())  bits |= WLEVEL_LOW;
        if (isHigh()) bits |= WLEVEL_HIGH;
        return bits;
    }

private:
#if WATER_POTI_TEST
    static uint16_t _readPoti() {
        uint32_t sum = 0;
        for (uint8_t i = 0; i < 4; i++) {
            sum += analogRead(PIN_POTI_WATER);
            delayMicroseconds(50);
        }
        return (uint16_t)(sum / 4);
    }
#endif
};

#endif // WATER_LEVEL_H
