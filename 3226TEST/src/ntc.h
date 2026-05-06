#ifndef NTC_H
#define NTC_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// NTC 5k Thermistor - Temperature Measurement
// Circuit: +3.3V → NTC 5k → ADC pin (PB2) → 5k resistor → GND
// Voltage divider: V_adc = 3.3V * R_series / (R_ntc + R_series)
// ============================================================================

class NTCSensor {
public:
    void begin() {
        analogReference(VDD);  // 3.3V reference on ATTiny3226
        // ATTiny3226 has 12-bit ADC (0-4095) but Arduino core defaults to 10-bit
        // analogReadResolution(12);  // Use if megaTinyCore supports it
        _temperature = 0.0f;
        _lastReadMs = 0;
    }

    // Call periodically (not too fast, ADC needs settling time)
    float readTemperature() {
        // Oversample 8x for noise reduction
        uint32_t sum = 0;
        for (uint8_t i = 0; i < 8; i++) {
            sum += analogRead(PIN_NTC);
            delayMicroseconds(100);
        }
        uint16_t adcValue = sum / 8;

        if (adcValue == 0 || adcValue >= 1023) {
            // Sensor disconnected or shorted
            _temperature = -999.0f;
            return _temperature;
        }

        // Calculate NTC resistance from voltage divider
        // V_adc = Vcc * R_series / (R_ntc + R_series)
        // R_ntc = R_series * (ADC_MAX / adcValue - 1)
        float resistance = (float)NTC_SERIES_R * ((1023.0f / (float)adcValue) - 1.0f);

        // Steinhart-Hart simplified (Beta equation)
        // 1/T = 1/T0 + (1/B) * ln(R/R0)
        float steinhart = resistance / (float)NTC_NOMINAL_R;  // R/R0
        steinhart = log(steinhart);                             // ln(R/R0)
        steinhart /= (float)NTC_BETA;                          // 1/B * ln(R/R0)
        steinhart += 1.0f / ((float)NTC_NOMINAL_TEMP + 273.15f); // + 1/T0
        steinhart = 1.0f / steinhart;                           // Invert
        steinhart -= 273.15f;                                   // Convert to °C

        _temperature = steinhart;
        _lastReadMs = millis();
        return _temperature;
    }

    float getTemperature() const { return _temperature; }

    // Get integer and fractional parts for I2C registers
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
    unsigned long _lastReadMs;
};

#endif // NTC_H
