#ifndef MOTOR_H
#define MOTOR_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// Motor Control - TRIAC Phase-Angle Control with Zero-Cross Detection
//
// Hardware:
//   - TRIAC BTB16-600B driven via MOC3020M optocoupler
//   - MOTOR_PWM (PA3) → R1 1k → MOC3020M → R2 330 → TRIAC gate
//   - Zero-cross detection via PC817 optocoupler → ZERO_POINT (PA6)
//   - Tacho via PC817 optocoupler → TACHO (PA2)
//   - Motor enable relay K5 (via ULN2003) must be active + door locked
//
// Phase-angle control:
//   After each zero-crossing, we delay a variable time, then fire the TRIAC.
//   Shorter delay = more power. At 50Hz, half-cycle = 10ms.
// ============================================================================

// Volatile globals for ISR
volatile unsigned long g_zeroCrossTime = 0;
volatile bool g_zeroCrossFlag = false;
volatile unsigned long g_tachoLastPulse = 0;
volatile unsigned long g_tachoPeriod = 0;
volatile uint16_t g_tachoCount = 0;

class MotorController {
public:
    void begin() {
        pinMode(PIN_MOTOR_EN, OUTPUT);
        pinMode(PIN_MOTOR_PWM, OUTPUT);
        digitalWrite(PIN_MOTOR_EN, LOW);
        digitalWrite(PIN_MOTOR_PWM, LOW);

        // Configure zero-cross interrupt (falling edge - optocoupler pulls low at zero cross)
        pinMode(PIN_ZERO_CROSS, INPUT);
        attachInterrupt(digitalPinToInterrupt(PIN_ZERO_CROSS), zeroCrossISR, FALLING);

        // Configure tacho interrupt
        pinMode(PIN_TACHO, INPUT);
        attachInterrupt(digitalPinToInterrupt(PIN_TACHO), tachoISR, FALLING);

        _enabled = false;
        _targetRPM = 0;
        _currentRPM = 0;
        _firingDelay = MAX_FIRING_DELAY; // Full delay = motor off
        _motorOn = false;
        _lastControlUpdate = 0;
    }

    void enable() {
        digitalWrite(PIN_MOTOR_EN, HIGH);
        _enabled = true;
        _lastControlUpdate = millis();
    }

    void disable() {
        digitalWrite(PIN_MOTOR_EN, LOW);
        digitalWrite(PIN_MOTOR_PWM, LOW);
        _enabled = false;
        _motorOn = false;
        _targetRPM = 0;
        _currentRPM = 0;
        _firingDelay = MAX_FIRING_DELAY;
        _lastControlUpdate = 0;
        noInterrupts();
        g_tachoLastPulse = 0;
        g_tachoPeriod = 0;
        interrupts();
    }

    void setTargetRPM(uint16_t rpm) {
        _targetRPM = rpm;
    }

    // Call this in the main loop - handles phase-angle firing
    void update() {
        if (!_enabled) return;

        unsigned long nowUs = micros();
        unsigned long nowMs = millis();

        // Calculate current RPM from tacho
        noInterrupts();
        unsigned long period = g_tachoPeriod;
        unsigned long lastPulse = g_tachoLastPulse;
        bool zeroCrossFlag = g_zeroCrossFlag;
        unsigned long zeroCrossTime = g_zeroCrossTime;
        if (zeroCrossFlag) g_zeroCrossFlag = false;
        interrupts();

        if (period > 0 && lastPulse > 0 && (nowUs - lastPulse) < 1000000UL) {
            // RPM = 60 / (period_in_seconds * pulses_per_rev)
            _currentRPM = (uint16_t)(60000000UL / (period * TACHO_PULSES_PER_REV));
        } else {
            _currentRPM = 0;
        }

        // Harte Sicherheitsgrenze: Sobald die Trommel schneller als erlaubt ist,
        // wird der TRIAC in derselben Update-Runde nicht mehr gezuendet.
        if (_currentRPM > MAX_DRUM_RPM) {
            _firingDelay = MAX_FIRING_DELAY;
            _motorOn = false;
            digitalWrite(PIN_MOTOR_PWM, LOW);
            return;
        }

        // Simple P-controller for firing delay adjustment
        if (_targetRPM == 0) {
            _firingDelay = MAX_FIRING_DELAY;
            _motorOn = false;
            digitalWrite(PIN_MOTOR_PWM, LOW);
            return;
        }

        _motorOn = true;

        // Adjust firing delay based on RPM error only in fixed intervals.
        // Without this, the control loop runs much faster than the mains cycles.
        if ((nowMs - _lastControlUpdate) >= CONTROL_UPDATE_INTERVAL_MS) {
            _lastControlUpdate = nowMs;
            int16_t error = (int16_t)_targetRPM - (int16_t)_currentRPM;

            if (error > 100) {
                if (_firingDelay > MIN_FIRING_DELAY + 50) {
                    _firingDelay -= 50;
                } else {
                    _firingDelay = MIN_FIRING_DELAY;
                }
            } else if (error > 20) {
                if (_firingDelay > MIN_FIRING_DELAY + 10) {
                    _firingDelay -= 10;
                } else {
                    _firingDelay = MIN_FIRING_DELAY;
                }
            } else if (error < -100) {
                if (_firingDelay < MAX_FIRING_DELAY - 50) {
                    _firingDelay += 50;
                } else {
                    _firingDelay = MAX_FIRING_DELAY;
                }
            } else if (error < -20) {
                if (_firingDelay < MAX_FIRING_DELAY - 10) {
                    _firingDelay += 10;
                } else {
                    _firingDelay = MAX_FIRING_DELAY;
                }
            }
        }

        // Phase-angle firing: after zero-cross, wait _firingDelay, then pulse TRIAC
        if (zeroCrossFlag) {
            if (_firingDelay < MAX_FIRING_DELAY) {
                // Wait for firing delay (blocking, but short ~0-9ms)
                unsigned long waitUntil = zeroCrossTime + _firingDelay;
                while (micros() < waitUntil) { /* spin */ }

                // Fire TRIAC - short pulse is enough, TRIAC latches until next zero-cross
                digitalWrite(PIN_MOTOR_PWM, HIGH);
                delayMicroseconds(50);  // 50µs gate pulse
                digitalWrite(PIN_MOTOR_PWM, LOW);
            }
        }
    }

    uint16_t getCurrentRPM() const { return _currentRPM; }
    uint16_t getTargetRPM() const { return _targetRPM; }
    bool isEnabled() const { return _enabled; }
    bool isMotorOn() const { return _motorOn; }

    bool isMotorStopped() const {
        return _currentRPM == 0 && (micros() - g_tachoLastPulse) > 2000000UL;
    }

private:
    static const unsigned long MIN_FIRING_DELAY = 500;    // µs - near full power
    static const unsigned long MAX_FIRING_DELAY = 9500;   // µs - near zero power (half-cycle = 10ms at 50Hz)
    static const unsigned long CONTROL_UPDATE_INTERVAL_MS = 10;

    bool _enabled;
    bool _motorOn;
    uint16_t _targetRPM;
    uint16_t _currentRPM;
    unsigned long _firingDelay;
    unsigned long _lastControlUpdate;

    static void zeroCrossISR() {
        g_zeroCrossTime = micros();
        g_zeroCrossFlag = true;
    }

    static void tachoISR() {
        unsigned long now = micros();
        g_tachoPeriod = now - g_tachoLastPulse;
        g_tachoLastPulse = now;
        g_tachoCount++;
    }
};

#endif // MOTOR_H
