#ifndef NTC_H
#define NTC_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// NTC 5k Thermistor - Temperature Measurement (ATmega328P Version)
//
// Circuit: +3.3V → NTC 5k → ADC pin (A0) → 5k resistor → GND
// ATmega328P: 10-bit ADC (0-1023), reference = DEFAULT (5V) oder EXTERNAL
//
// WICHTIG: Der Nano läuft auf 5V, aber der NTC-Spannungsteiler ist auf 3.3V
// ausgelegt. Daher muss entweder:
//   a) analogReference(EXTERNAL) mit 3.3V an AREF-Pin, oder
//   b) der Spannungsteiler an 5V angeschlossen werden (R_series anpassen)
//
// Hier: Wir nehmen an, der Spannungsteiler hängt an 3.3V und AREF
//       wird extern auf 3.3V gelegt (Jumper von 3V3 Pin → AREF Pin).
// ============================================================================

class NTCSensor {
public:

// ========================================================================
// POTI TEST MODE: Temperatur wird von einem Poti an A1 gelesen
//   Linksanschlag (0)    = POTI_TEMP_MIN °C
//   Rechtsanschlag (1023) = POTI_TEMP_MAX °C
// ========================================================================
#if POTI_TEST_MODE

    void begin() {
        // Im Testmodus: DEFAULT Reference (5V), Poti-Pin lesen
        analogReference(DEFAULT);
        analogRead(PIN_POTI_TEMP);
        delay(10);
        _temperature = 0.0f;
    }

    float readTemperature() {
        // Oversample 4x fuer stabilen Wert
        uint32_t sum = 0;
        for (uint8_t i = 0; i < 4; i++) {
            sum += analogRead(PIN_POTI_TEMP);
            delayMicroseconds(100);
        }
        uint16_t adcValue = sum / 4;

        // Lineares Mapping: 0..1023 → POTI_TEMP_MIN..POTI_TEMP_MAX
        _temperature = POTI_TEMP_MIN + (POTI_TEMP_MAX - POTI_TEMP_MIN)
                       * ((float)adcValue / 1023.0f);
        return _temperature;
    }

// ========================================================================
// ORIGINAL: NTC 5k Thermistor mit Steinhart-Hart (Beta-Gleichung)
// ========================================================================
#else

    void begin() {
        analogReference(EXTERNAL);  // AREF Pin muss mit 3.3V verbunden sein!
        // Dummy-Read nach Reference-Wechsel
        analogRead(PIN_NTC);
        delay(10);
        _temperature = 0.0f;
    }

    float readTemperature() {
        // Oversample 8x for noise reduction
        uint32_t sum = 0;
        for (uint8_t i = 0; i < 8; i++) {
            sum += analogRead(PIN_NTC);
            delayMicroseconds(100);
        }
        uint16_t adcValue = sum / 8;

        if (adcValue == 0 || adcValue >= 1023) {
            _temperature = -999.0f;
            return _temperature;
        }

        // Calculate NTC resistance from voltage divider
        // R_ntc = R_series * (ADC_MAX / adcValue - 1)
        float resistance = (float)NTC_SERIES_R * ((1023.0f / (float)adcValue) - 1.0f);

        // Steinhart-Hart simplified (Beta equation)
        float steinhart = resistance / (float)NTC_NOMINAL_R;
        steinhart = log(steinhart);
        steinhart /= (float)NTC_BETA;
        steinhart += 1.0f / ((float)NTC_NOMINAL_TEMP + 273.15f);
        steinhart = 1.0f / steinhart;
        steinhart -= 273.15f;

        _temperature = steinhart;
        return _temperature;
    }

#endif // POTI_TEST_MODE

    float getTemperature() const { return _temperature; }

    uint8_t getTempInt() const {
        if (_temperature < 0) return 0;
        return (uint8_t)_temperature;
    }
    uint8_t getTempFrac() const {
        if (_temperature < 0) return 0;
        return (uint8_t)((_temperature - (float)getTempInt()) * 10.0f);
    }

    bool isSensorOk() const {
        return _temperature > -100.0f && _temperature < 150.0f;
    }

    bool isOverheat() const {
        return _temperature >= (float)MAX_TEMP_C;
    }

private:
    float _temperature;
};

#endif // NTC_H
