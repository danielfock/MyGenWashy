# MyGenWashy I2C-Protokoll fuer den ESP32-Zweitcontroller

Diese Beschreibung gilt fuer beide Basiscontroller-Varianten:

- `3226TEST` mit ATTiny3226
- `NanoWashy` mit ATmega328P / Arduino Nano

In beiden Faellen ist der Basiscontroller der I2C-Slave und der ESP32-Zweitcontroller der I2C-Master.

## 1. Systemtrennung

Die Ebenen sind aus Sicherheitsgruenden getrennt:

- Hardware-/Relaislogik: unterste Sicherheitsebene
- Basiscontroller: Motorregelung, Rampen, Relaisumsetzung, Temperaturueberwachung, Temperatur-/Statusreadout
- ESP32-Zweitcontroller: User Interface, Programmablaufsteuerung, Smarthome

Der Basiscontroller enthaelt **keine Waschprogramm-Ablauflogik**. Er:

- ueberwacht Null-Durchgang und Tachosignal
- regelt die Motordrehzahl inkl. Hochlauf- und Niederlauf-Rampen
- setzt Relaisbefehle um
- ueberwacht die Temperatur
- liest die Temperatur und meldet sie per I2C

Der ESP32-Zweitcontroller entscheidet:

- welche Relais angefordert werden
- wann die Trommel laufen soll
- welche Drehzahl vorgegeben wird
- wie der komplette Waschprogrammablauf aussieht
- und liest Water Level LOW/HIGH direkt ueber eigene GPIOs

Zusaetzlich enthaelt der Basiscontroller eigene Schutzfunktionen, damit die untere Ebene auch bei Fehlern des ESP32 oder bei Signalverlust in einen sicheren Zustand geht.

## 2. Rollen und Busdaten

- Slave-Adresse: `0x20`
- Busmodus: I2C / 7 Bit
- Empfohlene Taktfrequenz: `100 kHz`
- Registermodell: registerbasiert mit internem Registerzeiger

Schreibzugriff:

```text
[register] [byte0] [byte1] ...
```

Lesezugriff:

1. Master schreibt zuerst nur die Startadresse.
2. Danach folgt ein Repeated-Start oder ein neuer Read-Zugriff.
3. Der Slave liefert ab dieser Adresse fortlaufend Registerbytes.

Beispiel: Statusblock lesen

```cpp
Wire.beginTransmission(0x20);
Wire.write(0x00);
Wire.endTransmission(false);
Wire.requestFrom(0x20, 12);
```

Beispiel: Relais- und Drehzahlanforderung schreiben

```cpp
Wire.beginTransmission(0x20);
Wire.write(0x11);
Wire.write(0x21);   // CTRL_LOCK | CTRL_MOTOR_ENABLE
Wire.write(0x05);   // RPM high byte
Wire.write(0x78);   // RPM low byte -> 1400
Wire.endTransmission();
```

## 3. Elektrische Hinweise

- Gemeinsame Masse zwischen zweitem Controller und Basiscontroller ist Pflicht.
- `SDA` und `SCL` muessen als Open-Drain-Bus mit Pull-ups betrieben werden.
- Bei der ATTiny-Variante ist der Bus nativ `3.3 V`.
- Bei der Nano-Variante laeuft der ATmega328P zwar mit `5 V`, die I2C-Pull-ups fuer den ESP32 muessen aber trotzdem auf `3.3 V` liegen oder ueber einen Level-Shifter gefuehrt werden.
- Die ESP32-Pins duerfen nicht mit `5 V` auf `SDA` oder `SCL` beaufschlagt werden.

Typische Verdrahtung:

- ESP32 `GPIO21` -> `SDA`
- ESP32 `GPIO22` -> `SCL`
- ESP32 `GND` -> `GND`

## 4. Register-Map

### Leseregister `0x00..0x0B`

| Adresse | Name | Bedeutung |
|---|---|---|
| `0x00` | `REG_STATUS` | Aktueller Controller-Zustand |
| `0x01` | `REG_ERROR` | Aktueller Fehlercode |
| `0x02` | `REG_TEMP_INT` | Temperatur Ganzzahl in `degC` |
| `0x03` | `REG_TEMP_FRAC` | Temperatur Nachkommastelle in `0.1 degC` |
| `0x04` | `REG_RPM_HIGH` | High-Byte der aktuellen Ist-Drehzahl |
| `0x05` | `REG_RPM_LOW` | Low-Byte der aktuellen Ist-Drehzahl |
| `0x06` | `REG_TARGET_RPM_HIGH` | High-Byte der angeforderten Soll-Drehzahl |
| `0x07` | `REG_TARGET_RPM_LOW` | Low-Byte der angeforderten Soll-Drehzahl |
| `0x08` | `REG_FLAGS` | Bitfeld mit Statusflags |
| `0x09` | `REG_CONTROL_REQUEST` | Aktuelle Relais-/Motoranforderung |
| `0x0A` | `REG_WATER_LEVEL` | Reserviert/Legacy, aktuell immer `0` |
| `0x0B` | `REG_OUTPUT_STATE` | Tatsaechlich vom Basiscontroller gesetzte Ausgangsbits |

### Schreibregister `0x10..0x13`

| Adresse | Name | Bedeutung |
|---|---|---|
| `0x10` | `REG_COMMAND` | Einmaliges Kommando |
| `0x11` | `REG_WRITE_CONTROL` | Gewuenschte Relais-/Motorbits |
| `0x12` | `REG_WRITE_RPM_HIGH` | Soll-Drehzahl High-Byte |
| `0x13` | `REG_WRITE_RPM_LOW` | Soll-Drehzahl Low-Byte |

## 5. Controller-Zustaende und Fehlercodes

### `REG_STATUS`

| Wert | Zustand |
|---|---|
| `0` | `IDLE` |
| `1` | `ACTIVE` |
| `2` | `ERROR` |

### `REG_ERROR`

| Wert | Fehler |
|---|---|
| `0` | `ERR_NONE` |
| `1` | `ERR_OVERHEAT` |
| `2` | `ERR_SENSOR_FAIL` |
| `3` | `ERR_MOTOR_OVERSPEED` |
| `4` | `ERR_ESP32_TIMEOUT` |
| `5` | `ERR_ZERO_CROSS_LOST` |
| `6` | `ERR_MOTOR_STALL` |
| `7` | `ERR_WATCHDOG_RESET` |

Bei einem Fehler zieht der Basiscontroller die Ausgaenge in einen sicheren Zustand:

- Heizung aus
- Zulauf aus
- Motor aus
- Pumpe an
- Tuerverriegelung an

## 5.1 Watchdogs und Fail-Safes

- Der Basiscontroller besitzt einen Hardware-Watchdog. Nach einem Watchdog-Reset startet er nicht still weiter, sondern geht zunaechst mit `ERR_WATCHDOG_RESET` in den sicheren Fehlerzustand.
- Solange aktive Anforderungen anliegen, muss der ESP32 die Register `0x11..0x13` zyklisch aktualisieren. Bleibt dieses Heartbeat-Update laenger als `2 s` aus, setzt der Basiscontroller `ERR_ESP32_TIMEOUT`.
- Wenn bei aktivem Motor nach einer kurzen Freigabezeit kein gueltiger Netznulldurchgang mehr erkannt wird, setzt der Basiscontroller `ERR_ZERO_CROSS_LOST`.
- Wenn bei aktivem Motor nach dem Hochlauf kein Tachosignal mehr eintrifft, setzt der Basiscontroller `ERR_MOTOR_STALL`.
- Bei allen Schutzfehlern bleiben nur `CMD_RESET_ERROR` und ein neuer sicherer Neustart zulaessig.

## 6. Statusbits

### `REG_FLAGS`

| Bit | Maske | Bedeutung |
|---|---|---|
| 0 | `0x01` | Tuerverriegelung aktiv |
| 1 | `0x02` | Heizung aktiv |
| 2 | `0x04` | Pumpe aktiv |
| 3 | `0x08` | Motor liefert Leistung |
| 4 | `0x10` | Mindestens ein Zulauf aktiv |
| 5 | `0x20` | ESP32-Zweitcontroller per I2C erkannt |
| 6 | `0x40` | Fehler aktiv |
| 7 | `0x80` | Basiscontroller verarbeitet gerade Anforderungen |

### `REG_CONTROL_REQUEST`

| Bit | Maske | Bedeutung |
|---|---|---|
| 0 | `0x01` | `CTRL_LOCK` |
| 1 | `0x02` | `CTRL_DRAIN` |
| 2 | `0x04` | `CTRL_WATER_IN1` |
| 3 | `0x08` | `CTRL_WATER_IN2` |
| 4 | `0x10` | `CTRL_HEATER` |
| 5 | `0x20` | `CTRL_MOTOR_ENABLE` |

### `REG_OUTPUT_STATE`

| Bit | Maske | Bedeutung |
|---|---|---|
| 0 | `0x01` | `OUT_LOCK` |
| 1 | `0x02` | `OUT_DRAIN` |
| 2 | `0x04` | `OUT_WATER_IN1` |
| 3 | `0x08` | `OUT_WATER_IN2` |
| 4 | `0x10` | `OUT_HEATER` |
| 5 | `0x20` | `OUT_MOTOR_ENABLE` |

### `REG_WATER_LEVEL`

| Bit | Maske | Bedeutung |
|---|---|---|
| 0 | `0x01` | Reserviert |
| 1 | `0x02` | Reserviert |

Der Wasserstand wird in der aktuellen Architektur **nicht** mehr ueber den Basiscontroller gelesen. LOW- und HIGH-Sensor liegen am ESP32-Zweitcontroller und werden dort direkt fuer Ablaufsteuerung, Web-UI und Smarthome ausgewertet.

## 7. Kommandos

| Wert | Name | Wirkung |
|---|---|---|
| `0x00` | `CMD_NOP` | keine Aktion |
| `0x01` | `CMD_ALL_OFF` | Remote-Anforderung loeschen und alles abschalten |
| `0x02` | `CMD_RESET_ERROR` | Fehler quittieren, Remote-Anforderung loeschen |

Die normale Steuerung erfolgt nicht ueber `START/STOP`-Programme, sondern ueber:

- `REG_WRITE_CONTROL` fuer Relais- und Motoranforderungen
- `REG_WRITE_RPM_HIGH/LOW` fuer die Drehzahlvorgabe

## 8. Drehzahl und Schutzfunktionen

- `0 RPM` bedeutet Motor aus.
- Gueltige Sollwerte liegen zwischen `30` und `1500 RPM`.
- Werte zwischen `1` und `29 RPM` werden als `0` behandelt.
- Werte ueber `1500 RPM` werden auf `1500 RPM` begrenzt.
- Der Motor startet nur, wenn `CTRL_MOTOR_ENABLE` gesetzt ist und eine Soll-Drehzahl groesser `0` anliegt.
- Der Basiscontroller rampt die Drehzahl intern hoch und wieder herunter.
- Ueberschreitet die Ist-Drehzahl `1500 RPM`, wird die Motorleistung sofort gekappt und `ERR_MOTOR_OVERSPEED` gesetzt.
- Der ESP32 darf die Drehzahl anfordern, regelt aber nie direkt die Phasenanschnitt-Hardware. Diese untere Ebene bleibt ausschliesslich im Basiscontroller.

## 9. ESP32-Arduino-Beispiel

Status lesen:

```cpp
uint8_t data[12];
Wire.beginTransmission(0x20);
Wire.write(0x00);
if (Wire.endTransmission(false) == 0) {
  if (Wire.requestFrom((uint8_t)0x20, (uint8_t)12) == 12) {
    for (uint8_t i = 0; i < 12; ++i) {
      data[i] = Wire.read();
    }
  }
}
```

Relais- und Drehzahlanforderung schreiben:

```cpp
uint16_t rpm = 1400;
Wire.beginTransmission(0x20);
Wire.write(0x11);
Wire.write(0x21);  // CTRL_LOCK | CTRL_MOTOR_ENABLE
Wire.write((uint8_t)(rpm >> 8));
Wire.write((uint8_t)(rpm & 0xFF));
Wire.endTransmission();
```

Aktive Anforderungen zyklisch als ESP32-Heartbeat erneuern:

```cpp
if ((requestedControlMask != 0 || requestedRpm != 0) &&
    (millis() - lastHeartbeatMs) >= 1000UL) {
  writeRemoteRequest();
  lastHeartbeatMs = millis();
}
```

Alles aus:

```cpp
Wire.beginTransmission(0x20);
Wire.write(0x10);
Wire.write(0x01);
Wire.endTransmission();
```

Die aktuelle Referenz-Implementierung dazu liegt im Arduino-Projekt [ESP32Washy/src/main.cpp](C:\Users\danie\Dokumente\GitHub\MyGenWashy\ESP32Washy\src\main.cpp). Die fruehere ESPHome-YAML bleibt nur noch als Legacy-Referenz im Ordner `esphome/`.
