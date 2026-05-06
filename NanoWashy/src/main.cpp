#include <Arduino.h>
#include <avr/wdt.h>
#include "config.h"
#include "ntc.h"
#include "motor.h"
#include "i2c_slave.h"

enum ControllerState : uint8_t {
    STATE_IDLE = 0,
    STATE_ACTIVE,
    STATE_ERROR
};

enum ErrorCode : uint8_t {
    ERR_NONE = 0,
    ERR_OVERHEAT,
    ERR_SENSOR_FAIL,
    ERR_MOTOR_OVERSPEED,
    ERR_ESP32_TIMEOUT,
    ERR_ZERO_CROSS_LOST,
    ERR_MOTOR_STALL,
    ERR_WATCHDOG_RESET
};

#if SERIAL_DEBUG
  #define DBG_INIT()    Serial.begin(SERIAL_BAUD)
  #define DBG(msg)      Serial.print(msg)
  #define DBG_LN(msg)   Serial.println(msg)
  #define DBG_VAL(l,v)  do { Serial.print(l); Serial.println(v); } while(0)
#else
  #define DBG_INIT()
  #define DBG(msg)
  #define DBG_LN(msg)
  #define DBG_VAL(l,v)
#endif

#if SERIAL_DEBUG
const char* stateNames[] = { "IDLE", "ACTIVE", "ERROR" };
const char* errorNames[] = {
    "NONE",
    "OVERHEAT",
    "SENSOR_FAIL",
    "MOTOR_OVERSPEED",
    "ESP32_TIMEOUT",
    "ZERO_CROSS_LOST",
    "MOTOR_STALL",
    "WATCHDOG_RESET"
};
#endif

NTCSensor ntc;
MotorController motor;
I2CSlave i2c;

ControllerState currentState = STATE_IDLE;
ErrorCode currentError = ERR_NONE;

unsigned long lastTempRead = 0;
unsigned long lastStatusUpdate = 0;
unsigned long lastDebugPrint = 0;
unsigned long lastRampUpdate = 0;
unsigned long motorEnableSinceMs = 0;

uint8_t requestedControl = 0;
uint16_t requestedRPM = 0;
uint16_t motorCommandRPM = 0;
uint8_t bootResetFlags = 0;

void processI2CCommands();
void loadRemoteRequest();
void applyRemoteControl(unsigned long now);
void applyMotorRequest(unsigned long now);
void applyErrorOutputs();
void updateControllerState();
void updateI2CRegisters();
void enterError(ErrorCode error);
void allOff();
uint16_t sanitizeRequestedRPM(uint16_t rpm);
uint8_t sanitizeControlMask(uint8_t mask);
uint8_t getFlags();
uint8_t getWaterLevel();
uint8_t getOutputState();
bool validateStaticSafety();
bool validateDynamicSafety(unsigned long now);
bool hasActiveDemand();
void setHeater(bool on);
void setDrainPump(bool on);
void setWaterInlet1(bool on);
void setWaterInlet2(bool on);
void setDoorLock(bool on);

// ========================================================================
// Wasserstand: Sensoren haengen jetzt am ESP32 (nicht mehr am Nano).
// getWaterLevel() gibt immer 0 zurueck — der ESP32 liest direkt.
// Das I2C-Register REG_WATER_LEVEL bleibt fuer Kompatibilitaet erhalten.
// ========================================================================
uint8_t getWaterLevel() {
    return 0;  // ESP32 liest Wasserstand direkt ueber eigene GPIOs
}

uint8_t sanitizeControlMask(uint8_t mask) {
    return mask & (CTRL_LOCK | CTRL_DRAIN | CTRL_WATER_IN1 | CTRL_WATER_IN2 |
                   CTRL_HEATER | CTRL_MOTOR_ENABLE);
}

uint16_t sanitizeRequestedRPM(uint16_t rpm) {
    if (rpm == 0) {
        return 0;
    }
    if (rpm < MIN_DRUM_RPM) {
        return 0;
    }
    if (rpm > MAX_DRUM_RPM) {
        return MAX_DRUM_RPM;
    }
    return rpm;
}

bool hasActiveDemand() {
    return requestedControl != 0 || motor.getCurrentRPM() > 0 || getOutputState() != 0;
}

void setup() {
    bootResetFlags = MCUSR;
    MCUSR = 0;
    wdt_disable();

    DBG_INIT();
    DBG_LN(F(""));
    DBG_LN(F("========================================"));
    DBG_LN(F("  MyGenWashy - Arduino Nano"));
    DBG_LN(F("  Basiscontroller / Low-Level Firmware"));
    DBG_LN(F("========================================"));

    pinMode(PIN_LOCK, OUTPUT);
    pinMode(PIN_DRAIN, OUTPUT);
    pinMode(PIN_WATER_IN1, OUTPUT);
    pinMode(PIN_WATER_IN2, OUTPUT);
    pinMode(PIN_HEATER, OUTPUT);

#if POTI_TEST_MODE
    DBG_LN(F("  ** POTI TEST MODE AKTIV **"));
    DBG_LN(F("  A1=Temp, A2=RPM"));
#endif
    // Wasserstand-Sensoren haengen am ESP32, nicht am Nano

    ntc.begin();
    motor.begin();
    allOff();
    i2c.begin();
    wdt_enable(WDTO_2S);

    delay(500);
    ntc.readTemperature();

    DBG_VAL(F("NTC Temp: "), ntc.getTemperature());
    DBG_LN(F("Water: Sensoren am ESP32 (nicht am Nano)"));

    if ((bootResetFlags & _BV(WDRF)) != 0) {
        DBG_LN(F("WARNUNG: Neustart nach Watchdog-Reset"));
        enterError(ERR_WATCHDOG_RESET);
    }
}

void loop() {
    unsigned long now = millis();
    wdt_reset();

    if (now - lastTempRead >= 500) {
        lastTempRead = now;
        ntc.readTemperature();
    }

    processI2CCommands();
    loadRemoteRequest();

    if (currentError == ERR_NONE) {
        if (validateStaticSafety()) {
            applyRemoteControl(now);
        }
    } else {
        applyErrorOutputs();
    }

    motor.update();

    if (currentError == ERR_NONE) {
        validateDynamicSafety(now);
    }

    updateControllerState();

    if (now - lastStatusUpdate >= 200) {
        lastStatusUpdate = now;
        updateI2CRegisters();
    }

    #if SERIAL_DEBUG
    if (now - lastDebugPrint >= 2000) {
        lastDebugPrint = now;
        Serial.print(F("["));
        Serial.print(stateNames[currentState]);
        Serial.print(F("] T="));
        Serial.print(ntc.getTemperature(), 1);
        Serial.print(F("C RPM="));
        Serial.print(motor.getCurrentRPM());
        Serial.print(F(" Target="));
        Serial.print(requestedRPM);
        Serial.print(F(" Ctrl=0x"));
        Serial.print(requestedControl, HEX);
        Serial.print(F(" Out=0x"));
        Serial.print(getOutputState(), HEX);
        Serial.print(F(" WL="));
        Serial.print(F("--"));  // Wasserstand am ESP32
        Serial.print(F(" ESP="));
        Serial.print(i2c.isEspConnected() ? "Ja" : "Nein");
        if (currentError != ERR_NONE) {
            Serial.print(F(" ERR="));
            Serial.print(errorNames[currentError]);
        }
        Serial.println();
    }
    #endif

    wdt_reset();
}

void processI2CCommands() {
    if (!i2c.hasNewCommand()) {
        return;
    }

    uint8_t cmd = i2c.getCommand();

    switch (cmd) {
        case CMD_ALL_OFF:
            i2c.clearRemoteRequest();
            requestedControl = 0;
            requestedRPM = 0;
            motorCommandRPM = 0;
            if (currentError == ERR_NONE) {
                allOff();
            } else {
                applyErrorOutputs();
            }
            DBG_LN(F("I2C: ALL_OFF"));
            break;

        case CMD_RESET_ERROR:
            i2c.clearRemoteRequest();
            requestedControl = 0;
            requestedRPM = 0;
            motorCommandRPM = 0;
            currentError = ERR_NONE;
            allOff();
            DBG_LN(F("I2C: RESET_ERROR"));
            break;

        default:
            DBG_VAL(F("I2C: unbekanntes Kommando 0x"), cmd);
            break;
    }

    i2c.clearCommand();
}

void loadRemoteRequest() {
    requestedControl = sanitizeControlMask(i2c.getControlRequest());
    requestedRPM = sanitizeRequestedRPM(i2c.getRequestedRPM());
}

bool validateStaticSafety() {
    if (!hasActiveDemand()) {
        return true;
    }

    if (!ntc.isSensorOk()) {
        enterError(ERR_SENSOR_FAIL);
        return false;
    }

    if (ntc.isOverheat()) {
        enterError(ERR_OVERHEAT);
        return false;
    }

    return true;
}

bool validateDynamicSafety(unsigned long now) {
    if (motor.getCurrentRPM() > MAX_DRUM_RPM) {
        enterError(ERR_MOTOR_OVERSPEED);
        return false;
    }

    if (!hasActiveDemand()) {
        return true;
    }

    if (!i2c.hasRecentControlWrite(ESP32_CONTROL_WATCHDOG_MS)) {
        enterError(ERR_ESP32_TIMEOUT);
        return false;
    }

    if (motor.isEnabled()) {
        unsigned long enabledForMs = (motorEnableSinceMs > 0) ? (now - motorEnableSinceMs) : 0;

        if (enabledForMs >= MOTOR_ZERO_CROSS_GRACE_MS &&
            !motor.hasRecentZeroCross(MOTOR_ZERO_CROSS_TIMEOUT_US)) {
            enterError(ERR_ZERO_CROSS_LOST);
            return false;
        }

        if (requestedRPM > 0 &&
            enabledForMs >= MOTOR_TACHO_STARTUP_MS &&
            !motor.hasRecentTachoPulse(MOTOR_TACHO_TIMEOUT_US)) {
            enterError(ERR_MOTOR_STALL);
            return false;
        }
    }

    return true;
}

void applyRemoteControl(unsigned long now) {
    setDoorLock((requestedControl & CTRL_LOCK) != 0);
    setDrainPump((requestedControl & CTRL_DRAIN) != 0);
    setWaterInlet1((requestedControl & CTRL_WATER_IN1) != 0);
    setWaterInlet2((requestedControl & CTRL_WATER_IN2) != 0);
    setHeater((requestedControl & CTRL_HEATER) != 0);

    applyMotorRequest(now);
}

void applyMotorRequest(unsigned long now) {
    bool motorRequested = ((requestedControl & CTRL_MOTOR_ENABLE) != 0) && requestedRPM > 0;

    if (!motorRequested) {
        motorCommandRPM = 0;
        motor.setTargetRPM(0);
        if (motor.isEnabled() && motor.isMotorStopped()) {
            motor.disable();
        }
        if (!motor.isEnabled()) {
            motorEnableSinceMs = 0;
        }
        return;
    }

    if (!motor.isEnabled()) {
        motor.enable();
        lastRampUpdate = now;
        motorEnableSinceMs = now;
    } else if (motorEnableSinceMs == 0) {
        motorEnableSinceMs = now;
    }

    if (motorCommandRPM == 0) {
        motorCommandRPM = (requestedRPM < RPM_RAMP_STEP) ? requestedRPM : RPM_RAMP_STEP;
    } else if (now - lastRampUpdate >= RPM_RAMP_INTERVAL_MS) {
        lastRampUpdate = now;
        if (motorCommandRPM < requestedRPM) {
            motorCommandRPM += RPM_RAMP_STEP;
            if (motorCommandRPM > requestedRPM) {
                motorCommandRPM = requestedRPM;
            }
        } else if (motorCommandRPM > requestedRPM) {
            if (motorCommandRPM > RPM_RAMP_STEP) {
                motorCommandRPM -= RPM_RAMP_STEP;
            } else {
                motorCommandRPM = 0;
            }
            if (motorCommandRPM < requestedRPM) {
                motorCommandRPM = requestedRPM;
            }
        }
    }

    motor.setTargetRPM(motorCommandRPM);
}

void enterError(ErrorCode error) {
    if (currentError != ERR_NONE) {
        return;
    }

    currentError = error;
    applyErrorOutputs();
    DBG(F("FEHLER: "));
    DBG_LN(errorNames[currentError]);
}

void applyErrorOutputs() {
    setHeater(false);
    setWaterInlet1(false);
    setWaterInlet2(false);
    setDoorLock(true);
    setDrainPump(true);
    motorCommandRPM = 0;
    motor.setTargetRPM(0);
    motor.disable();
    motorEnableSinceMs = 0;
}

void updateControllerState() {
    if (currentError != ERR_NONE) {
        currentState = STATE_ERROR;
        return;
    }

    if (hasActiveDemand()) {
        currentState = STATE_ACTIVE;
        return;
    }

    currentState = STATE_IDLE;
}

void updateI2CRegisters() {
    i2c.updateStatus(
        (uint8_t)currentState,
        (uint8_t)currentError,
        ntc.getTempInt(),
        ntc.getTempFrac(),
        motor.getCurrentRPM(),
        requestedRPM,
        getFlags(),
        requestedControl,
        getWaterLevel(),
        getOutputState()
    );
}

void allOff() {
    setHeater(false);
    setDrainPump(false);
    setWaterInlet1(false);
    setWaterInlet2(false);
    motor.setTargetRPM(0);
    motor.disable();
    motorEnableSinceMs = 0;
    setDoorLock(false);
}

void setHeater(bool on) {
    digitalWrite(PIN_HEATER, on ? HIGH : LOW);
}

void setDrainPump(bool on) {
    digitalWrite(PIN_DRAIN, on ? HIGH : LOW);
}

void setWaterInlet1(bool on) {
    digitalWrite(PIN_WATER_IN1, on ? HIGH : LOW);
}

void setWaterInlet2(bool on) {
    digitalWrite(PIN_WATER_IN2, on ? HIGH : LOW);
}

void setDoorLock(bool on) {
    digitalWrite(PIN_LOCK, on ? HIGH : LOW);
}

uint8_t getOutputState() {
    uint8_t outputs = 0;
    if (digitalRead(PIN_LOCK) == HIGH) {
        outputs |= OUT_LOCK;
    }
    if (digitalRead(PIN_DRAIN) == HIGH) {
        outputs |= OUT_DRAIN;
    }
    if (digitalRead(PIN_WATER_IN1) == HIGH) {
        outputs |= OUT_WATER_IN1;
    }
    if (digitalRead(PIN_WATER_IN2) == HIGH) {
        outputs |= OUT_WATER_IN2;
    }
    if (digitalRead(PIN_HEATER) == HIGH) {
        outputs |= OUT_HEATER;
    }
    if (motor.isEnabled()) {
        outputs |= OUT_MOTOR_ENABLE;
    }
    return outputs;
}

uint8_t getFlags() {
    uint8_t flags = 0;

    if (digitalRead(PIN_LOCK) == HIGH) {
        flags |= FLAG_DOOR_LOCKED;
    }
    if (digitalRead(PIN_HEATER) == HIGH) {
        flags |= FLAG_HEATER_ON;
    }
    if (digitalRead(PIN_DRAIN) == HIGH) {
        flags |= FLAG_PUMP_ON;
    }
    if (motor.isMotorOn()) {
        flags |= FLAG_MOTOR_ON;
    }
    if (digitalRead(PIN_WATER_IN1) == HIGH || digitalRead(PIN_WATER_IN2) == HIGH) {
        flags |= FLAG_WATER_IN;
    }
    if (i2c.isEspConnected()) {
        flags |= FLAG_ESP_CONNECTED;
    }
    if (currentError != ERR_NONE) {
        flags |= FLAG_ERROR;
    }
    if (currentState == STATE_ACTIVE) {
        flags |= FLAG_ACTIVE;
    }

    return flags;
}
