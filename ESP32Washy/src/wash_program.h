#ifndef WASH_PROGRAM_H
#define WASH_PROGRAM_H

#include <Arduino.h>
#include "config.h"
#include "nano_bridge.h"
#include "water_level.h"
#include "wash_programs.h"

enum WashState : uint8_t {
    WASH_IDLE = 0,
    WASH_LOCKING,
    WASH_PREWASH_FILL,
    WASH_PREWASH_WASH,
    WASH_PREWASH_DRAIN,
    WASH_FILLING,
    WASH_HEATING,
    WASH_WASHING,
    WASH_DRAIN1,
    WASH_RINSE_FILL,
    WASH_RINSE_DRAIN,
    WASH_SPINNING,
    WASH_DRAIN2,
    WASH_MOTOR_STOP,
    WASH_UNLOCKING,
    WASH_DONE,
    WASH_ERROR,
    WASH_PAUSED,
    WASH_STATE_COUNT
};

enum WashError : uint8_t {
    WERR_NONE = 0,
    WERR_NANO_COMM,
    WERR_NANO_ERROR,
    WERR_FILL_TIMEOUT,
    WERR_HEAT_TIMEOUT,
    WERR_DRAIN_TIMEOUT,
    WERR_OVERHEAT,
    WERR_SENSOR_FAIL,
    WERR_MOTOR_STOP_FAIL,
    WERR_USER_ABORT
};

enum WashMotionState : uint8_t {
    WMSTEP_RUN = 0,
    WMSTEP_PAUSE
};

class WashProgram {
public:
    void begin(NanoBridge* bridge, WaterLevel* water) {
        _bridge = bridge;
        _water = water;
        _state = WASH_IDLE;
        _error = WERR_NONE;
        _prevState = WASH_IDLE;
        _stateStartMs = 0;
        _programStartMs = 0;
        _pauseProgress = 0;
        _drainLowGoneMs = 0;
        _washMotionState = WMSTEP_RUN;
        _washMotionStateStartMs = 0;
        _washRunQualifiedMs = 0;
        memset(&_prog, 0, sizeof(_prog));
        _programName[0] = '\0';
    }

    bool startProgram(const WashProgramDef& prog) {
        if (_state != WASH_IDLE && _state != WASH_DONE && _state != WASH_ERROR) return false;
        WashProgramDef normalized = prog;
        normalizeWashMotion(normalized);
        memcpy(&_prog, &normalized, sizeof(WashProgramDef));
        strlcpy(_programName, prog.name, PROGRAM_NAME_MAX);
        _error = WERR_NONE;
        _drainLowGoneMs = 0;
        _programStartMs = millis();
        Serial.printf("[WASH] Starte: %s (%dC W=%d/%us-%us S=%d)\n",
            _prog.name, _prog.tempC, _prog.washRPM, _prog.washRunSec, _prog.washPauseSec, _prog.spinRPM);
        _enterState(WASH_LOCKING);
        return true;
    }

    bool start() { return startProgram(_prog); }

    void stop() {
        _error = WERR_USER_ABORT;
        _emergencyOff();
        _enterState(WASH_ERROR);
    }

    void pause() {
        if (_state > WASH_LOCKING && _state < WASH_DONE) {
            _prevState = _state;
            _pauseProgress = getProgress();
            _bridge->setHeater(false);
            _bridge->setWaterIn1(false);
            _bridge->setWaterIn2(false);
            _bridge->setDrain(false);
            _bridge->setMotorEnable(false);
            _bridge->setRequestedRPM(0);
            _enterState(WASH_PAUSED);
        }
    }

    void resume() {
        if (_state == WASH_PAUSED) _enterState(_prevState);
    }

    void resetError() {
        if (_state == WASH_ERROR) {
            _bridge->allOff();
            _bridge->sendCommand(CMD_RESET_ERROR);
            _error = WERR_NONE;
            _enterState(WASH_IDLE);
        }
    }

    void setTargetTemp(uint8_t tempC)  { _prog.tempC = constrain(tempC, 0, 95); }
    void setWashRPM(uint16_t rpm)      { _prog.washRPM = constrain(rpm, 0, MAX_WASH_RPM_REALISTIC); }
    void setSpinRPM(uint16_t rpm)      { _prog.spinRPM = constrain(rpm, 0, MAX_SPIN_RPM_ALLOWED); }

    void update() {
        if (_state == WASH_IDLE || _state == WASH_DONE) return;
        unsigned long now = millis();
        unsigned long elapsed = now - _stateStartMs;
        const NanoStatus& ns = _bridge->status();

        if (!_bridge->isConnected()) { _setError(WERR_NANO_COMM); return; }
        if (ns.error != NANO_ERR_NONE && _state != WASH_ERROR) { _setError(WERR_NANO_ERROR); return; }
        if (ns.temperature >= (float)MAX_TEMP_C && _state != WASH_ERROR) { _setError(WERR_OVERHEAT); return; }

        switch (_state) {
        case WASH_LOCKING:
            _bridge->setLock(true);
            if (elapsed >= LOCK_DELAY_MS) {
                if (_prog.washMinutes == 0 && _prog.tempC == 0) _enterState(WASH_SPINNING);
                else if (_prog.prewash) _enterState(WASH_PREWASH_FILL);
                else _enterState(WASH_FILLING);
            }
            break;

        case WASH_PREWASH_FILL:
            _bridge->setLock(true);
            _bridge->setWaterIn1(true);
            if (_water->isLow() || elapsed >= 90000UL) {
                _bridge->setWaterIn1(false);
                _enterState(WASH_PREWASH_WASH);
            }
            break;

        case WASH_PREWASH_WASH:
            _bridge->setLock(true);
            _updateWashMotion(ns);
            if (elapsed >= 180000UL) {
                _bridge->setMotorEnable(false);
                _bridge->setRequestedRPM(0);
                _enterState(WASH_PREWASH_DRAIN);
            }
            break;

        case WASH_PREWASH_DRAIN:
            _bridge->setLock(true);
            _bridge->setMotorEnable(false);
            _bridge->setRequestedRPM(0);
            _bridge->setDrain(true);
            if (_drainDone(elapsed)) _enterState(WASH_FILLING);
            break;

        case WASH_FILLING:
            _bridge->setLock(true);
            _bridge->setWaterIn1(true);
            if (_water->isHigh()) {
                _bridge->setWaterIn1(false);
                _enterState(_prog.tempC > 0 ? WASH_HEATING : WASH_WASHING);
            } else if (elapsed >= FILL_BASE_MS && _water->isLow()) {
                _bridge->setWaterIn1(false);
                _enterState(_prog.tempC > 0 ? WASH_HEATING : WASH_WASHING);
            } else if (elapsed >= FILL_TIMEOUT_MS) {
                _bridge->setWaterIn1(false);
                if (_water->isLow()) _enterState(_prog.tempC > 0 ? WASH_HEATING : WASH_WASHING);
                else _setError(WERR_FILL_TIMEOUT);
            }
            break;

        case WASH_HEATING:
            _bridge->setLock(true);
            _bridge->setWaterIn1(false);
            if (ns.temperature >= (float)_prog.tempC) {
                _bridge->setHeater(false);
                _enterState(WASH_WASHING);
            } else if (ns.temperature < (float)(_prog.tempC - TEMP_HYSTERESIS_C)) {
                _bridge->setHeater(true);
            }
            if (elapsed >= HEAT_TIMEOUT_MS) {
                _bridge->setHeater(false);
                if (ns.temperature >= (float)(_prog.tempC - 5)) _enterState(WASH_WASHING);
                else _setError(WERR_HEAT_TIMEOUT);
            }
            break;

        case WASH_WASHING: {
            _bridge->setLock(true);
            _updateWashMotion(ns);
            if (_prog.tempC > 0) {
                if (ns.temperature < (float)(_prog.tempC - TEMP_HYSTERESIS_C)) _bridge->setHeater(true);
                else if (ns.temperature >= (float)_prog.tempC) _bridge->setHeater(false);
            }
            if (elapsed >= (unsigned long)_prog.washMinutes * 60000UL) {
                _bridge->setHeater(false);
                _bridge->setRequestedRPM(0);
                _bridge->setMotorEnable(false);
                _enterState(WASH_DRAIN1);
            }
            break;
        }

        case WASH_DRAIN1:
            _bridge->setLock(true);
            _bridge->setHeater(false);
            _bridge->setMotorEnable(false);
            _bridge->setRequestedRPM(0);
            _bridge->setDrain(true);
            if (_drainDone(elapsed)) {
                _enterState(_prog.extraRinse ? WASH_RINSE_FILL : WASH_SPINNING);
            }
            break;

        case WASH_RINSE_FILL:
            _bridge->setLock(true);
            _bridge->setWaterIn1(true);
            if (_water->isHigh() || (elapsed >= FILL_BASE_MS && _water->isLow())) {
                _bridge->setWaterIn1(false);
                _enterState(WASH_RINSE_DRAIN);
            }
            if (elapsed >= FILL_TIMEOUT_MS) {
                _bridge->setWaterIn1(false);
                _enterState(WASH_RINSE_DRAIN);
            }
            break;

        case WASH_RINSE_DRAIN:
            _bridge->setLock(true);
            _bridge->setDrain(true);
            if (_drainDone(elapsed)) _enterState(WASH_SPINNING);
            break;

        case WASH_SPINNING: {
            _bridge->setLock(true);
            _bridge->setDrain(false);
            _bridge->setMotorEnable(true);
            _bridge->setRequestedRPM(_prog.spinRPM);
            if (elapsed >= (unsigned long)_prog.spinMinutes * 60000UL) {
                _bridge->setRequestedRPM(0);
                _bridge->setMotorEnable(false);
                _enterState(WASH_DRAIN2);
            }
            break;
        }

        case WASH_DRAIN2:
            _bridge->setLock(true);
            _bridge->setMotorEnable(false);
            _bridge->setRequestedRPM(0);
            _bridge->setDrain(true);
            if (_drainDone(elapsed)) _enterState(WASH_MOTOR_STOP);
            break;

        case WASH_MOTOR_STOP:
            _bridge->setLock(true);
            _bridge->setMotorEnable(false);
            _bridge->setRequestedRPM(0);
            _bridge->setDrain(false);
            if (ns.rpm == 0 || elapsed >= MOTOR_STOP_WAIT_MS) _enterState(WASH_UNLOCKING);
            break;

        case WASH_UNLOCKING:
            _bridge->allOff();
            if (elapsed >= 1000) _enterState(WASH_DONE);
            break;

        case WASH_DONE:
            _bridge->allOff();
            break;

        case WASH_ERROR:
            break;

        case WASH_PAUSED:
            _bridge->setLock(true);
            break;

        default:
            break;
        }
    }

    WashState getState() const { return _state; }
    WashError getError() const { return _error; }
    const WashProgramDef& getActiveProgramDef() const { return _prog; }
    const char* getProgramName() const { return _programName; }
    uint8_t getTargetTemp() const { return _prog.tempC; }
    uint16_t getWashRPM() const { return _prog.washRPM; }
    uint16_t getSpinRPM() const { return _prog.spinRPM; }
    uint8_t getWashMotionProfile() const { return _prog.washMotionProfile; }
    uint8_t getWashRunSec() const { return _prog.washRunSec; }
    uint8_t getWashPauseSec() const { return _prog.washPauseSec; }
    const char* getWashMotionProfileName() const { return washMotionProfileName(_prog.washMotionProfile); }

    uint8_t getProgress() const {
        switch (_state) {
            case WASH_IDLE:           return 0;
            case WASH_LOCKING:        return 1;
            case WASH_PREWASH_FILL:   return _pct(2, 5, 90000UL);
            case WASH_PREWASH_WASH:   return _pct(5, 8, 180000UL);
            case WASH_PREWASH_DRAIN:  return _pct(8, 10, 120000UL);
            case WASH_FILLING:        return _pct(10, 18, FILL_BASE_MS);
            case WASH_HEATING:        return _pct(18, 25, HEAT_TIMEOUT_MS / 2);
            case WASH_WASHING:        return _pct(25, 65, (unsigned long)_prog.washMinutes * 60000UL);
            case WASH_DRAIN1:         return _pct(65, 72, 120000UL);
            case WASH_RINSE_FILL:     return _pct(72, 76, FILL_BASE_MS);
            case WASH_RINSE_DRAIN:    return _pct(76, 80, 120000UL);
            case WASH_SPINNING:       return _pct(80, 93, (unsigned long)_prog.spinMinutes * 60000UL);
            case WASH_DRAIN2:         return _pct(93, 97, 60000UL);
            case WASH_MOTOR_STOP:     return 97;
            case WASH_UNLOCKING:      return 99;
            case WASH_DONE:           return 100;
            case WASH_ERROR:          return 0;
            case WASH_PAUSED:         return _pauseProgress;
            default:                  return 0;
        }
    }

    unsigned long getElapsedMs() const {
        if (_programStartMs == 0) return 0;
        return millis() - _programStartMs;
    }

    const char* getStateName() const {
        static const char* names[] = {
            "Aus", "Verriegeln",
            "Vorw.Fuellen", "Vorwaesche", "Vorw.Abpumpen",
            "Fuellen", "Heizen", "Waschen",
            "Abpumpen", "Spuelen", "Sp.Abpumpen",
            "Schleudern", "Abpumpen 2",
            "Motor-Stopp", "Entriegeln", "Fertig", "Fehler", "Pausiert"
        };
        return (_state < WASH_STATE_COUNT) ? names[_state] : "?";
    }

    const char* getErrorName() const {
        static const char* names[] = {
            "Kein Fehler", "Nano nicht erreichbar", "Nano Hardware-Fehler",
            "Fuell-Timeout", "Heiz-Timeout", "Abpump-Timeout",
            "Ueberhitzung", "Sensor defekt", "Motor stoppt nicht",
            "Abgebrochen"
        };
        return (_error <= WERR_USER_ABORT) ? names[_error] : "?";
    }

private:
    NanoBridge* _bridge;
    WaterLevel* _water;
    WashState _state;
    WashState _prevState;
    WashError _error;
    unsigned long _stateStartMs;
    unsigned long _programStartMs;
    uint8_t _pauseProgress;
    unsigned long _drainLowGoneMs;
    WashMotionState _washMotionState;
    unsigned long _washMotionStateStartMs;
    unsigned long _washRunQualifiedMs;
    WashProgramDef _prog;
    char _programName[PROGRAM_NAME_MAX];

    void _enterState(WashState newState) {
        Serial.printf("[WASH] %s -> ", getStateName());
        _state = newState;
        _stateStartMs = millis();
        _drainLowGoneMs = 0;
        if (newState == WASH_PREWASH_WASH || newState == WASH_WASHING) {
            _resetWashMotion();
        }
        Serial.println(getStateName());
    }

    void _setError(WashError err) {
        _error = err;
        Serial.printf("[WASH] FEHLER: %s\n", getErrorName());
        _emergencyOff();
        _enterState(WASH_ERROR);
    }

    void _emergencyOff() {
        _bridge->allOff();
        _bridge->sendCommand(CMD_ALL_OFF);
    }

    // Gemeinsame Drain-Logik: LOW weg + DRAIN_EXTRA_MS nachlaufen, oder DRAIN_TIMEOUT
    bool _drainDone(unsigned long elapsed) {
        if (!_water->isLow()) {
            if (_drainLowGoneMs == 0) {
                _drainLowGoneMs = millis();
            } else if (millis() - _drainLowGoneMs >= DRAIN_EXTRA_MS) {
                _bridge->setDrain(false);
                _drainLowGoneMs = 0;
                return true;
            }
        } else {
            _drainLowGoneMs = 0;
        }
        if (elapsed >= DRAIN_TIMEOUT_MS) {
            _bridge->setDrain(false);
            _drainLowGoneMs = 0;
            return true;
        }
        return false;
    }

    void _resetWashMotion() {
        _washMotionState = WMSTEP_RUN;
        _washMotionStateStartMs = millis();
        _washRunQualifiedMs = 0;
    }

    void _updateWashMotion(const NanoStatus& ns) {
        if (_prog.washMinutes == 0 || _prog.washRPM == 0) {
            _bridge->setMotorEnable(false);
            _bridge->setRequestedRPM(0);
            return;
        }

        if (_prog.washPauseSec == 0) {
            _bridge->setMotorEnable(true);
            _bridge->setRequestedRPM(_prog.washRPM);
            return;
        }

        unsigned long now = millis();
        uint16_t rpmThreshold = (_prog.washRPM > 10) ? (uint16_t)((_prog.washRPM * 85U) / 100U) : _prog.washRPM;

        if (_washMotionState == WMSTEP_RUN) {
            _bridge->setMotorEnable(true);
            _bridge->setRequestedRPM(_prog.washRPM);

            if (_washRunQualifiedMs == 0 && ns.rpm >= rpmThreshold) {
                _washRunQualifiedMs = now;
            }

            unsigned long maxRunWindowMs = (unsigned long)_prog.washRunSec * 1000UL + 15000UL;
            bool runTimeReached = _washRunQualifiedMs > 0 &&
                (now - _washRunQualifiedMs) >= (unsigned long)_prog.washRunSec * 1000UL;
            bool runTimeout = (now - _washMotionStateStartMs) >= maxRunWindowMs;
            if (runTimeReached || runTimeout) {
                _washMotionState = WMSTEP_PAUSE;
                _washMotionStateStartMs = now;
                _washRunQualifiedMs = 0;
            }
            return;
        }

        _bridge->setMotorEnable(false);
        _bridge->setRequestedRPM(0);
        if ((now - _washMotionStateStartMs) >= (unsigned long)_prog.washPauseSec * 1000UL) {
            _washMotionState = WMSTEP_RUN;
            _washMotionStateStartMs = now;
            _washRunQualifiedMs = 0;
        }
    }

    uint8_t _pct(uint8_t s, uint8_t e, unsigned long dur) const {
        if (dur == 0) return s;
        unsigned long el = millis() - _stateStartMs;
        uint8_t p = s + (uint8_t)((el * (e - s)) / dur);
        return (p > e) ? e : p;
    }
};

#endif // WASH_PROGRAM_H
