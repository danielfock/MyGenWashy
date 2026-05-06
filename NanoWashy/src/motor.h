#ifndef MOTOR_H
#define MOTOR_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// Motor Control - TRIAC Phase-Angle Control (ATmega328P / Arduino Nano)
//
// Unterschiede zum ATTiny3226:
//   - Nur INT0 (D2) und INT1 (D3) für externe Interrupts
//   - Zero-Cross → INT0 (D2), Tacho → INT1 (D3)
//   - Motor PWM auf D9 (Timer1 OC1A) — wird hier aber als Digital-Pin
//     für den TRIAC-Zündimpuls verwendet (kein HW-PWM für Phasenanschnitt)
//   - 16MHz statt 20MHz → micros() Auflösung 4µs (ausreichend)
// ============================================================================

// Volatile globals for ISR (auch im Testmodus deklariert fuer Watchdog-Checks)
volatile unsigned long g_zeroCrossTime = 0;
volatile bool g_zeroCrossFlag = false;
volatile unsigned long g_tachoLastPulse = 0;
volatile unsigned long g_tachoPeriod = 0;
volatile uint16_t g_tachoCount = 0;

// ============================================================================
// ORIGINAL: ISRs fuer Zero-Cross und Tacho (nur ohne POTI_TEST_MODE)
// ============================================================================
#if !POTI_TEST_MODE

// ISR für Zero-Cross (INT0 / D2)
void zeroCrossISR() {
    g_zeroCrossTime = micros();
    g_zeroCrossFlag = true;
}

// ISR für Tacho (INT1 / D3)
void tachoISR() {
    unsigned long now = micros();
    g_tachoPeriod = now - g_tachoLastPulse;
    g_tachoLastPulse = now;
    g_tachoCount++;
}

#endif // !POTI_TEST_MODE

class MotorController {
public:

// ========================================================================
// POTI TEST MODE: Motor-Drehzahl wird von einem Poti an A2 gelesen
//   Linksanschlag (0)     = 0 RPM
//   Rechtsanschlag (1023) = POTI_RPM_MAX RPM
// Zero-Cross und Tacho werden kuenstlich simuliert, damit die Watchdogs
// in main.cpp nicht ausloesen. TRIAC wird nicht gezuendet.
// Motor-Enable-Pin (D10) wird weiterhin geschaltet (Relais-Klick als
// Feedback beim Testen).
// ========================================================================
#if POTI_TEST_MODE

    void begin() {
        pinMode(PIN_MOTOR_EN, OUTPUT);
        pinMode(PIN_MOTOR_PWM, OUTPUT);
        digitalWrite(PIN_MOTOR_EN, LOW);
        digitalWrite(PIN_MOTOR_PWM, LOW);

        // Poti-Pin als Eingang (kein Pullup)
        pinMode(PIN_POTI_RPM, INPUT);

        // Keine ISRs im Testmodus — Zero-Cross und Tacho kommen vom Poti

        _enabled = false;
        _targetRPM = 0;
        _currentRPM = 0;
        _firingDelay = MAX_FIRING_DELAY;
        _motorOn = false;
        _lastControlUpdate = 0;
    }

    void enable() {
        digitalWrite(PIN_MOTOR_EN, HIGH);
        _enabled = true;
        _lastControlUpdate = millis();
        // Sofort gueltigen Zero-Cross setzen, damit die Grace-Period
        // nicht mit einem veralteten Timestamp startet
        g_zeroCrossTime = micros();
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
        g_tachoLastPulse = 0;
        g_tachoPeriod = 0;
        g_zeroCrossTime = 0;
    }

    void setTargetRPM(uint16_t rpm) {
        _targetRPM = rpm;
    }

    void update() {
        if (!_enabled) return;

        unsigned long nowUs = micros();

        // --- Poti lesen: simulierte IST-Drehzahl ---
        uint32_t sum = 0;
        for (uint8_t i = 0; i < 4; i++) {
            sum += analogRead(PIN_POTI_RPM);
            delayMicroseconds(50);
        }
        uint16_t adcValue = sum / 4;
        _currentRPM = (uint16_t)((unsigned long)adcValue * POTI_RPM_MAX / 1023UL);

        // --- Watchdog-Signale kuenstlich erzeugen ---
        // Simuliert Zero-Cross alle ~10ms (50Hz) und Tacho-Pulse passend zur RPM
        g_zeroCrossTime = nowUs;
        g_zeroCrossFlag = true;
        if (_currentRPM > 0) {
            g_tachoLastPulse = nowUs;
            g_tachoPeriod = 60000000UL / ((unsigned long)_currentRPM * TACHO_PULSES_PER_REV);
        }

        // Harte Sicherheitsgrenze (auch im Testmodus aktiv)
        if (_currentRPM > MAX_DRUM_RPM) {
            _motorOn = false;
            return;
        }

        if (_targetRPM == 0) {
            _motorOn = false;
            return;
        }

        _motorOn = true;
        // Kein TRIAC-Zuendimpuls im Testmodus
    }

// ========================================================================
// ORIGINAL: Echte TRIAC-Phasenanschnittsteuerung mit ISR-Daten
// ========================================================================
#else

    void begin() {
        pinMode(PIN_MOTOR_EN, OUTPUT);
        pinMode(PIN_MOTOR_PWM, OUTPUT);
        digitalWrite(PIN_MOTOR_EN, LOW);
        digitalWrite(PIN_MOTOR_PWM, LOW);

        // Zero-Cross auf INT0 (D2), fallende Flanke
        pinMode(PIN_ZERO_CROSS, INPUT);
        attachInterrupt(digitalPinToInterrupt(PIN_ZERO_CROSS), zeroCrossISR, FALLING);

        // Tacho auf INT1 (D3), fallende Flanke
        pinMode(PIN_TACHO, INPUT);
        attachInterrupt(digitalPinToInterrupt(PIN_TACHO), tachoISR, FALLING);

        _enabled = false;
        _targetRPM = 0;
        _currentRPM = 0;
        _firingDelay = MAX_FIRING_DELAY;
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

    void update() {
        if (!_enabled) return;

        unsigned long nowUs = micros();
        unsigned long nowMs = millis();

        // RPM aus Tacho-Periode berechnen
        noInterrupts();
        unsigned long period = g_tachoPeriod;
        unsigned long lastPulse = g_tachoLastPulse;
        bool zeroCrossFlag = g_zeroCrossFlag;
        unsigned long zeroCrossTime = g_zeroCrossTime;
        if (zeroCrossFlag) g_zeroCrossFlag = false;
        interrupts();

        if (period > 0 && lastPulse > 0 && (nowUs - lastPulse) < 1000000UL) {
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

        // Motor aus
        if (_targetRPM == 0) {
            _firingDelay = MAX_FIRING_DELAY;
            _motorOn = false;
            digitalWrite(PIN_MOTOR_PWM, LOW);
            return;
        }

        _motorOn = true;

        // P-Regler: Stellgroesse nur in festen Intervallen anpassen.
        // Sonst laeuft die Loop viel zu schnell und der Motor ueberzieht.
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

        // Phasenanschnitt: nach Zero-Cross warten, dann TRIAC zünden
        if (zeroCrossFlag) {
            if (_firingDelay < MAX_FIRING_DELAY) {
                unsigned long waitUntil = zeroCrossTime + _firingDelay;
                while (micros() < waitUntil) { /* spin */ }

                // TRIAC-Zündimpuls (50µs reichen)
                digitalWrite(PIN_MOTOR_PWM, HIGH);
                delayMicroseconds(50);
                digitalWrite(PIN_MOTOR_PWM, LOW);
            }
        }
    }

#endif // POTI_TEST_MODE

    // --- Gemeinsame Getter (identisch in beiden Modi) ---
    uint16_t getCurrentRPM() const { return _currentRPM; }
    uint16_t getTargetRPM() const { return _targetRPM; }
    bool isEnabled() const { return _enabled; }
    bool isMotorOn() const { return _motorOn; }

    bool hasRecentZeroCross(unsigned long timeoutUs) const {
        noInterrupts();
        unsigned long lastZeroCross = g_zeroCrossTime;
        interrupts();
        return lastZeroCross > 0 && (micros() - lastZeroCross) <= timeoutUs;
    }

    bool hasRecentTachoPulse(unsigned long timeoutUs) const {
        noInterrupts();
        unsigned long lastPulse = g_tachoLastPulse;
        interrupts();
        return lastPulse > 0 && (micros() - lastPulse) <= timeoutUs;
    }

    bool isMotorStopped() const {
        return _currentRPM == 0 && (micros() - g_tachoLastPulse) > 2000000UL;
    }

private:
    static const unsigned long MIN_FIRING_DELAY = 500;     // µs
    static const unsigned long MAX_FIRING_DELAY = 9500;    // µs (50Hz Halbwelle = 10ms)
    static const unsigned long CONTROL_UPDATE_INTERVAL_MS = 10;

    bool _enabled;
    bool _motorOn;
    uint16_t _targetRPM;
    uint16_t _currentRPM;
    unsigned long _firingDelay;
    unsigned long _lastControlUpdate;
};

#endif // MOTOR_H
