#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// MyGenWashy - ESP32 Configuration
// Programmablaufsteuerung + Smart Home Bridge
//
// Kommuniziert ueber I2C als Master mit dem Basis-Controller
// (Arduino Nano / ATTiny3226) der die Hardware direkt ansteuert.
// ============================================================================

// --- I2C Master (zum Basis-Controller) ---
#define I2C_SDA           21
#define I2C_SCL           22
#define I2C_SLAVE_ADDR    0x20
#define I2C_CLOCK_HZ      100000

// --- WiFi ---
// Aus wifi_credentials.h laden (nicht in Git einchecken!)
// Fallback-Werte:
#define WIFI_SSID_DEFAULT     "MyGenWashy-Setup"
#define WIFI_PASS_DEFAULT     ""
#define WIFI_HOSTNAME         "mygenwashy"

// --- MQTT (optional, fuer Home Assistant) ---
#define MQTT_ENABLED          true
#define MQTT_PORT             1883
#define MQTT_TOPIC_PREFIX     "mygenwashy"
#define MQTT_DISCOVERY_PREFIX "homeassistant"

// --- Webserver ---
#define WEBSERVER_PORT        80

// --- Water Level Sensor Mode ---
// Bestimmt wie der Wasserstand erfasst wird:
//
//   0 = FLOAT_SWITCH  Echte Schwimmerschalter an GPIO34/35 (Produktion)
//                     GPIO34/35 sind input-only, KEINE internen Pull-ups!
//                     Externe 10k Pull-ups nach 3.3V erforderlich.
//
//   1 = POTI_TEST     Poti an GPIO32 simuliert Wasserstand (Testbetrieb)
//                     3 Zonen: Leer / LOW / LOW+HIGH (12-bit ADC)
//
//   2 = TIMER_SIM     Kein Sensor noetig (reiner Software-Test)
//                     Wasserstand wird zeitgesteuert simuliert:
//                     Fuellen: LOW nach 60s, HIGH nach 90s
//                     Abpumpen: Leer nach 30s
//
#define WATER_LEVEL_MODE      2

// Mode 0: Schwimmerschalter
#if WATER_LEVEL_MODE == 0
  #define PIN_WATER_LOW           34    // GPIO34 - Float switch LOW (input-only)
  #define PIN_WATER_HIGH          35    // GPIO35 - Float switch HIGH (input-only)
  #define WATER_SENSOR_ACTIVE     LOW

// Mode 1: Poti-Test
#elif WATER_LEVEL_MODE == 1
  #define PIN_POTI_WATER          32    // GPIO32 - ADC1_CH4
  #define POTI_WATER_LOW_THRESH   1365  // 0..1364 = Leer
  #define POTI_WATER_HIGH_THRESH  2730  // 1365..2729 = LOW, 2730..4095 = LOW+HIGH

// Mode 2: Timer-Simulation
#elif WATER_LEVEL_MODE == 2
  #define SIM_FILL_LOW_MS     10000UL   // 10s bis Wasser LOW erreicht
  #define SIM_FILL_HIGH_MS    20000UL   // 20s bis Wasser HIGH erreicht
  #define SIM_DRAIN_EMPTY_MS  20000UL   // 20s bis Wasser unter LOW sinkt

#else
  #error "WATER_LEVEL_MODE muss 0, 1 oder 2 sein"
#endif

// --- Debug ---
#define SERIAL_DEBUG          1
#define SERIAL_BAUD           115200

// ============================================================================
// Waschprogramm Parameter
// ============================================================================

#define TARGET_TEMP_C         40    // Zieltemperatur in Grad C
#define MAX_TEMP_C            85    // Ueberhitzungsgrenze
#define TEMP_HYSTERESIS_C      2    // Hysterese Heizung

// Timing
#define FILL_TIMEOUT_MS       (5UL  * 60UL * 1000UL)   // 5 min max Fuellzeit
#define HEAT_TIMEOUT_MS       (15UL * 60UL * 1000UL)   // 15 min max Heizzeit
#define WASH_DURATION_MS      (20UL * 60UL * 1000UL)   // 20 min Waschen
#define DRAIN_TIMEOUT_MS      (5UL  * 60UL * 1000UL)   // 5 min max Abpumpen
#define SPIN_DURATION_MS      (5UL  * 60UL * 1000UL)   // 5 min Schleudern
#define DRAIN_EXTRA_MS        (10UL * 1000UL)           // 10 sec extra Abpumpen
#define LOCK_DELAY_MS         (2000UL)                  // 2s nach Lock warten
#define MOTOR_STOP_WAIT_MS    (5000UL)                  // 5s Motor-Auslauf
#define FILL_BASE_MS          (120UL * 1000UL)          // 2 min Basis-Fuellzeit

// Motor
#define WASH_RPM              800     // RPM fuer Waschgang (niedriger als Schleudern)
#define SPIN_RPM              1200    // RPM fuer Schleudern

// I2C Polling
#define I2C_POLL_INTERVAL_MS  200     // Status vom Nano lesen
#define I2C_WRITE_INTERVAL_MS 500     // Control-Register schreiben (< Watchdog 2s)

// ============================================================================
// I2C Register Map (muss identisch zum Basis-Controller sein!)
// ============================================================================

// Lese-Register (ESP32 liest vom Basis-Controller)
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
#define REG_WATER_LEVEL        0x0A   // Legacy/Reserviert: Nano liefert hier 0
#define REG_OUTPUT_STATE       0x0B

// Schreib-Register
#define REG_COMMAND            0x10
#define REG_WRITE_CONTROL      0x11
#define REG_WRITE_RPM_HIGH     0x12
#define REG_WRITE_RPM_LOW      0x13

#define REG_READ_COUNT         12     // 0x00..0x0B = 12 bytes to read
#define REG_COUNT              0x14

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

// Remote Control Bits (geschrieben in REG_WRITE_CONTROL)
#define CTRL_LOCK              0x01
#define CTRL_DRAIN             0x02
#define CTRL_WATER_IN1         0x04
#define CTRL_WATER_IN2         0x08
#define CTRL_HEATER            0x10
#define CTRL_MOTOR_ENABLE      0x20

// Wasserstand
#define WLEVEL_LOW             0x01
#define WLEVEL_HIGH            0x02

// Nano Error Codes (gelesen aus REG_ERROR)
#define NANO_ERR_NONE          0
#define NANO_ERR_OVERHEAT      1
#define NANO_ERR_SENSOR_FAIL   2
#define NANO_ERR_OVERSPEED     3
#define NANO_ERR_ESP_TIMEOUT   4
#define NANO_ERR_ZERO_CROSS    5
#define NANO_ERR_MOTOR_STALL   6
#define NANO_ERR_WATCHDOG      7

#endif // CONFIG_H
