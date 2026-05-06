# MyGenWashy — Setup-Anleitung

Schritt-für-Schritt Anleitung zum Kompilieren und Flashen der Waschmaschinen-Firmware in VS Code. Es gibt drei relevante Projekte: ATTiny3226 (Originalhardware), Arduino Nano (ATmega328P) als Basiscontroller und ESP32 als Zweitcontroller. Alle verwenden dasselbe I2C-Protokoll zwischen hoher und niedriger Ebene.

---

## 1. Voraussetzungen installieren

### 1.1 VS Code

Falls noch nicht vorhanden, VS Code herunterladen und installieren:
https://code.visualstudio.com/

### 1.2 PlatformIO Extension

1. VS Code öffnen
2. Extensions-Tab öffnen (`Ctrl+Shift+X`)
3. Nach **"PlatformIO IDE"** suchen
4. Auf **Install** klicken
5. VS Code neu starten, wenn aufgefordert

PlatformIO installiert automatisch alle benötigten Toolchains beim ersten Build.

### 1.3 Python & pymcuprog (für UPDI-Upload)

pymcuprog ist das Tool, das den kompilierten Code über UPDI auf den ATTiny3226 flasht.

```bash
pip install pymcuprog
```

Unter Linux ggf. mit `pip3` oder `--user` Flag:
```bash
pip3 install pymcuprog --user
```

### 1.4 USB-Serial Treiber

Je nach verwendetem USB-Serial Adapter den passenden Treiber installieren:

- **CH340/CH341**: https://www.wch-ic.com/downloads/CH341SER_ZIP.html
- **FTDI FT232**: https://ftdichip.com/drivers/vcp-drivers/
- **CP2102**: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers

Nach der Installation prüfen, ob der Adapter als COM-Port erkannt wird (Windows: Geräte-Manager → Anschlüsse).

---

## 2. Projekt in VS Code öffnen

Es gibt drei Firmware-Projekte — je nach verwendetem Mikrocontroller den richtigen Ordner öffnen:

| Variante | Ordner | Controller | Besonderheit |
|----------|--------|------------|--------------|
| **ATTiny3226** | `MyGenWashy/3226TEST` | ATTiny3226-S | Original-Hardware, UPDI-Programmer nötig |
| **Arduino Nano** | `MyGenWashy/NanoWashy` | ATmega328P | Einfacher, Upload direkt über USB |
| **ESP32 Zweitcontroller** | `MyGenWashy/ESP32Washy` | ESP32 Dev Module | Arduino-Firmware: Web UI, Waschprogramme, MQTT, I2C-Master, Water-Level |

1. VS Code öffnen
2. **File → Open Folder...** → zum jeweiligen Ordner navigieren und öffnen
3. PlatformIO erkennt automatisch die `platformio.ini` und zeigt das Projekt in der Sidebar

Beim ersten Öffnen lädt PlatformIO die benötigte Plattform automatisch herunter (`atmelmegaavr` für ATTiny, `atmelavr` für Nano, `espressif32` für ESP32). Das kann einige Minuten dauern.

---

## 3. Firmware kompilieren (Build)

### Option A: Über die PlatformIO-Toolbar

In der **blauen Statusleiste** unten in VS Code befinden sich die PlatformIO-Buttons:

- **✓ (Häkchen)** = Build / Kompilieren
- **→ (Pfeil)** = Upload
- **🔌 (Stecker)** = Serial Monitor

Auf das **Häkchen** klicken, um den Build zu starten.

### Option B: Über das Terminal

Terminal öffnen (`Ctrl+Ö` oder `Ctrl+Shift+Backtick`) und eingeben:

```bash
pio run -e Build_Only
```

### Option C: Über die Command Palette

1. `Ctrl+Shift+P` drücken
2. "PlatformIO: Build" eingeben und auswählen

### Erwartete Ausgabe

Bei erfolgreichem Build erscheint am Ende:

```
Linking .pio/build/Build_Only/firmware.elf
Building .pio/build/Build_Only/firmware.hex
Checking size .pio/build/Build_Only/firmware.elf
========================= [SUCCESS] =========================
```

Falls Fehler auftreten, siehe Abschnitt "Fehlerbehebung" weiter unten.

---

## 4a. Arduino Nano flashen (einfach über USB)

Der Nano wird direkt über sein USB-Kabel programmiert — kein externer Programmer nötig.

### Upload starten

```bash
pio run -t upload
```

Oder in VS Code auf den **Upload-Pfeil (→)** in der Statusleiste klicken.

Falls der Upload fehlschlägt ("not in sync"), liegt es meist am Bootloader. In `NanoWashy/platformio.ini` die Board-Variante ändern:
- Neuere Clones (CH340): `board = nanoatmega328` (Standard)
- Ältere Originale: `board = nanoatmega328new`

### Serial Monitor (Debug-Ausgabe)

Der Nano gibt über USB Debug-Informationen aus (115200 Baud). In VS Code den Serial Monitor öffnen (Stecker-Symbol in der Statusleiste) oder:

```bash
pio device monitor
```

Die Ausgabe zeigt laufend Status, Temperatur, RPM und Fehlercodes:
```
[WASHING] T=38.5°C RPM=55 Prog=45% ESP=Nein
```

`RPM=` ist dabei immer die aktuelle Ist-Drehzahl, nicht der vorgegebene Sollwert.

### Nano Pin-Mapping (Verdrahtung zum Board)

Der Nano wird anstelle des ATTiny3226 mit dem Intermediary-Board (ULN2003 + Relais) verbunden:

```
Arduino Nano              Intermediary Board (ULN2003)
┌──────────────┐          ┌──────────────────────┐
│ D4  (LOCK)   ├──────────┤ I4 (MCU_LOCK)        │
│ D5  (DRAIN)  ├──────────┤ I1 (MCU_DRAIN_PUMP)  │
│ D6  (INLET1) ├──────────┤ I3 (MCU_WATER_INLET_1)│
│ D7  (INLET2) ├──────────┤ I2 (MCU_WATER_INLET_2)│
│ D8  (HEATER) ├──────────┤ I5 (MCU_HEATER)      │
│ D10 (MOT_EN) ├──────────┤ I6 (MOTOR_ENABLE)    │
│ D9  (MOT_PWM)├──────────┤ MOTOR_PWM (→ MOC3020)│
│ D2  (ZERO_X) ├──────────┤ ZERO_POINT_SIGNAL    │
│ D3  (TACHO)  ├──────────┤ TACHO_SIGNAL          │
│ A0  (NTC)    ├──────────┤ NTC (Sensor)          │
│ A4  (SDA)    ├──────────┤ J13 Pin 5 (→ ESP32)  │
│ A5  (SCL)    ├──────────┤ J13 Pin 4 (→ ESP32)  │
│ 5V           ├──────────┤ +5V                   │
│ GND          ├──────────┤ GND                   │
└──────────────┘          └──────────────────────┘
```

**Wichtig — AREF-Pin:** Der NTC-Spannungsteiler arbeitet mit 3.3V. Der AREF-Pin des Nano muss mit dem 3.3V-Pin des Nano verbunden werden (kurzer Draht auf dem Board). Ohne diese Verbindung sind die Temperaturwerte falsch.

**Wichtig — Sensortrennung:** Temperatur (`A0`) und Drehzahl/Null-Durchgang (`D2/D3`) liegen am Nano-Basiscontroller. Die Water-Level-Sensoren liegen **nicht** am Nano, sondern direkt am ESP32 (`GPIO34/35`). Die Nano-Pins `D11`/`D12` und `A3` sind dadurch frei fuer Erweiterungen.

---

## 4b. ATTiny3226 flashen (UPDI-Programmer nötig)

### 4b.1 SerialUPDI-Programmer verkabeln

Der ATTiny3226 wird über UPDI programmiert (Single-Wire-Interface über Pin 16 / PA0). Dazu wird ein USB-Serial Adapter mit einem 1kΩ Widerstand als SerialUPDI-Programmer verwendet.

**Verkabelung:**

```
USB-Serial Adapter          ATTiny3226 (J1 Header)
┌─────────────┐             ┌───────────┐
│          TX  ├──[1kΩ]──┬──┤ UPDI (Pin 3)
│          RX  ├──────────┘  │
│         GND  ├─────────────┤ GND  (Pin 1)
│         3V3  ├─────────────┤ +3V3 (Pin 2)  ← Optional, nur wenn Board
│              │             │                  nicht eigenständig versorgt
└─────────────┘             └───────────┘
```

Der **1kΩ Widerstand** zwischen TX und UPDI ist zwingend erforderlich — er trennt TX und RX auf der gemeinsamen UPDI-Leitung.

**Wichtig:** Die 3V3-Verbindung nur herstellen, wenn das Board NICHT über die 230V-Versorgung eingeschaltet ist. Niemals gleichzeitig über USB und Netzstrom versorgen!

### 4.2 COM-Port ermitteln

- **Windows**: Geräte-Manager → Anschlüsse (COM & LPT) → z.B. "USB-SERIAL CH340 (COM3)"
- **Linux**: `ls /dev/ttyUSB*` oder `ls /dev/ttyACM*`
- **macOS**: `ls /dev/cu.usbserial*`

### 4.3 Upload-Port konfigurieren (optional)

In der `platformio.ini` kann der Port fest eingetragen werden:

```ini
[env:Upload_UPDI]
upload_port = COM3          ; Windows - an eigenen Port anpassen
; upload_port = /dev/ttyUSB0  ; Linux
```

Wenn kein Port angegeben wird, versucht PlatformIO den Port automatisch zu erkennen.

### 4.4 Upload starten

**Über das Terminal:**
```bash
pio run -e Upload_UPDI -t upload
```

**Über die PlatformIO-Toolbar:**
1. Unten links in der Statusleiste das Environment auf **"Upload_UPDI"** umstellen
2. Auf den **Upload-Pfeil (→)** klicken

### Erwartete Ausgabe

```
pymcuprog write --erase --tool uart --device attiny3226 ...
Connecting to SerialUPDI
Writing flash... Done.
========================= [SUCCESS] =========================
```

---

## 5. ESP32 Zweitcontroller (Arduino / PlatformIO)

Der zweite Controller ist jetzt als normales Arduino-/PlatformIO-Projekt aufgebaut. Das Projekt liegt unter `ESP32Washy/`. Die alte ESPHome-Konfiguration unter `esphome/mygenwashy.yaml` bleibt als Referenz fuer Entitaeten und fruehe Tests im Repo, ist aber nicht mehr der primaere Flash-Pfad.

Die vollstaendige Register- und Kommando-Dokumentation fuer den ESP32 steht in `I2C_PROTOKOLL.md`.

### 5.1 ESP32-Projekt bauen

```bash
cd ESP32Washy
pio run
```

### 5.2 ESP32 flashen

```bash
cd ESP32Washy
pio run -t upload
```

Beim ersten Flash muss der ESP32 ueber USB verbunden sein. Danach laeuft der Zweitcontroller als normale Arduino-Firmware.

### 5.3 ESP32 mit dem Board verbinden

Der ESP32 wird über den **J13 Connector** (User Interface Sheet im Schematic) angeschlossen:

```
J13 Connector           ESP32
┌─────────────┐         ┌──────────┐
│ Pin 1: +5V  ├─────────┤ VIN (5V) │
│ Pin 2: +3V3 │ (n.c.)  │          │
│ Pin 3: ---  │         │          │
│ Pin 4: SCL  ├─────────┤ GPIO 22  │
│ Pin 5: SDA  ├─────────┤ GPIO 21  │
│ GND         ├─────────┤ GND      │
└─────────────┘         └──────────┘
```

**Hinweis:** Der ESP32 wird über die 5V des Boards versorgt (Pin 1 von J13 → VIN des ESP32). Die 3.3V-Leitung nicht verwenden — der ESP32 hat einen eigenen LDO.

**Wichtig fuer die Nano-Variante:** Der Arduino Nano selbst laeuft mit 5V, der I2C-Bus zum ESP32 darf aber nicht auf 5V hochgezogen werden. SDA und SCL muessen mit 3.3V Pull-ups betrieben oder ueber einen Level-Shifter gefuehrt werden.

### 5.3.1 Water-Level-Sensoren am ESP32

Die Wasserstandssensoren liegen in der aktuellen Architektur direkt am ESP32:

- `GPIO34` = `WATER_LEVEL_LOW`
- `GPIO35` = `WATER_LEVEL_HIGH`

Wichtig dazu:

- `GPIO34` und `GPIO35` sind beim ESP32 reine Eingänge.
- Es gibt dort **keine internen Pull-ups**.
- Fuer Active-Low-Schwimmerschalter sind daher externe Pull-ups nach `3.3V` erforderlich, z. B. `10k`.

### 5.4 WiFi-Zugangsdaten einrichten

Die WLAN-Daten stehen in `ESP32Washy/src/wifi_credentials.h`. Diese Datei ist **nicht** im Git-Repository (`.gitignore`). Erstelle sie manuell:

```cpp
#define WIFI_SSID "DeinNetzwerkName"
#define WIFI_PASS "DeinWLANPasswort"

// Optional, fuer Home Assistant MQTT:
// #define MQTT_SERVER "192.168.x.x"
// #define MQTT_USER   "mqtt_user"
// #define MQTT_PASS   "mqtt_passwort"
```

Wenn `wifi_credentials.h` fehlt oder leer ist, startet der ESP32 einen Access Point mit dem Namen `MyGenWashy-Setup` (Passwort: `mygenwashy`).

### 5.5 Web UI und REST API

Der ESP32 bringt eine Weboberflaeche mit Dark-Theme und zwei Tabs:

1. **Programmwahl**: Vordefinierte und eigene Waschprogramme auswaehlen, starten, pausieren, stoppen.
2. **Neu erstellen**: Eigene Programme mit Temperatur, RPM, Zeiten und Optionen anlegen. Eigene Programme werden auf dem Flash (LittleFS) gespeichert und ueberleben Neustarts.

Die Web UI ist erreichbar unter der IP-Adresse, die im Seriellen Monitor angezeigt wird, oder ueber mDNS: `http://mygenwashy.local`.

REST-API-Endpunkte:

- `GET /api/status` — JSON mit Temperatur, RPM, Fortschritt, Wasserstand, Programmstatus, WiFi-RSSI
- `GET /api/programs` — Liste aller verfuegbaren Waschprogramme (vordefiniert + benutzerdefiniert)
- `POST /api/select` (`idx=N`) — Programm nach Index auswaehlen
- `POST /api/start` — Gewaehltes Programm starten
- `POST /api/stop` — Programm sofort abbrechen (Notaus)
- `POST /api/pause` — Programm pausieren (Ausgaenge aus, Tuer verriegelt)
- `POST /api/resume` — Pausiertes Programm fortsetzen
- `POST /api/reset` — Fehler quittieren und Idle-Zustand wiederherstellen
- `POST /api/programs/add` (JSON Body) — Eigenes Programm erstellen
- `POST /api/programs/delete` (`idx=N`) — Eigenes Programm loeschen (nur Custom-Programme)

### 5.6 MQTT und Home Assistant

Wenn `MQTT_SERVER` in `wifi_credentials.h` definiert ist, verbindet sich der ESP32 automatisch mit dem MQTT-Broker und sendet **MQTT Auto-Discovery** Nachrichten fuer Home Assistant. Folgende Entities werden angelegt:

Sensoren: Temperatur, Drehzahl, Fortschritt (%), Status, Programm.
Binary Sensors: Tuer verriegelt, Controller verbunden, Fehler.
Buttons: Start, Stop, Pause, Resume, Reset.
Select: Waschprogramm-Auswahl (alle Programme als Dropdown).

Alle Entities erscheinen in Home Assistant automatisch unter dem Geraet "MyGenWashy" (Hersteller: MayerMakes / TuttleButtle).

MQTT-Konfiguration in `ESP32Washy/src/config.h`:

```cpp
#define MQTT_PORT             1883
#define MQTT_TOPIC_PREFIX     "mygenwashy"
#define MQTT_DISCOVERY_PREFIX "homeassistant"
```

### 5.7 Sicherheitshinweise

Drehzahl: Die Wasch-RPM liegen typisch bei 20..120 RPM (je nach Bewegungsprofil). Schleuderdrehzahlen bis 1500 RPM sind moeglich. Bei gemessener Ueberschreitung von 1500 RPM kappt der Basiscontroller die Motorleistung sofort und meldet `ERR_MOTOR_OVERSPEED`.

Watchdog: Der Nano-Basiscontroller besitzt einen Hardware-Watchdog. Nach einem Watchdog-Reset bleibt er im sicheren Fehlerzustand. Der ESP32 schreibt automatisch alle 500 ms ein Heartbeat-Update an den Nano (innerhalb des 2-Sekunden-Watchdog-Fensters). Faellt das Heartbeat aus, schaltet der Nano Heizung und Zulauf ab, stoppt den Motor und aktiviert Pumpe sowie Verriegelung.

---

## 6. Projektstruktur

```
MyGenWashy/
├── 3226TEST/                     ← ATTiny3226 PlatformIO-Projekt
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp              ← Low-Level-Basiscontroller
│       ├── config.h              ← Pin-Definitionen (ATTiny Pins)
│       ├── ntc.h                 ← NTC Temperaturmessung
│       ├── motor.h               ← TRIAC Motor-Steuerung + Tacho
│       └── i2c_slave.h           ← I2C Slave für ESP32
├── NanoWashy/                    ← Arduino Nano PlatformIO-Projekt
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp              ← Low-Level-Basiscontroller + Serial Debug
│       ├── config.h              ← Pin-Definitionen (Nano D2-D10, A0)
│       ├── ntc.h                 ← NTC (AREF=3.3V extern)
│       ├── motor.h               ← TRIAC (INT0/INT1 Interrupts)
│       └── i2c_slave.h           ← I2C Slave (A4/A5 fest)
├── ESP32Washy/                   ← ESP32 Arduino-/PlatformIO-Projekt
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp              ← Web UI, REST API, I2C-Master
│       ├── config.h              ← WLAN-, I2C- und Registerkonfiguration
│       ├── nano_bridge.h         ← I2C-Bridge zum Basiscontroller
│       ├── water_level.h         ← LOW/HIGH Sensoren direkt am ESP32
│       ├── wash_program.h        ← Ablaufsteuerung
│       ├── wash_programs.h       ← Standardprogramme
│       └── program_storage.h     ← Speicherung eigener Programme
├── esphome/                      ← Fruehe ESPHome-Referenz / Legacy
│   ├── mygenwashy.yaml           ← ESPHome-Konfiguration
│   └── secrets.yaml.example      ← Vorlage für WLAN-Credentials
├── mr_washi/                     ← KiCad Hardware-Dateien
└── SETUP_ANLEITUNG.md            ← Diese Datei
```

Basiscontroller und ESP32 verwenden dasselbe I2C-Protokoll mit Slave-Adresse `0x20` und identischer Register-Map.

---

## 7. Fehlerbehebung

### Build-Fehler: "platform not found"

PlatformIO muss die `atmelmegaavr` Platform nachinstallieren:

```bash
pio pkg install -p atmelmegaavr
```

### Build-Fehler: "Wire.h: No such file or directory"

Wire ist Teil des megaTinyCore und sollte automatisch verfügbar sein. Falls nicht:

```bash
pio pkg install -l "Wire"
```

### Upload-Fehler: "pymcuprog: command not found"

pymcuprog ist nicht installiert oder nicht im PATH:

```bash
pip install pymcuprog
```

Unter Windows muss ggf. der Python Scripts-Ordner zum PATH hinzugefügt werden (z.B. `C:\Users\<User>\AppData\Local\Programs\Python\Python3x\Scripts`).

### Upload-Fehler: "Could not connect to target"

1. Verkabelung prüfen — insbesondere den 1kΩ Widerstand zwischen TX und UPDI
2. Richtigen COM-Port in `platformio.ini` eintragen
3. Sicherstellen, dass kein anderes Programm den COM-Port belegt (Serial Monitor schließen!)
4. Versorgungsspannung prüfen — der ATTiny braucht 3.3V (oder 5V wenn über VCC versorgt)

### Upload-Fehler: "Device ID mismatch"

Der angeschlossene Chip stimmt nicht mit der Konfiguration überein. In `platformio.ini` prüfen, dass `board = ATtiny3226` korrekt ist.

### ESPHome: "I2C device not found at 0x20"

1. I2C-Verkabelung prüfen (SDA → GPIO21, SCL → GPIO22)
2. Pull-up Widerstände: Die meisten ESP32-Boards haben interne Pull-ups, die ausreichen. Falls nicht, 4.7kΩ Pull-ups nach 3.3V an SDA und SCL.
3. ATTiny muss laufen und mit Strom versorgt sein
4. Mismatch der Logikpegel prüfen — beide Seiten laufen auf 3.3V

---

## 8. Test-Modi

### 8.1 Nano POTI_TEST_MODE

Der Basiscontroller kann ohne echte Sensorhardware getestet werden. In `NanoWashy/src/config.h`:

```cpp
#define POTI_TEST_MODE    1   // 1 = Poti-Simulation, 0 = echte Hardware
```

Im Testmodus werden zwei Potentiometer als Sensoren verwendet:

- `A1` (Poti Temperatur): Linear 0..100 °C
- `A2` (Poti RPM): Linear 0..1500 RPM, simuliert Tacho-Signal

Zero-Cross und Tacho-ISRs sind deaktiviert, TRIAC wird nicht gezuendet. Watchdog-Signale werden kuenstlich erzeugt.

### 8.2 ESP32 WATER_LEVEL_MODE

Der Wasserstand-Sensor am ESP32 hat drei Betriebsmodi. Die Einstellung erfolgt in `ESP32Washy/src/config.h` (Zeile 49):

```cpp
#define WATER_LEVEL_MODE  2   // 0 = Schwimmerschalter, 1 = Poti, 2 = Timer
```

| Mode | Name | Hardware | Anwendung |
|------|------|----------|-----------|
| 0 | Schwimmerschalter | GPIO34 (LOW) + GPIO35 (HIGH) | Echtbetrieb / Produktion |
| 1 | Poti-Test | GPIO32 (ADC, 12-bit) | Hardware-Test am Steckbrett |
| 2 | Timer-Simulation | Keine | Software-Test ohne Sensor |

**Mode 0 — Schwimmerschalter (Produktion):** Echte Float-Switches an `GPIO34` (LOW) und `GPIO35` (HIGH). Active-Low, externe 10k Pull-ups nach 3.3V erforderlich (GPIO34/35 haben keine internen Pull-ups).

**Mode 1 — Poti-Test (Hardware-Testbetrieb):** Poti an `GPIO32` (ADC1_CH4, 12-bit, 0..4095). Drei Zonen: 0..1364 = Leer, 1365..2729 = LOW aktiv, 2730..4095 = LOW + HIGH aktiv. Ideal wenn man mit einem Poti am Steckbrett den Wasserstand simulieren will.

**Mode 2 — Timer-Simulation (Software-Test):** Kein Sensor noetig. Der Wasserstand wird rein zeitgesteuert simuliert: Beim Fuellen meldet sich LOW nach 10 Sekunden und HIGH nach 20 Sekunden. Beim Abpumpen sinkt der Pegel nach 20 Sekunden unter LOW. Die Zeiten sind in `config.h` ueber `SIM_FILL_LOW_MS`, `SIM_FILL_HIGH_MS` und `SIM_DRAIN_EMPTY_MS` anpassbar. Ideal fuer reine Software-Tests ohne jegliche Sensor-Hardware.

Nach dem Aendern des Modus muss der ESP32 neu kompiliert und geflasht werden (`pio run -t upload`).

---

## 9. Waschprogramme

### 9.1 Vordefinierte Programme

Der ESP32 bringt 7 Standard-Waschprogramme mit:

| Programm | Temp | Wasch-RPM | Schleuder-RPM | Waschzeit | Bewegung |
|----------|------|-----------|---------------|-----------|----------|
| Kalt 20°C | 20°C | 40 | 800 | 15 min | Schonend |
| Pflegeleicht 30°C | 30°C | 40 | 1000 | 20 min | Schonend |
| Bunt 40°C | 40°C | 55 | 1200 | 25 min | Normal |
| Koch 60°C | 60°C | 55 | 1400 | 30 min | Normal + Extra-Spuelung |
| Kochwaesche 90°C | 90°C | 55 | 1400 | 35 min | Normal + Vorwaesche + Extra-Spuelung |
| Schnell 40°C | 40°C | 55 | 1200 | 10 min | Normal |
| Nur Schleudern | — | — | 1200 | — | Nur Schleudern |

### 9.2 Waschbewegungsprofile

Die Trommel laeuft nicht durchgehend, sondern in Intervallen (Laufzeit / Pause):

- **Normal**: 55 RPM, 12s an / 3s aus — Standard fuer Baumwolle
- **Schonend**: 40 RPM, 5s an / 10s aus — Synthetik, empfindliche Stoffe
- **Wolle**: 25 RPM, 3s an / 27s aus — Minimal-Bewegung
- **Benutzerdefiniert**: Frei waehlbare Werte

Referenzen: Miele Professional Waschzeiten, AEG Wollprogramm, BSH Patent-Analyse.

### 9.3 Benutzerdefinierte Programme

Eigene Programme koennen ueber die Web UI erstellt werden. Sie werden als JSON in `/programs.json` auf dem ESP32-Flash (LittleFS) gespeichert und ueberleben Neustarts. Maximal 8 eigene Programme zusaetzlich zu den 7 Standard-Programmen.

---

## 10. Naechste Schritte

Nach erfolgreichem Build und Flash:

1. **Basiscontroller alleine testen**: Ohne ESP32-Zweitcontroller bleibt die Firmware im Bereitschaftszustand. Fuer erste Tests ohne angeschlossene 230V-Peripherie die Relais-Ausgaenge mit LEDs und Vorwiderstaenden bestuecken.

2. **Mit ESP32 testen**: ESP32 ueber J13 verbinden. Fuer den Wasserstand `WATER_LEVEL_MODE` in `config.h` waehlen: Mode 2 (Timer) fuer reine Software-Tests, Mode 1 (Poti an GPIO32) fuer Hardware-Tests am Steckbrett, Mode 0 (Schwimmerschalter an GPIO34/35) fuer den Echtbetrieb. Web UI im Browser oeffnen und Waschprogramme testen.

3. **Home Assistant anbinden**: MQTT-Server in `wifi_credentials.h` eintragen. Nach dem Neustart erscheinen alle Entities automatisch in Home Assistant.

Die Trennung ist damit:

- Basiscontroller (Nano): Motorregelung, Rampen, Relaisumsetzung, Temperaturueberwachung
- ESP32-Zweitcontroller: Waschprogramm-Ablaufsteuerung, Wasserstandsmessung, Web UI, MQTT/Home Assistant
