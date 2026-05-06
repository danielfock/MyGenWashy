#ifndef WASH_PROGRAMS_H
#define WASH_PROGRAMS_H

#include <Arduino.h>

// ============================================================================
// Waschprogramm-Definitionen
//
// Jedes Programm definiert:
//   - Name (fuer UI und MQTT)
//   - Zieltemperatur
//   - Wasch-Drehzahl und Schleuderdrehzahl
//   - Waschbewegung als Rhythmus aus Lauf- und Pausezeit
//   - Dauer der einzelnen Phasen
//   - Optionale Vorwaesche
//
// Vordefinierte Programme basieren auf gaengigen Standardprogrammen
// europaeischer Waschmaschinen (20/30/40/60/90 Grad).
//
// Recherchierte Referenzen fuer realistische Waschbewegungen:
//   - Miele Professional: Normal 12s an / 3s aus, Gentle 5s / 10s,
//     Very gentle 3s / 27s, frei waehlbare 20..70 RPM
//   - AEG Wollprogramm: hoher Wasserstand, wenig und sanfte Trommelbewegung
//   - BSH Patent: typischer Waschbereich ca. 55 RPM, optimierte Bereiche
//     um 35 RPM und 100 RPM je nach Mechanik
// ============================================================================

#define PROGRAM_NAME_MAX  24
#define MAX_CUSTOM_PROGRAMS 8
#define MAX_TOTAL_PROGRAMS  (NUM_DEFAULT_PROGRAMS + MAX_CUSTOM_PROGRAMS)

#define MIN_WASH_RPM_REALISTIC  20
#define MAX_WASH_RPM_REALISTIC 120
#define MAX_SPIN_RPM_ALLOWED  1500
#define MIN_WASH_RUN_SEC         3
#define MAX_WASH_RUN_SEC       120
#define MAX_WASH_PAUSE_SEC     120

enum WashMotionProfile : uint8_t {
    WMOTION_NORMAL = 0,
    WMOTION_GENTLE,
    WMOTION_WOOL,
    WMOTION_CUSTOM,
    WMOTION_COUNT
};

struct WashMotionDefaults {
    uint16_t rpm;
    uint8_t runSec;
    uint8_t pauseSec;
};

inline uint8_t sanitizeWashMotionProfile(uint8_t profile) {
    return (profile < WMOTION_COUNT) ? profile : (uint8_t)WMOTION_NORMAL;
}

inline const char* washMotionProfileName(uint8_t profile) {
    switch (sanitizeWashMotionProfile(profile)) {
        case WMOTION_NORMAL: return "Normal";
        case WMOTION_GENTLE: return "Schonend";
        case WMOTION_WOOL:   return "Wolle";
        case WMOTION_CUSTOM: return "Benutzerdefiniert";
        default:             return "Normal";
    }
}

inline WashMotionDefaults getWashMotionDefaults(uint8_t profile) {
    switch (sanitizeWashMotionProfile(profile)) {
        case WMOTION_GENTLE:
            return { 40, 5, 10 };
        case WMOTION_WOOL:
            return { 25, 3, 27 };
        case WMOTION_CUSTOM:
            return { 55, 12, 3 };
        case WMOTION_NORMAL:
        default:
            return { 55, 12, 3 };
    }
}

struct WashProgramDef {
    char     name[PROGRAM_NAME_MAX];
    uint8_t  tempC;          // Zieltemperatur in Grad C (0 = kalt)
    uint16_t washRPM;        // Drehzahl beim Waschen
    uint16_t spinRPM;        // Drehzahl beim Schleudern
    uint16_t washMinutes;    // Waschdauer in Minuten
    uint16_t spinMinutes;    // Schleuderdauer in Minuten
    uint8_t  washMotionProfile; // Bewegungsprofil: normal/schonend/wolle/custom
    uint8_t  washRunSec;     // Trommel-Laufzeit je Waschzyklus
    uint8_t  washPauseSec;   // Pause zwischen Waschzyklen
    bool     prewash;        // Vorwaesche (kurzes Fuellen+Abpumpen vor dem Hauptgang)
    bool     extraRinse;     // Zusaetzlicher Spuelgang
    bool     isCustom;       // true = vom User erstellt, loeschbar
};

// ============================================================================
// Vordefinierte Standard-Waschprogramme
//
// Quellen:
//   - 20°C: Empfindliche Stoffe, Seide (EU-Pflicht seit 2013)
//   - 30°C: Synthetik, Mischgewebe, Alltagskleidung (Energiespar)
//   - 40°C: Standard Buntwaesche, Baumwolle bunt
//   - 60°C: Handtuecher, Bettwaesche, Unterwaesche (NHS-Empfehlung)
//   - 90°C: Stark verschmutzt, Hygienewaesche, Allergiker
//   - Schnell: Leicht verschmutzte Kleidung, kurzes Programm
//   - Schleudern: Nur schleudern ohne Waschen
// ============================================================================

static const WashProgramDef DEFAULT_PROGRAMS[] = {
    // Name                  Temp  WashRPM SpinRPM Wash Spin Motion            On  Off Pre   Rinse Custom
    { "Kalt 20°C",          20,     40,     800,   15,   3,  WMOTION_GENTLE,   5, 10, false, false, false },
    { "Pflegeleicht 30°C",  30,     40,    1000,   20,   4,  WMOTION_GENTLE,   5, 10, false, false, false },
    { "Bunt 40°C",          40,     55,    1200,   25,   5,  WMOTION_NORMAL,  12,  3, false, false, false },
    { "Koch 60°C",          60,     55,    1400,   30,   5,  WMOTION_NORMAL,  12,  3, false, true,  false },
    { "Kochwäsche 90°C",    90,     55,    1400,   35,   5,  WMOTION_NORMAL,  12,  3, true,  true,  false },
    { "Schnell 40°C",       40,     55,    1200,   10,   3,  WMOTION_NORMAL,  12,  3, false, false, false },
    { "Nur Schleudern",      0,      0,    1200,    0,   5,  WMOTION_NORMAL,   0,  0, false, false, false },
};

static const uint8_t NUM_DEFAULT_PROGRAMS = sizeof(DEFAULT_PROGRAMS) / sizeof(DEFAULT_PROGRAMS[0]);

// ============================================================================
// Hilfsfunktionen
// ============================================================================

inline void normalizeWashMotion(WashProgramDef& p, bool legacyProgram = false) {
    p.washMotionProfile = sanitizeWashMotionProfile(p.washMotionProfile);

    if (p.washMinutes == 0) {
        p.washRPM = 0;
        p.washRunSec = 0;
        p.washPauseSec = 0;
        return;
    }

    WashMotionDefaults defaults = getWashMotionDefaults(p.washMotionProfile);

    if (legacyProgram && p.washRPM > MAX_WASH_RPM_REALISTIC) {
        p.washRPM = defaults.rpm;
    }

    if (p.washRPM == 0) {
        p.washRPM = defaults.rpm;
    }
    p.washRPM = constrain(p.washRPM, MIN_WASH_RPM_REALISTIC, MAX_WASH_RPM_REALISTIC);

    if (p.washRunSec == 0) {
        p.washRunSec = defaults.runSec;
    }
    p.washRunSec = constrain(p.washRunSec, MIN_WASH_RUN_SEC, MAX_WASH_RUN_SEC);

    if (p.washPauseSec == 0 && p.washMotionProfile != WMOTION_CUSTOM) {
        p.washPauseSec = defaults.pauseSec;
    }
    p.washPauseSec = constrain(p.washPauseSec, 0, MAX_WASH_PAUSE_SEC);
}

// Programm-Validierung
inline bool isValidProgram(const WashProgramDef& p) {
    if (strlen(p.name) == 0) return false;
    if (p.tempC > 95) return false;
    if (p.washMinutes > 0) {
        if (p.washRPM < MIN_WASH_RPM_REALISTIC || p.washRPM > MAX_WASH_RPM_REALISTIC) return false;
        if (p.washRunSec < MIN_WASH_RUN_SEC || p.washRunSec > MAX_WASH_RUN_SEC) return false;
        if (p.washPauseSec > MAX_WASH_PAUSE_SEC) return false;
    }
    if (p.spinRPM > MAX_SPIN_RPM_ALLOWED) return false;
    if (p.washMinutes > 120) return false;
    if (p.spinMinutes > 30) return false;
    return true;
}

// Geschaetzte Gesamtdauer in Minuten (fuer UI-Anzeige)
inline uint16_t estimatedDurationMin(const WashProgramDef& p) {
    uint16_t total = 0;
    total += 1;                           // Verriegeln
    if (p.tempC > 0 || p.washMinutes > 0) {
        total += 3;                       // Fuellen (~3 min)
    }
    if (p.prewash) {
        total += 5;                       // Vorwaesche + Abpumpen
    }
    if (p.tempC > 0) {
        // Grobe Schaetzung Heizzeit: ~2 min pro 10 Grad ueber Raumtemp
        total += ((p.tempC > 20) ? (p.tempC - 20) / 5 : 0);
    }
    total += p.washMinutes;
    total += 3;                           // Abpumpen 1
    total += p.spinMinutes;
    total += 2;                           // Abpumpen 2
    total += 1;                           // Motor-Stopp + Entriegeln
    return total;
}

#endif // WASH_PROGRAMS_H
