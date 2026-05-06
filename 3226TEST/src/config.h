#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// MyGenWashy - ATTiny3226 Pin Configuration
// Based on mr_washi schematic (KiCad) - mcu.kicad_sch
// ============================================================================

// --- Relay Outputs (active HIGH → ULN2003 sinks → relay coil energized) ---
#define PIN_LOCK        PIN_PC0   // 10 - Door lock relay (K8) - MUST be first action
#define PIN_DRAIN       PIN_PC1   // 11 - Drain pump relay (K1)
#define PIN_WATER_IN1   PIN_PC2   // 12 - Water inlet valve 1 (K3)
#define PIN_WATER_IN2   PIN_PC3   // 13 - Water inlet valve 2 (K4)
#define PIN_HEATER      PIN_PA4   //  2 - Heater relay (K2)

// --- Motor Control ---
#define PIN_MOTOR_EN    PIN_PA1   // 17 - Motor enable (relay K5, only with door locked)
#define PIN_MOTOR_PWM   PIN_PA3   // 19 - Motor PWM (TRIAC phase control via MOC3020M)

// --- Sensor Inputs ---
#define PIN_TACHO       PIN_PA2   // 18 - Motor tacho pulse (PC817 optocoupler)
#define PIN_ZERO_CROSS  PIN_PA6   //  4 - AC zero-cross detection (PC817 optocoupler)
#define PIN_NTC         PIN_PB2   //  7 - NTC 5k thermistor (voltage divider, ADC)

// --- Water Level Sensors ---
// Float switches: LOW_LEVEL (J6) und HIGH_LEVEL (J7) im Schematic.
// Die 230V-Schwimmerschalter schalten Relais K6/K7. Deren Kontakte liefern
// ein LOW-Voltage Signal an den MCU (über Optokoppler oder Relais-Hilfskontakt).
//   LOW  = Wasser steht mindestens auf LOW-Level
//   HIGH = Wasser hat Maximum erreicht (Zulauf stoppen)
// Aktiv-Pegel: LOW = Sensor ausgelöst (Wasser erreicht), mit INPUT_PULLUP
#define PIN_WATER_LOW   PIN_PA5   //  3 - Water level LOW sensor (active LOW)
#define PIN_WATER_HIGH  PIN_PA7   //  5 - Water level HIGH sensor (active LOW)

// Logikpegel der Schwimmerschalter:
//   Schalter geschlossen (Wasser da) → Pin wird LOW gezogen
//   Schalter offen (unter LOW-Level) → Pin HIGH durch internen Pullup
#define WATER_SENSOR_ACTIVE LOW

// --- I2C (TWI0) - for ESP32 communication ---
// SDA = PB1 (Pin 8), SCL = PB0 (Pin 9) — fest beim ATTiny3226
#define I2C_SLAVE_ADDR  0x20

// ============================================================================
// Low-Level Controller Parameter
// ============================================================================

// Temperature
#define MAX_TEMP_C            85

// Motor
#define MIN_DRUM_RPM          30
#define MAX_DRUM_RPM          1500
#define RPM_RAMP_STEP         50
#define RPM_RAMP_INTERVAL_MS  500
#define TACHO_PULSES_PER_REV  1

// NTC Thermistor (5k NTC + 5k Serien-R → Spannungsteiler an 3.3V)
#define NTC_NOMINAL_R         5000
#define NTC_SERIES_R          5000
#define NTC_BETA              3950
#define NTC_NOMINAL_TEMP      25

// ============================================================================
// I2C Register Map (ATTiny/Nano als Slave, ESP32 als Master)
//
// Protokoll: Register-basiert.
//   Schreiben: Master sendet [reg_addr] [byte0] [byte1] ...
//   Lesen:     Master sendet [reg_addr], dann liest N Bytes
//
// Vollständige Dokumentation: siehe I2C_PROTOKOLL.md
// ============================================================================

// --- Lese-Register (ESP32 liest vom Controller) ---
#define REG_STATUS             0x00
#define REG_ERROR              0x01
#define REG_TEMP_INT           0x02
#define REG_TEMP_FRAC          0x03
#define REG_RPM_HIGH           0x04
#define REG_RPM_LOW            0x05
#define REG_TARGET_RPM_HIGH    0x06
#define REG_TARGET_RPM_LOW     0x07
#define REG_FLAGS              0x08
#define REG_CONTROL_REQUEST    0x09
#define REG_WATER_LEVEL        0x0A
#define REG_OUTPUT_STATE       0x0B

// --- Schreib-Register (ESP32 schreibt zum Controller) ---
#define REG_COMMAND            0x10
#define REG_WRITE_CONTROL      0x11
#define REG_WRITE_RPM_HIGH     0x12
#define REG_WRITE_RPM_LOW      0x13

#define REG_COUNT           0x14  // Anzahl Register

// --- Kommandos (geschrieben in REG_COMMAND) ---
#define CMD_NOP                0x00
#define CMD_ALL_OFF            0x01
#define CMD_RESET_ERROR        0x02

// --- Flags-Register Bits (REG_FLAGS, 0x07) ---
#define FLAG_DOOR_LOCKED       0x01
#define FLAG_HEATER_ON         0x02
#define FLAG_PUMP_ON           0x04
#define FLAG_MOTOR_ON          0x08
#define FLAG_WATER_IN          0x10
#define FLAG_ESP_CONNECTED     0x20
#define FLAG_ERROR             0x40
#define FLAG_ACTIVE            0x80

// --- Remote Control Bits ---
#define CTRL_LOCK              0x01
#define CTRL_DRAIN             0x02
#define CTRL_WATER_IN1         0x04
#define CTRL_WATER_IN2         0x08
#define CTRL_HEATER            0x10
#define CTRL_MOTOR_ENABLE      0x20

// --- Output State Bits ---
#define OUT_LOCK               0x01
#define OUT_DRAIN              0x02
#define OUT_WATER_IN1          0x04
#define OUT_WATER_IN2          0x08
#define OUT_HEATER             0x10
#define OUT_MOTOR_ENABLE       0x20

// --- Wasserstand-Register Bits (REG_WATER_LEVEL, 0x0A) ---
#define WLEVEL_LOW          0x01  // Bit 0: LOW-Sensor aktiv (Wasser >= LOW-Level)
#define WLEVEL_HIGH         0x02  // Bit 1: HIGH-Sensor aktiv (Wasser >= Maximum)

#endif // CONFIG_H
