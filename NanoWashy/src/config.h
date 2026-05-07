#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// MyGenWashy - Arduino Nano (ATmega328P) Pin Configuration
//
// Port der ATTiny3226-Version auf Arduino Nano.
// Pin-Zuordnung optimiert für ATmega328P:
//   - INT0 (D2) / INT1 (D3) für Interrupts (Zero-Cross, Tacho)
//   - D4-D8, D10 für Relais/Ausgänge
//   - D9 für TRIAC-Zündimpuls
//   - A0 für NTC (ADC)
//   - A4/A5 für I2C (fest)
//
// ============================================================================
//
//  Arduino Nano Pinout — MyGenWashy Belegung:
//  ┌─────────────────────────────────────────────┐
//  │ Pin │ Typ             │ Signal              │
//  ├─────┼─────────────────┼─────────────────────┤
//  │ D2  │ INT0 (Interrupt)│ ZERO_CROSS          │
//  │ D3  │ INT1 (Interrupt)│ TACHO               │
//  │ D4  │ Digital Output  │ LOCK                │
//  │ D5  │ Digital Output  │ DRAIN               │
//  │ D6  │ Digital Output  │ WATER_INLET_1       │
//  │ D7  │ Digital Output  │ WATER_INLET_2       │
//  │ D8  │ Digital Output  │ HEATER              │
//  │ D9  │ Digital Output  │ MOTOR_PWM (TRIAC)   │
//  │ D10 │ Digital Output  │ MOTOR_ENABLE        │
//  │ A0  │ ADC0            │ NTC                 │
//  │ A4  │ I2C SDA (fest)  │ SDA → ESP32         │
//  │ A5  │ I2C SCL (fest)  │ SCL → ESP32         │
//  └─────────────────────────────────────────────┘
//
//  Frei: D11, D12, D13 (für Erweiterungen)
//  Test: A1 = Poti Temp, A2 = Poti RPM (POTI_TEST_MODE)
//
// ============================================================================

// --- Relay Outputs ---
#define PIN_LOCK          4    // D4 - Door lock relay (K8)
#define PIN_DRAIN         5    // D5 - Drain pump relay (K1)
#define PIN_WATER_IN1     6    // D6 - Water inlet valve 1 (K3)
#define PIN_WATER_IN2     7    // D7 - Water inlet valve 2 (K4)
#define PIN_HEATER        8    // D8 - Heater relay (K2)

// --- Motor Control ---
#define PIN_MOTOR_EN     10    // D10 - Motor enable relay (K5)
#define PIN_MOTOR_PWM     9    // D9 - TRIAC phase control

// --- Sensor Inputs (Interrupt-fähig) ---
#define PIN_ZERO_CROSS    2    // D2 / INT0 - AC zero-cross
#define PIN_TACHO         3    // D3 / INT1 - Motor tacho

// --- Analog Input ---
#define PIN_NTC          A0    // A0 / ADC0 - NTC 5k thermistor

// --- I2C (fest beim ATmega328P) ---
// SDA = A4, SCL = A5 — nicht änderbar
#define I2C_SLAVE_ADDR  0x20

// --- Debug Serial ---
#define SERIAL_DEBUG     1
#define SERIAL_BAUD  115200

// ============================================================================
// POTI TEST MODE
// Zum Testen ohne echte Hardware: Temperatur und Drehzahl
// werden ueber zwei Potentiometer (Analog-Eingaenge) simuliert.
//   - A1: Temperatur-Poti  → 0..100 °C linear
//   - A2: RPM-Poti         → 0..1500 RPM linear
// Zero-Cross und Tacho-ISRs werden deaktiviert, TRIAC wird nicht gezuendet.
// Alle Watchdog-Signale werden kuenstlich erzeugt.
//
// Zum Aktivieren: POTI_TEST_MODE auf 1 setzen.
// Fuer echte Hardware: POTI_TEST_MODE auf 0 setzen (Originalverhalten).
// ============================================================================
#define POTI_TEST_MODE    1

#if POTI_TEST_MODE
  #define PIN_POTI_TEMP       A1    // Poti fuer simulierte Temperatur
  #define PIN_POTI_RPM        A2    // Poti fuer simulierte Motor-Drehzahl
  #define POTI_TEMP_MIN       0.0f  // Poti Linksanschlag = 0 °C
  #define POTI_TEMP_MAX       100.0f// Poti Rechtsanschlag = 100 °C
  #define POTI_RPM_MAX        1500  // Poti Rechtsanschlag = 1500 RPM
#endif

// ============================================================================
// Low-Level Controller Parameter
// ============================================================================

// Temperature
#define MAX_TEMP_C            85

// Motor
#define MIN_DRUM_RPM          30    // Kleinste zulaessige Soll-Drehzahl > 0
#define MAX_DRUM_RPM          1500  // Harte Sicherheitsgrenze fuer die Trommel
#define RPM_RAMP_STEP         50
#define RPM_RAMP_INTERVAL_MS  500
#define TACHO_PULSES_PER_REV  1

// Watchdogs / Fail-Safe
#define ESP32_CONTROL_WATCHDOG_MS     2000UL
#define MOTOR_ZERO_CROSS_GRACE_MS      500UL
#define MOTOR_ZERO_CROSS_TIMEOUT_US 250000UL
#define MOTOR_TACHO_STARTUP_MS        8000UL
#define MOTOR_TACHO_TIMEOUT_US      4000000UL

// NTC Thermistor
#define NTC_NOMINAL_R         5000
#define NTC_SERIES_R          5000
#define NTC_BETA              3950
#define NTC_NOMINAL_TEMP      25

// ============================================================================
// I2C Register Map (identisch zur ATTiny-Version)
// Vollständige Dokumentation: siehe I2C_PROTOKOLL.md
// ============================================================================

// Lese-Register
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
#define REG_WATER_LEVEL        0x0A  // Legacy/Reserviert: ESP32 liest Water-Level lokal
#define REG_OUTPUT_STATE       0x0B

// Schreib-Register
#define REG_COMMAND            0x10
#define REG_WRITE_CONTROL      0x11
#define REG_WRITE_RPM_HIGH     0x12
#define REG_WRITE_RPM_LOW      0x13

#define REG_COUNT           0x14

// Kommandos
#define CMD_NOP                0x00
#define CMD_ALL_OFF            0x01
#define CMD_RESET_ERROR        0x02

// Flags
#define FLAG_DOOR_LOCKED       0x01
#define FLAG_HEATER_ON         0x02
#define FLAG_PUMP_ON           0x04
#define FLAG_MOTOR_ON          0x08
#define FLAG_WATER_IN          0x10
#define FLAG_ESP_CONNECTED     0x20
#define FLAG_ERROR             0x40
#define FLAG_ACTIVE            0x80

// Remote Control Bits
#define CTRL_LOCK              0x01
#define CTRL_DRAIN             0x02
#define CTRL_WATER_IN1         0x04
#define CTRL_WATER_IN2         0x08
#define CTRL_HEATER            0x10
#define CTRL_MOTOR_ENABLE      0x20

// Output State Bits
#define OUT_LOCK               0x01
#define OUT_DRAIN              0x02
#define OUT_WATER_IN1          0x04
#define OUT_WATER_IN2          0x08
#define OUT_HEATER             0x10
#define OUT_MOTOR_ENABLE       0x20

// Wasserstand (Legacy-Bitdefinition fuer reserviertes Register 0x0A)
#define WLEVEL_LOW          0x01
#define WLEVEL_HIGH         0x02

#endif // CONFIG_H
