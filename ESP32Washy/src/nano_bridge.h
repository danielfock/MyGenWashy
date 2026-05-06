#ifndef NANO_BRIDGE_H
#define NANO_BRIDGE_H

#include <Wire.h>
#include "config.h"

// ============================================================================
// I2C Master Bridge — Kommunikation mit dem Basis-Controller (Nano/ATTiny)
//
// Der ESP32 ist I2C Master und:
//   - Liest periodisch Status-Register (Temp, RPM, Flags, Errors)
//   - Schreibt Control-Register (Relais-Bits, Motor-RPM)
//   - Muss innerhalb des Nano-Watchdogs schreiben (< 2s)
//
// Thread-safe: Alle I2C-Operationen laufen im main loop (kein RTOS-Problem)
// ============================================================================

struct NanoStatus {
    uint8_t  state;           // Controller state (0=IDLE, 1=ACTIVE, 2=ERROR)
    uint8_t  error;           // Error code from Nano
    float    temperature;     // Water temperature in C
    uint16_t rpm;             // Current motor RPM
    uint16_t targetRpm;       // Target RPM (as acknowledged by Nano)
    uint8_t  flags;           // Status flags
    uint8_t  controlRequest;  // Current control bits echoed back
    uint8_t  waterLevel;      // Legacy/Reserviert: Nano liefert hier aktuell immer 0
    uint8_t  outputState;     // Actual relay output state
    bool     connected;       // I2C communication OK
    unsigned long lastReadMs; // Last successful read timestamp
};

class NanoBridge {
public:
    void begin() {
        Wire.begin(I2C_SDA, I2C_SCL, I2C_CLOCK_HZ);
        memset(&_status, 0, sizeof(_status));
        _controlBits = 0;
        _requestedRPM = 0;
        _lastWriteMs = 0;
        _lastReadMs = 0;
        _consecutiveErrors = 0;
    }

    // --- Lesen: Status vom Nano holen ---
    bool readStatus() {
        // Setze Register-Pointer auf 0x00
        Wire.beginTransmission(I2C_SLAVE_ADDR);
        Wire.write(0x00);
        uint8_t err = Wire.endTransmission(false);  // repeated start
        if (err != 0) {
            _handleCommError();
            return false;
        }

        // Lese 12 Bytes (REG_STATUS bis REG_OUTPUT_STATE)
        uint8_t count = Wire.requestFrom((uint8_t)I2C_SLAVE_ADDR, (uint8_t)REG_READ_COUNT);
        if (count < REG_READ_COUNT) {
            _handleCommError();
            return false;
        }

        uint8_t data[REG_READ_COUNT];
        for (uint8_t i = 0; i < REG_READ_COUNT; i++) {
            data[i] = Wire.read();
        }

        // Parse
        _status.state          = data[0];
        _status.error          = data[1];
        _status.temperature    = (float)data[2] + (float)data[3] / 10.0f;
        _status.rpm            = ((uint16_t)data[4] << 8) | data[5];
        _status.targetRpm      = ((uint16_t)data[6] << 8) | data[7];
        _status.flags          = data[8];
        _status.controlRequest = data[9];
        _status.waterLevel     = data[10];
        _status.outputState    = data[11];
        _status.connected      = true;
        _status.lastReadMs     = millis();

        _lastReadMs = millis();
        _consecutiveErrors = 0;
        return true;
    }

    // --- Schreiben: Control-Bits und RPM zum Nano senden ---
    bool writeControl() {
        uint8_t rpmHigh = (_requestedRPM >> 8) & 0xFF;
        uint8_t rpmLow  = _requestedRPM & 0xFF;

        Wire.beginTransmission(I2C_SLAVE_ADDR);
        Wire.write(REG_WRITE_CONTROL);  // Start-Register
        Wire.write(_controlBits);       // REG_WRITE_CONTROL (0x11)
        Wire.write(rpmHigh);            // REG_WRITE_RPM_HIGH (0x12)
        Wire.write(rpmLow);             // REG_WRITE_RPM_LOW  (0x13)
        uint8_t err = Wire.endTransmission();

        if (err != 0) {
            _handleCommError();
            return false;
        }

        _lastWriteMs = millis();
        _consecutiveErrors = 0;
        return true;
    }

    // --- Kommando senden (ALL_OFF, RESET_ERROR) ---
    bool sendCommand(uint8_t cmd) {
        Wire.beginTransmission(I2C_SLAVE_ADDR);
        Wire.write(REG_COMMAND);
        Wire.write(cmd);
        uint8_t err = Wire.endTransmission();

        if (err != 0) {
            _handleCommError();
            return false;
        }

        _consecutiveErrors = 0;
        return true;
    }

    // --- Setzer fuer Control-Outputs ---
    void setControlBits(uint8_t bits) { _controlBits = bits; }
    void setRequestedRPM(uint16_t rpm) { _requestedRPM = rpm; }

    // Einzelne Bits setzen/loeschen
    void setLock(bool on)        { _setBit(CTRL_LOCK, on); }
    void setDrain(bool on)       { _setBit(CTRL_DRAIN, on); }
    void setWaterIn1(bool on)    { _setBit(CTRL_WATER_IN1, on); }
    void setWaterIn2(bool on)    { _setBit(CTRL_WATER_IN2, on); }
    void setHeater(bool on)      { _setBit(CTRL_HEATER, on); }
    void setMotorEnable(bool on) { _setBit(CTRL_MOTOR_ENABLE, on); }

    void allOff() {
        _controlBits = 0;
        _requestedRPM = 0;
    }

    // --- Getter ---
    const NanoStatus& status() const { return _status; }
    uint8_t getControlBits() const { return _controlBits; }
    uint16_t getRequestedRPM() const { return _requestedRPM; }
    bool isConnected() const { return _status.connected; }
    uint8_t getConsecutiveErrors() const { return _consecutiveErrors; }
    unsigned long lastWriteAge() const { return millis() - _lastWriteMs; }

    // Convenience
    bool isDoorLocked() const { return (_status.outputState & 0x01) != 0; }
    bool hasNanoError() const { return _status.error != NANO_ERR_NONE; }

private:
    NanoStatus _status;
    uint8_t _controlBits;
    uint16_t _requestedRPM;
    unsigned long _lastWriteMs;
    unsigned long _lastReadMs;
    uint8_t _consecutiveErrors;

    void _setBit(uint8_t bit, bool on) {
        if (on) _controlBits |= bit;
        else    _controlBits &= ~bit;
    }

    void _handleCommError() {
        _consecutiveErrors++;
        if (_consecutiveErrors > 10) {
            _status.connected = false;
        }
    }
};

#endif // NANO_BRIDGE_H
