#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include <Wire.h>
#include "config.h"

// ============================================================================
// I2C Slave Interface for ESP32 Communication
//
// Register-based low-level protocol:
//   - The second controller writes relay request bits and a motor RPM request
//   - The basis controller executes only the lower hardware layer
//   - Status, actual RPM, temperature and water level are read back
// ============================================================================

class I2CSlave {
public:
    void begin() {
        memset(_registers, 0, sizeof(_registers));
        _regPointer = 0;
        _newCommand = false;
        _espConnected = false;
        _lastEspContact = 0;

        Wire.begin(I2C_SLAVE_ADDR);
        Wire.onReceive(onReceiveStatic);
        Wire.onRequest(onRequestStatic);

        _instance = this;
    }

    void updateStatus(uint8_t state, uint8_t error, uint8_t tempInt, uint8_t tempFrac,
                      uint16_t rpm, uint16_t targetRpm, uint8_t flags,
                      uint8_t controlRequest, uint8_t waterLevel, uint8_t outputState) {
        noInterrupts();
        _registers[REG_STATUS]          = state;
        _registers[REG_ERROR]           = error;
        _registers[REG_TEMP_INT]        = tempInt;
        _registers[REG_TEMP_FRAC]       = tempFrac;
        _registers[REG_RPM_HIGH]        = (rpm >> 8) & 0xFF;
        _registers[REG_RPM_LOW]         = rpm & 0xFF;
        _registers[REG_TARGET_RPM_HIGH] = (targetRpm >> 8) & 0xFF;
        _registers[REG_TARGET_RPM_LOW]  = targetRpm & 0xFF;
        _registers[REG_FLAGS]           = flags;
        _registers[REG_CONTROL_REQUEST] = controlRequest;
        _registers[REG_WATER_LEVEL]     = waterLevel;
        _registers[REG_OUTPUT_STATE]    = outputState;
        interrupts();
    }

    bool hasNewCommand() const { return _newCommand; }

    uint8_t getCommand() {
        _newCommand = false;
        return _registers[REG_COMMAND];
    }

    uint8_t getControlRequest() const { return _registers[REG_WRITE_CONTROL]; }

    uint16_t getRequestedRPM() const {
        return ((uint16_t)_registers[REG_WRITE_RPM_HIGH] << 8) | _registers[REG_WRITE_RPM_LOW];
    }

    void clearCommand() {
        _registers[REG_COMMAND] = CMD_NOP;
    }

    void clearRemoteRequest() {
        noInterrupts();
        _registers[REG_WRITE_CONTROL] = 0;
        _registers[REG_WRITE_RPM_HIGH] = 0;
        _registers[REG_WRITE_RPM_LOW] = 0;
        interrupts();
    }

    // Check ESP32 connectivity (consider disconnected after 10s without contact)
    bool isEspConnected() {
        if (millis() - _lastEspContact > 10000UL) {
            _espConnected = false;
        }
        return _espConnected;
    }

private:
    uint8_t _registers[REG_COUNT];
    uint8_t _regPointer;
    volatile bool _newCommand;
    bool _espConnected;
    unsigned long _lastEspContact;

    static I2CSlave* _instance;

    // Called when ESP32 writes data to us
    static void onReceiveStatic(int numBytes) {
        if (_instance) _instance->onReceive(numBytes);
    }

    void onReceive(int numBytes) {
        if (numBytes < 1) return;

        _lastEspContact = millis();
        _espConnected = true;

        // First byte is always the register pointer
        _regPointer = Wire.read();
        numBytes--;

        // Remaining bytes are data to write (for command registers)
        while (numBytes > 0 && Wire.available()) {
            if (_regPointer >= REG_COMMAND && _regPointer < REG_COUNT) {
                _registers[_regPointer] = Wire.read();
                if (_regPointer == REG_COMMAND && _registers[REG_COMMAND] != CMD_NOP) {
                    _newCommand = true;
                }
                _regPointer++;
            } else {
                Wire.read(); // discard writes to read-only registers
                _regPointer++;
            }
            numBytes--;
        }
    }

    // Called when ESP32 requests data from us
    static void onRequestStatic() {
        if (_instance) _instance->onRequest();
    }

    void onRequest() {
        _lastEspContact = millis();
        _espConnected = true;

        // Send bytes starting from current register pointer
        // Typically ESP32 reads a block of status registers
        uint8_t count = 0;
        while (_regPointer < REG_COUNT && count < 16) {
            Wire.write(_registers[_regPointer]);
            _regPointer++;
            count++;
        }
    }
};

I2CSlave* I2CSlave::_instance = nullptr;

#endif // I2C_SLAVE_H
