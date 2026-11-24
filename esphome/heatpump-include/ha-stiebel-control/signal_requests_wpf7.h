/*
 * Signal Request Table for WPF 7 (Sole/Wasser) Heat Pump
 * 
 * v0.6 FINAL: Only verified working signals
 * v0.7 (ressl 2.3.0): INITIALISIERUNG removed (never answered),
 *                       RAUMISTTEMP polled every 10 min instead of 30 s (always -255)
 * v0.8 (ressl 2.4.0): RAUMISTTEMP read back from 0x601 and 0x301 every minute
 *                       (MANAGER 0x480 always answered -255)
 * 
 * Removed (no solar thermal):    SAMMLERISTTEMP
 * Removed (no WW tank):          SPEICHERISTTEMP, SPEICHERSOLLTEMP
 * Removed (returns -255):        RAUMISTTEMP, VORLAUFISTTEMP, KESSELSOLLTEMP,
 *                                 VORLAUFSOLLTEMP (KESSEL), ANZEIGE_HOCHDRUCK,
 *                                 ANZEIGE_NIEDERDRUCK, VERDICHTER,
 *                                 LAUFZEIT_VERDICHTER_*, HEIZKURVE,
 *                                 FUSSPUNKT_HEIZKURVE, BIVALENZALTERNATIVTEMPERATUR_HZG,
 *                                 RAUM_FROSTSCHUTZTEMP, MAX/MIN_RUECKLAUFSOLLTEMP_WP,
 *                                 WARTUNG_BETRIEBS_STD, STILLSTANDZEIT
 * Removed (undecodable):         SOMMERBETRIEB (responds with "?")
 * Removed (no response):         All HEIZMODUL (0x500) signals
 *
 * Scan performed 2026-02-10 with both KESSEL (0x180) and MANAGER (0x480)
 * 
 * Hardware: Waveshare ESP32-S3-RS485-CAN Board
 * CAN Bus: Elster protocol @ 20kbps
 */

#ifndef SIGNAL_REQUESTS_WPF7_H
#define SIGNAL_REQUESTS_WPF7_H

#include "config.h"

extern const SignalRequest signalRequests[] = {
    // ========================================================================
    // TIME AND DATE (MANAGER 0x480)
    // ========================================================================
    {"JAHR", FREQ_1MIN, cm_manager},
    {"MONAT", FREQ_1MIN, cm_manager},
    {"TAG", FREQ_1MIN, cm_manager},
    {"STUNDE", FREQ_1MIN, cm_manager},
    {"MINUTE", FREQ_1MIN, cm_manager},
    {"SEKUNDE", FREQ_1MIN, cm_manager},
    
    // ========================================================================
    // STATUS AND CONTROL (MANAGER 0x480)
    // ========================================================================
    {"EVU_SPERRE_AKTIV", FREQ_30S, cm_manager},
    // WW_ECO removed — no hot water tank on WPF 7
    {"PROGRAMMSCHALTER", FREQ_1MIN, cm_manager},
    // INITIALISIERUNG removed: the MANAGER never answered it (no response since 2026-02)
    
    // ========================================================================
    // ROOM TEMPERATURE SETPOINTS (MANAGER 0x480)
    // ========================================================================
    {"RAUMSOLLTEMP_I", FREQ_30S, cm_manager},
    {"RAUMSOLLTEMP_II", FREQ_30S, cm_manager},
    {"RAUMSOLLTEMP_III", FREQ_30S, cm_manager},
    {"RAUMSOLLTEMP_NACHT", FREQ_30S, cm_manager},
    
    // ========================================================================
    // BUFFER SETPOINTS (MANAGER 0x480)
    // Heizungspuffer — no WW-Trinkwasserspeicher on this system
    // EINSTELL_SPEICHERSOLLTEMP = 47°C (Puffer-Sollwert)
    // EINSTELL_SPEICHERSOLLTEMP2 = 10°C (Eco/Absenkung)
    // ========================================================================
    {"EINSTELL_SPEICHERSOLLTEMP", FREQ_30S, cm_manager},
    {"EINSTELL_SPEICHERSOLLTEMP2", FREQ_30S, cm_manager},
    
    // ========================================================================
    // TEMPERATURES (KESSEL 0x180)
    // ========================================================================
    {"RUECKLAUFISTTEMP", FREQ_30S, cm_kessel},      // Return flow: 25-30°C
    {"WPVORLAUFIST", FREQ_30S, cm_kessel},           // HP flow: 28-35°C
    {"AUSSENTEMP", FREQ_30S, cm_kessel},             // Outside temp
    {"QUELLENSOLLTEMPERATUR", FREQ_30S, cm_kessel},  // Source setpoint: -5.0°C
    {"HEISSGAS_TEMP", FREQ_30S, cm_kessel},          // Hot gas: 26-62°C
    {"PUFFERSOLL", FREQ_30S, cm_kessel},             // Buffer setpoint: 26-28°C
    
    // ========================================================================
    // BUFFER SETPOINTS (KESSEL 0x180) — redundancy
    // ========================================================================
    {"EINSTELL_SPEICHERSOLLTEMP", FREQ_30S, cm_kessel},
    {"EINSTELL_SPEICHERSOLLTEMP2", FREQ_30S, cm_kessel},
    
    // ========================================================================
    // DIAGNOSTICS & WPM i STATUS (verified via 2026-11-16 CAN scan & ComfortSoft 4.2.0)
    // - 0x01d4 QUELLE_IST (KESSEL): Brine inlet temp (0.1 degC)
    // - 0x0268 MASCHINENDRUCK (KESSEL): R410A high-pressure transducer P1 (0.1 bar gauge)
    // - 0x0a20 SAMMEL_RELAISSTATUS (KESSEL): CWpmIStatusPage bitmask:
    //     0x0001 DHC1, 0x0002 DHC2, 0x000C (bits 2&3) Verdichter, 0x0010 WW-Ventil,
    //     0x0020 Heizkreispumpe, 0x0040 Pufferladepumpe, 0x0100 Quellenpumpe, 0x0200 Kuehlen
    // - 0xfdab ZWEITER_WE_STATUS (MANAGER): in ComfortSoft CommunicationElsterDeclarations.dll
    //     0xfdab is cWP_EVU (1 = EVU release); kept for existing diagnostic entity compatibility.
    // - 0x01c4 LAUFZEIT_WP1 (KESSEL): compressor total runtime (h)
    // - 0x0259/0x025a LAUFZEIT_DHC1/2 (KESSEL): electric backup stage 1/2 runtime (h)
    // - 0x01cc STILLSTANDZEIT_0 (MANAGER): remaining compressor standstill time (min)
    // - 0x010e HEIZKURVE (MANAGER): heating curve slope (0.23; KESSEL answered 0.00)
    // - 0x0004 VORLAUFSOLLTEMP (BEDIENMODUL_2 0x301): flow target temp (0x601 answered -40)
    // - 0xfdb4 SOMMERBETRIEB (KESSEL): summer mode (et_little_bool, 0x0100 = on)
    // ========================================================================
    {"QUELLE_IST", FREQ_30S, cm_kessel},
    {"MASCHINENDRUCK", FREQ_30S, cm_kessel},
    {"SAMMEL_RELAISSTATUS", FREQ_30S, cm_kessel},
    {"ZWEITER_WE_STATUS", FREQ_1MIN, cm_manager},
    {"LAUFZEIT_WP1", FREQ_10MIN, cm_kessel},
    {"LAUFZEIT_DHC1", FREQ_10MIN, cm_kessel},
    {"LAUFZEIT_DHC2", FREQ_10MIN, cm_kessel},
    {"STILLSTANDZEIT_0", FREQ_30S, cm_manager},
    {"HEIZKURVE", FREQ_10MIN, cm_manager},
    {"VORLAUFSOLLTEMP", FREQ_1MIN, cm_bedienmodul_2},
    {"SOMMERBETRIEB", FREQ_5MIN, cm_kessel},
    
    // ========================================================================
    // ERROR (KESSEL 0x180)
    // Unblacklisted in 2.9.0: KESSEL 0x180 cleanly responds 0x0000 (no error)
    // ========================================================================
    {"FEHLERMELDUNG", FREQ_1MIN, cm_kessel},
    
    // ========================================================================
    // MISCHERMODUL_2 (0x601)
    // ========================================================================
    {"BRENNER", FREQ_1MIN, cm_mischermodul_2},
};

extern const size_t SIGNAL_REQUEST_COUNT_VALUE = sizeof(signalRequests) / sizeof(SignalRequest);

#endif // SIGNAL_REQUESTS_WPF7_H
