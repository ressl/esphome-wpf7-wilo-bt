/*
 *
 *  Copyright (C) 2023 Bastian Stahmer (bastian@stahmer.net)
 *  This program is part of the ESPHome / Home Assistant Program "ha-stiebel-control"
 *  and is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program. If not, see http://www.gnu.org/licenses/ .
 */

#if !defined(ha_stiebel_control_H)
#define ha_stiebel_control_H

// ============================================================================
// INCLUDES
// ============================================================================
#include "ElsterTable.h"
#include "KElsterTable.h"
#include "config.h"
#include "language_select.h"
#include <driver/twai.h>
#include <sstream>
#include <iomanip>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <cmath>
// ============================================================================
// CAN BUS MEMBER DEFINITIONS
// ============================================================================

typedef struct
{
    const char *Name;
    uint32_t CanId;
} CanMember;

static const CanMember CanMembers[] =
    {
        //  Name  CanId
        {"KESSEL", 0x180},
        {"ATEZ", 0x280},
        {"BEDIENMODUL_1", 0x300},
        {"BEDIENMODUL_2", 0x301},
        {"BEDIENMODUL_3", 0x302},
        {"BEDIENMODUL_4", 0x303},
        {"RAUMFERNFUEHLER", 0x400},
        {"RAUMFERNFUEHLER_2", 0x401},
        {"MANAGER", 0x480},
        {"HEIZMODUL", 0x500},
        {"BUSKOPPLER", 0x580},
        {"MISCHERMODUL_1", 0x600},
        {"MISCHERMODUL_2", 0x601},
        {"MISCHERMODUL_3", 0x602},
        {"MISCHERMODUL_4", 0x603},
        {"PC", 0x680},
        {"FREMDGERAET", 0x700},
        {"DCF_MODUL", 0x780},
        {"OTHER", 0x000}};

typedef enum
{
    // Die Reihenfolge muss mit CanMembers übereinstimmen!
    cm_kessel = 0,
    cm_atez,
    cm_bedienmodul_1,
    cm_bedienmodul_2,
    cm_bedienmodul_3,
    cm_bedienmodul_4,
    cm_raumfernfuehler,
    cm_raumfernfuehler_2,
    cm_manager,
    cm_heizmodul,
    cm_buskoppler,
    cm_mischermodul_1,
    cm_mischermodul_2,
    cm_mischermodul_3,
    cm_mischermodul_4,
    cm_pc,
    cm_fremdgeraet,
    cm_dcf_modul,
    cm_other
} CanMemberType;

// ============================================================================
// MQTT AUTO-DISCOVERY CONFIGURATION
// ============================================================================

// ============================================================================
// CALCULATED SENSOR DISCOVERY CONFIGURATION
// ============================================================================

struct CalculatedSensorConfig {
    const char* uniqueId;          // Unique ID for Home Assistant
    const char* name;              // Friendly name
    const char* stateTopic;        // MQTT state topic
    const char* component;         // "sensor" or "binary_sensor"
    const char* deviceClass;       // Device class (empty string if none)
    const char* unit;              // Unit of measurement (empty string if none)
    const char* stateClass;        // State class (empty string if none)
    const char* icon;              // MDI icon
    const char* payloadOn;         // For binary sensors (empty string if not applicable)
    const char* payloadOff;        // For binary sensors (empty string if not applicable)
    const char* entityCategory;    // "diagnostic" | "config" | "" (empty = normal)
    bool enabledByDefault;         // false = disabled in HA until user enables
};

static const CalculatedSensorConfig calculatedSensors[] = {
    // Date sensor
    {"stiebel_calculated_date", "Datum", "heatingpump/calculated/date/state", 
     "sensor", "", "", "", "mdi:calendar", "", "", "", true},

    // Time sensor
    {"stiebel_calculated_time", "Uhrzeit", "heatingpump/calculated/time/state", 
     "sensor", "", "", "", "mdi:clock", "", "", "", true},

    // Betriebsart sensor
    {"stiebel_calculated_betriebsart", "Betriebsart", "heatingpump/calculated/betriebsart/state", 
     "sensor", "", "", "", "mdi:cog", "", "", "", true},

    // Delta T continuous
    {"stiebel_calculated_delta_t_continuous", "Delta T WP (kontinuierlich)", "heatingpump/calculated/delta_t_continuous/state", 
     "sensor", "temperature", "K", "measurement", "mdi:thermometer", "", "", "", true},

    // Delta T running (only when compressor active)
    {"stiebel_calculated_delta_t_running", "Delta T WP (nur bei Verdichter an)", "heatingpump/calculated/delta_t_running/state", 
     "sensor", "temperature", "K", "measurement", "mdi:thermometer-chevron-up", "", "", "", true},

    // Compressor active binary sensor
    {"stiebel_calculated_compressor_active", "WP Verdichter aktiv", "heatingpump/calculated/compressor_active/state", 
     "binary_sensor", "running", "", "", "mdi:engine", "on", "off", "", true},

    // CAN bus diagnostic sensors (TWAI / ESP32-S3; silently inactive on MCP2515 builds)
    {"stiebel_calculated_can_tec",        LNAME_CALC_CAN_TEC,        "heatingpump/calculated/can_tec/state",
     "sensor", "", "", "measurement", "mdi:alert-network", "", "", "diagnostic", false},
    {"stiebel_calculated_can_rec",        LNAME_CALC_CAN_REC,        "heatingpump/calculated/can_rec/state",
     "sensor", "", "", "measurement", "mdi:alert-network-outline", "", "", "diagnostic", false},
    {"stiebel_calculated_can_bus_errors", LNAME_CALC_CAN_BUS_ERRORS, "heatingpump/calculated/can_bus_errors/state",
     "sensor", "", "", "total_increasing", "mdi:network-off", "", "", "diagnostic", false},
    {"stiebel_calculated_can_state",      LNAME_CALC_CAN_STATE,      "heatingpump/calculated/can_state/state",
     "sensor", "", "", "", "mdi:can", "", "", "diagnostic", false},
};

static const size_t CALCULATED_SENSOR_COUNT = sizeof(calculatedSensors) / sizeof(CalculatedSensorConfig);

// ============================================================================
// WRITABLE NUMBER CONFIGURATION (for MQTT Number entities)
// ============================================================================

struct WritableNumberConfig {
    const char* signalName;        // CAN signal name like "EINSTELL_SPEICHERSOLLTEMP"
    const char* friendlyName;      // Display name in Home Assistant
    CanMemberType member;          // Which CAN member to write to
    float min;                     // Minimum value
    float max;                     // Maximum value
    float step;                    // Step size
    const char* unit;              // Unit of measurement
    const char* icon;              // MDI icon
    const char* deviceClass;       // HA device class
};

static const WritableNumberConfig writableNumbers[] = {
    // Primary buffer tank target temperature
    {"EINSTELL_SPEICHERSOLLTEMP", "Puffer Soll Temperatur", 
     cm_manager, 20.0, 60.0, 1.0, "°C", "mdi:storage-tank", "temperature"},
    
    // Secondary buffer tank target temperature
    {"EINSTELL_SPEICHERSOLLTEMP2", "Puffer Soll Temperatur 2", 
     cm_manager, 20.0, 60.0, 1.0, "°C", "mdi:storage-tank-outline", "temperature"},
    
    // SG Ready boost temperatures (not real CAN signals, handled internally)
    {"SG_READY_BOOST_STATE3", "SG Ready Boost Zustand 3", 
     cm_manager, 0.0, 10.0, 0.5, "°C", "mdi:thermometer-plus", "temperature"},
    
    {"SG_READY_BOOST_STATE4", "SG Ready Boost Zustand 4", 
     cm_manager, 0.0, 15.0, 0.5, "°C", "mdi:thermometer-chevron-up", "temperature"},
    
    // Room temperature setpoints (controllable from HA)
    {"RAUMSOLLTEMP_I", "Raum Solltemperatur Tag", 
     cm_manager, 15.0, 30.0, 0.5, "°C", "mdi:home-thermometer", "temperature"},
    
    {"RAUMSOLLTEMP_NACHT", "Raum Solltemperatur Nacht", 
     cm_manager, 15.0, 30.0, 0.5, "°C", "mdi:home-thermometer", "temperature"}
};

static const size_t WRITABLE_NUMBER_COUNT = sizeof(writableNumbers) / sizeof(WritableNumberConfig);

// ============================================================================
// WRITABLE SELECT CONFIGURATION (for MQTT Select entities)
// ============================================================================

struct WritableSelectConfig {
    const char* signalName;        // CAN signal name like "PROGRAMMSCHALTER"
    const char* friendlyName;      // Display name in Home Assistant
    CanMemberType member;          // Which CAN member to write to
    const char** options;          // Array of option strings
    size_t optionCount;            // Number of options
    const char* icon;              // MDI icon
};

// Operating mode options for PROGRAMMSCHALTER
static const char* programmschalterOptions[] = {
    LNAME_OPT_NOTBETRIEB,
    LNAME_OPT_BEREITSCHAFT,
    LNAME_OPT_AUTOMATIK,
    LNAME_OPT_TAGBETRIEB,
    LNAME_OPT_ABSENKBETRIEB,
    LNAME_OPT_WARMWASSER,
};

// SG Ready state options (Smart Grid Ready)
static const char* sgReadyOptions[] = {
    LNAME_OPT_SG_EVU_SPERRE,
    LNAME_OPT_SG_NORMAL,
    LNAME_OPT_SG_EMPFOHLEN,
    LNAME_OPT_SG_ZWANG,
};

static const WritableSelectConfig writableSelects[] = {
    // Heat pump operating mode
    {"PROGRAMMSCHALTER", LNAME_SEL_PROGRAMMSCHALTER,
     cm_manager, programmschalterOptions, 6, "mdi:dip-switch"},
    {"SG_READY_STATE", LNAME_SEL_SG_READY,
     cm_manager, sgReadyOptions, 4, "mdi:solar-power"}
};

static const size_t WRITABLE_SELECT_COUNT = sizeof(writableSelects) / sizeof(WritableSelectConfig);

// ============================================================================
// WRITABLE DATETIME CONFIGURATION (for MQTT Datetime entities)
// ============================================================================
// RUNTIME STATE TRACKING
// ============================================================================

// SG Ready state tracking (ressl 2.3.0)
// State, baseline room setpoint and baseline mode live in ESPHome globals
// (sg_state, sg_saved_room, sg_saved_mode in heatpump-include/sg_ready.yaml) and
// survive a reboot, so a boost that is active during a reboot can be undone.
static uint32_t sgLastApplyMs = 0;

// Last operating mode read from the MANAGER (PROGRAMMSCHALTER), "" = not read yet
static std::string hscLastProgrammschalter;

// Track which signals have been discovered
static std::set<std::string> discoveredSignals;

// Track which calculated sensors have been discovered
static std::set<std::string> discoveredCalculatedSensors;

// Track which writable numbers have been discovered
static std::set<std::string> discoveredWritableNumbers;

// Track which writable selects have been discovered
static std::set<std::string> discoveredWritableSelects;

// Track next scheduled request time per unique signal key (MEMBER_SIGNAL)
static std::unordered_map<std::string, unsigned long> nextRequestTime;

// Track starting position for round-robin signal processing (prevents starvation)
static int signalProcessingStartIndex = 0;

// Track sensor values for calculated sensors
static float lastWpVorlaufIst = NAN;
static float lastRuecklaufIstTemp = NAN;
static float lastVerdichterValue = NAN;
static float lastHeissgasTemp = NAN;
static float lastHighPressureBar = NAN;
static uint16_t lastRelayMask = 0;
static bool hasRelayMask = false;

// Official ComfortSoft 4.2.0 CWpmIStatusPage bitmask for SAMMEL_RELAISSTATUS (0x0a20)
static constexpr uint16_t RELAY_BIT_DHC1            = 0x0001u;
static constexpr uint16_t RELAY_BIT_DHC2            = 0x0002u;
static constexpr uint16_t RELAY_MASK_VERDICHTER     = 0x000Cu; // Bits 2 & 3 (AreAllBitsSet(12))
static constexpr uint16_t RELAY_BIT_WW_VENTIL       = 0x0010u;
static constexpr uint16_t RELAY_BIT_HEIZKREISPUMPE  = 0x0020u;
static constexpr uint16_t RELAY_BIT_PUFFERLADEPUMPE = 0x0040u;
static constexpr uint16_t RELAY_BIT_QUELLENPUMPE    = 0x0100u;
static constexpr uint16_t RELAY_BIT_KUEHLEN         = 0x0200u;

inline bool relaisVerdichterOn(uint16_t v)
{
    return (v & RELAY_MASK_VERDICHTER) == RELAY_MASK_VERDICHTER;
}

// ressl 2.9.0: R410A saturation temperature (degC) from gauge pressure (bar).
// Fit to NIST Chemistry WebBook R410A (0..65 degC, max error 0.015 degC), P_abs = P_gauge + 1.01325 bar.
inline float r410aCondensingTempC(float pressureBarGauge)
{
    if (std::isnan(pressureBarGauge) || pressureBarGauge < 0.5f || pressureBarGauge > 48.0f) {
        return NAN;
    }
    double lp = std::log((double)pressureBarGauge + 1.01325);
    double t = -48.038088 + lp * (15.509594 + lp * (3.389605 + lp * 0.128850));
    return static_cast<float>(t);
}

// Track date/time component values
static int lastJahr = -1;
static int lastMonat = -1;
static int lastTag = -1;
static int lastStunde = -1;
static int lastMinute = -1;
static int lastSekunde = -1;

// Scheduled update times for calculated sensors (milliseconds)
static unsigned long nextDeltaTUpdate = 0;
static unsigned long nextCompressorUpdate = 0;
static unsigned long nextDateTimeUpdate = 0;
static unsigned long nextBetriebsartUpdate = 0;
static unsigned long nextCanDiagUpdate = 0;
static unsigned long nextCOPUpdate = 0;
static unsigned long nextLowFlowCheck = 0;
static unsigned long lowFlowSinceMs = 0;

// COP estimation & flow-protection constants for WPF 7 + Wilo Stratos PICO (2.9.0)
// Live Wilo flow (0x08FE) is used whenever BLE is connected; fallback 0.90 m³/h matches 2.5 m setpoint.
static constexpr float FALLBACK_FLOW_RATE_M3H      = 0.90f;   // m³/h fallback if Wilo BLE temporarily offline
static constexpr float COMPRESSOR_POWER_THRESHOLD  = 200.0f;  // W — fallback threshold if CAN relay not yet received
static constexpr float COMPRESSOR_OVERRIDE_POWER_W = 350.0f;  // W — immediate start detection before next 30s relay poll
static constexpr float WATER_FACTOR                = 1.163f;  // kWh/(m³·K) — specific heat of water
static constexpr float WPF7_MIN_FLOW_WARN_M3H      = 0.75f;   // m³/h warn threshold (WPF 7 datasheet min = 0.70 m³/h)
static constexpr float WPF7_MIN_FLOW_CLEAR_M3H     = 0.78f;   // m³/h clear threshold (hysteresis)
static constexpr unsigned long WPF7_LOW_FLOW_DEBOUNCE_MS = 15000UL;

// UID cache to avoid repeated string operations on every signal update
static std::unordered_map<std::string, std::string> uidCache;

// ============================================================================
// SIGNAL REQUEST CONFIGURATION
// ============================================================================

// Signal request configuration structure
typedef struct {
    const char* signalName;
    unsigned long frequency;     // Request frequency in seconds
    CanMemberType member;        // Use cm_other for "all members"
} SignalRequest;

// Forward declarations for the model-specific signal request table.
// The actual definition comes from signal_requests_*.h, included by the
// model's YAML package (e.g. wpl13e.yaml via esphome: includes:).
extern const SignalRequest signalRequests[];
extern const size_t SIGNAL_REQUEST_COUNT_VALUE;
#define SIGNAL_REQUEST_COUNT SIGNAL_REQUEST_COUNT_VALUE

// Runtime state for signal request manager
static bool requestManagerStarted = false;
static unsigned long requestManagerStartTime = 0;

// ============================================================================
// CAN BUS HELPER FUNCTIONS
// ============================================================================

// Only compile function implementations when building with ESPHome framework
// Skip when compiling ha-dummy.cpp or other standalone contexts
#if !defined(HA_DUMMY_BUILD)

/**
 * Simple struct for CAN ID bytes (avoids heap allocation)
 */
struct CanIdBytes {
    uint8_t first;
    uint8_t second;
};

/**
 * Generate CAN read ID from member CAN ID
 */
CanIdBytes generate_read_id(unsigned short can_id)
{
    uint8_t address = (can_id & 0x780) / 8;
    return {static_cast<uint8_t>((address & 0xF0) + 1), static_cast<uint8_t>(can_id & 7)};
}

CanIdBytes generate_write_id(unsigned short can_id)
{
    uint8_t address = (can_id & 0x780) / 8;
    return {static_cast<uint8_t>(address & 0xF0), static_cast<uint8_t>(can_id & 7)};
}

// ----------------------------------------------------------------------------
// ressl 2.3.0: all custom MQTT traffic goes through these wrappers.
// With HA_MQTT_ENTITIES == 0 (default) they do nothing; HA uses the native API.
// ----------------------------------------------------------------------------
inline void hscMqttPublish(const char *topic, const char *payload, size_t len, bool retain)
{
#if HA_MQTT_ENTITIES
    id(mqtt_client).publish(topic, payload, len, 0, retain);
#else
    (void) topic; (void) payload; (void) len; (void) retain;
#endif
}

inline void hscMqttPublish(const std::string &topic, const char *payload, size_t len, bool retain)
{
    hscMqttPublish(topic.c_str(), payload, len, retain);
}

inline void hscMqttPublish(const std::string &topic, const std::string &payload, bool retain = false)
{
    hscMqttPublish(topic.c_str(), payload.c_str(), payload.length(), retain);
}

// millis() wraps after 49.7 days. "now >= next" stalls the scheduler after the
// wrap; the signed difference stays correct as long as intervals are < 24 days.
inline bool hscTimeReached(uint32_t now, uint32_t scheduled)
{
    return static_cast<int32_t>(now - scheduled) >= 0;
}

const CanMember &lookupCanMember(uint32_t canId)
{
    for (size_t i = 0; i < sizeof(CanMembers) / sizeof(CanMember); ++i) {
        if (CanMembers[i].CanId == canId) {
            return CanMembers[i];
        }
    }
    // Return the last element if member is not found
    return CanMembers[sizeof(CanMembers) / sizeof(CanMembers[0]) - 1];
}

// Check if signal is permanently blacklisted (lookup in ElsterTable)
inline bool isPermanentlyBlacklisted(const char* signalName) {
    const ElsterIndex *ei = GetElsterIndex(signalName);
    return ei->isBlacklisted;
}

// Helper: Get type-based defaults for Home Assistant metadata
inline void getTypeDefaults(ElsterType type, const char*& component, const char*& deviceClass, const char*& unit, const char*& stateClass, const char*& icon) {
    switch(type) {
        case et_dec_val:
        case et_cent_val:
            component = "sensor"; deviceClass = "temperature"; unit = "°C"; stateClass = "measurement"; icon = "mdi:thermometer";
            break;
        case et_mil_val:
            component = "sensor"; deviceClass = "duration"; unit = "h"; stateClass = "total_increasing"; icon = "mdi:timer";
            break;
        case et_bool:
        case et_little_bool:
            component = "binary_sensor"; deviceClass = ""; unit = ""; stateClass = ""; icon = "mdi:electric-switch";
            break;
        case et_byte:
        case et_little_endian:
            component = "sensor"; deviceClass = ""; unit = ""; stateClass = "measurement"; icon = "mdi:counter";
            break;
        case et_err_nr:
            component = "sensor"; deviceClass = ""; unit = ""; stateClass = ""; icon = "mdi:alert-circle";
            break;
        case et_dev_id:
        case et_dev_nr:
            component = "text"; deviceClass = ""; unit = ""; stateClass = ""; icon = "mdi:identifier";
            break;
        case et_betriebsart:
            component = "sensor"; deviceClass = "enum"; unit = ""; stateClass = ""; icon = "mdi:hvac";
            break;
        case et_zeit:
            component = "sensor"; deviceClass = "timestamp"; unit = ""; stateClass = ""; icon = "mdi:clock";
            break;
        case et_datum:
            component = "sensor"; deviceClass = "date"; unit = ""; stateClass = ""; icon = "mdi:calendar";
            break;
        default:
            component = "sensor"; deviceClass = ""; unit = ""; stateClass = ""; icon = "mdi:gauge";
            break;
    }
}

const ElsterIndex *processCanMessage(const std::vector<uint8_t> &msg, uint32_t can_id, std::string &signalValue, const CanMember **outCanMember)
{
    // Return if the message is too small
    if (msg.size() < 7)
    {
        return &ElsterTable[0];
    }

    const CanMember &cm = lookupCanMember(can_id);
    *outCanMember = &cm;  // Return CanMember to caller

    const ElsterIndex *ei;
    uint8_t byte1;
    uint8_t byte2;
    char charValue[32];   // ressl 2.3.0: was 16, "not used time domain" needs 21

    if (msg[2] == 0xfa)
    {
        byte1 = msg[5];
        byte2 = msg[6];
        ei = GetElsterIndex(msg[4] + (msg[3] << 8));
    }
    else
    {
        byte1 = msg[3];
        byte2 = msg[4];
        ei = GetElsterIndex(msg[2]);
    }

    switch (ei->Type)
    {
    case et_double_val:
        SetDoubleType(charValue, ei->Type, static_cast<double>(byte2 + (byte1 << 8)));
        break;
    case et_triple_val:
        SetDoubleType(charValue, ei->Type, static_cast<double>(byte2 + (byte1 << 8)));
        break;
    default:
        SetValueType(charValue, ei->Type, static_cast<int>(byte2 + (byte1 << 8)));
        break;
    }

    uint16_t rawIdx = (msg[2] == 0xfa) ? (uint16_t)(msg[4] + (msg[3] << 8)) : (uint16_t)msg[2];
    ESP_LOGI("processCanMessage()", "%s (0x%02x):\t%s:\t%s\t(idx=0x%04x, %s)", cm.Name, (unsigned)can_id, ei->Name, charValue, (unsigned)rawIdx, ElsterTypeStr[ei->Type]);

    signalValue = charValue;
    return ei;
}

void readSignal(const CanMember *cm, const ElsterIndex *ei)
{
    constexpr bool use_extended_id = false; // No use of extended ID
    const uint8_t IndexByte1 = static_cast<uint8_t>(ei->Index >> 8);
    const uint8_t IndexByte2 = static_cast<uint8_t>(ei->Index & 0xFF);
    std::vector<uint8_t> data;
    CanIdBytes readId = generate_read_id(cm->CanId);

    if (IndexByte1 == 0x00)
    {
        data = {readId.first,
                readId.second,
                IndexByte2,
                0x00,
                0x00,
                0x00,
                0x00};
    }
    else
    {
        data = {readId.first,
                readId.second,
                0xFA,
                IndexByte1,
                IndexByte2,
                0x00,
                0x00};
    }

    char logmsg[160];
    snprintf(logmsg, sizeof(logmsg), "READ \"%s\" (0x%04x) FROM %s (0x%02x {0x%02x, 0x%02x}): %02x, %02x, %02x, %02x, %02x, %02x, %02x", ei->Name, ei->Index, cm->Name, (unsigned)cm->CanId, readId.first, readId.second, data[0], data[1], data[2], data[3], data[4], data[5], data[6]);
    ESP_LOGI("readSignal()", "%s", logmsg);

    id(my_can).send_data(CanMembers[cm_pc].CanId, use_extended_id, data);
}

void readSignal(const CanMember *cm, const char *elsterName)
{
    readSignal(cm, GetElsterIndex(elsterName));
    return;
}

void writeSignal(const CanMember *cm, const ElsterIndex *ei, const char *&str)
{
    bool use_extended_id = false;
    int writeValue = TranslateString(str, ei->Type);
    if (writeValue == -1) {
        ESP_LOGW("writeSignal()", "TranslateString failed for \"%s\" (input: \"%s\") — refusing to write", ei->Name, str);
        return;
    }
    uint8_t IndexByte1 = static_cast<uint8_t>(ei->Index >> 8);
    uint8_t IndexByte2 = static_cast<uint8_t>(ei->Index & 0xFF);
    std::vector<uint8_t> data;
    CanIdBytes writeId = generate_write_id(cm->CanId);

    if (IndexByte1 == 0x00)
    {
        data = {writeId.first,
                writeId.second,
                IndexByte2,
                static_cast<uint8_t>(writeValue >> 8),
                static_cast<uint8_t>(writeValue & 0xFF),
                0x00,
                0x00};
    }
    else
    {
        data = {writeId.first,
                writeId.second,
                0xfa,
                IndexByte1,
                IndexByte2,
                static_cast<uint8_t>(writeValue >> 8),
                static_cast<uint8_t>(writeValue & 0xFF)};
    }

    char logmsg[120];
    snprintf(logmsg, sizeof(logmsg), "WRITE \"%s\" (0x%04x): \"%d\" TO: %s (0x%02x {0x%02x, 0x%02x}): %02x, %02x, %02x, %02x, %02x, %02x, %02x",
             ei->Name, ei->Index, writeValue, cm->Name, (unsigned)cm->CanId, writeId.first, writeId.second,
             data[0], data[1], data[2], data[3], data[4], data[5], data[6]);
    ESP_LOGI("writeSignal()", "%s", logmsg);

    id(my_can).send_data(CanMembers[cm_pc].CanId, use_extended_id, data);
}

void writeSignal(const CanMember *cm, const char *elsterName, const char *str)
{
    const char* strCopy = str;  // Create a non-const pointer we can pass by reference
    writeSignal(cm, GetElsterIndex(elsterName), strCopy);
    return;
}

// Unified function to publish MQTT discovery for calculated sensors
void publishCalculatedSensorDiscovery(const CalculatedSensorConfig& config, bool forceRepublish = false) {
    if (!HA_MQTT_ENTITIES) return;   // ressl 2.3.0: native API only
    // Check if already published (unless force republish)
    if (!forceRepublish && discoveredCalculatedSensors.find(config.uniqueId) != discoveredCalculatedSensors.end()) {
        return;
    }
    
    // Mark as discovered
    discoveredCalculatedSensors.insert(config.uniqueId);
    
    // Build discovery topic
    char discoveryTopic[256];
    snprintf(discoveryTopic, sizeof(discoveryTopic), 
             "homeassistant/%s/heatingpump/%s/config", config.component, config.uniqueId);
    
    // Build JSON payload
    std::ostringstream payload;
    payload << "{\"name\":\"" << config.name << "\","
            << "\"unique_id\":\"" << config.uniqueId << "\","
            << "\"state_topic\":\"" << config.stateTopic << "\"";
    
    // Add device class if specified
    if (config.deviceClass[0] != '\0') {
        payload << ",\"device_class\":\"" << config.deviceClass << "\"";
    }
    
    // Add unit if specified
    if (config.unit[0] != '\0') {
        payload << ",\"unit_of_measurement\":\"" << config.unit << "\"";
    }
    
    // Add state class if specified
    if (config.stateClass[0] != '\0') {
        payload << ",\"state_class\":\"" << config.stateClass << "\"";
    }
    
    // Add icon if specified
    if (config.icon[0] != '\0') {
        payload << ",\"icon\":\"" << config.icon << "\"";
    }
    
    // For binary sensors, add payload on/off
    if (config.payloadOn[0] != '\0' && config.payloadOff[0] != '\0') {
        payload << ",\"payload_on\":\"" << config.payloadOn << "\","
                << "\"payload_off\":\"" << config.payloadOff << "\"";
    }

    // Add entity category if specified
    if (config.entityCategory[0] != '\0') {
        payload << ",\"entity_category\":\"" << config.entityCategory << "\"";
    }

    // Add enabled_by_default if false (true is HA default, no need to emit)
    if (!config.enabledByDefault) {
        payload << ",\"enabled_by_default\":false";
    }

    // Add device info
    payload << ",\"device\":{\"identifiers\":[\"stiebel_eltron_wpl13e\"],"
            << "\"name\":\"Stiebel Eltron Wärmepumpe\","
            << "\"manufacturer\":\"Stiebel Eltron\"}}";

    // Publish discovery message
    std::string payloadStr = payload.str();
    hscMqttPublish(discoveryTopic, payloadStr.c_str(), payloadStr.length(), true);

    ESP_LOGI("MQTT", "Discovery published for calculated sensor: %s", config.name);
}

// Publish all calculated sensor discoveries (used during startup and republish)
void publishAllCalculatedSensorDiscoveries(bool forceRepublish = false) {
    if (forceRepublish) {
        discoveredCalculatedSensors.clear();
        ESP_LOGI("MQTT", "Republishing all calculated sensor discoveries");
    }
    
    for (size_t i = 0; i < CALCULATED_SENSOR_COUNT; i++) {
        publishCalculatedSensorDiscovery(calculatedSensors[i], forceRepublish);
    }
}

// ============================================================================
// MQTT WRITABLE NUMBER (for temperature setpoints, etc.)
// ============================================================================

// Publish MQTT discovery for a writable number entity
void publishWritableNumberDiscovery(const WritableNumberConfig& config, bool forceRepublish = false) {
    if (!HA_MQTT_ENTITIES) return;   // ressl 2.3.0: native API only
    // Get CanMember for building topics
    const CanMember* cm = &CanMembers[config.member];
    
    // Build unique ID
    char uniqueId[128];
    snprintf(uniqueId, sizeof(uniqueId), "stiebel_%s_%s", cm->Name, config.signalName);
    std::string uid(uniqueId);
    std::transform(uid.begin(), uid.end(), uid.begin(), ::tolower);
    
    // Check if already published (unless force republish)
    if (!forceRepublish && discoveredWritableNumbers.find(uid) != discoveredWritableNumbers.end()) {
        return;
    }
    
    // Mark as discovered
    discoveredWritableNumbers.insert(uid);
    
    // Build discovery topic: homeassistant/number/heatingpump/<unique_id>/config
    char discoveryTopic[256];
    snprintf(discoveryTopic, sizeof(discoveryTopic), 
             "homeassistant/number/heatingpump/%s/config", uid.c_str());
    
    // Build command topic: heatingpump/<MEMBER>/<SIGNAL>/set
    char commandTopic[128];
    snprintf(commandTopic, sizeof(commandTopic), "heatingpump/%s/%s/set", cm->Name, config.signalName);
    
    // Build state topic: heatingpump/<MEMBER>/<SIGNAL>/state
    char stateTopic[128];
    snprintf(stateTopic, sizeof(stateTopic), "heatingpump/%s/%s/state", cm->Name, config.signalName);
    
    // Build JSON payload
    std::ostringstream payload;
    payload << "{\"name\":\"" << config.friendlyName << "\","
            << "\"unique_id\":\"" << uid << "\","
            << "\"command_topic\":\"" << commandTopic << "\","
            << "\"state_topic\":\"" << stateTopic << "\","
            << "\"min\":" << config.min << ","
            << "\"max\":" << config.max << ","
            << "\"step\":" << config.step << ","
            << "\"mode\":\"box\","
            << "\"unit_of_measurement\":\"" << config.unit << "\","
            << "\"device_class\":\"" << config.deviceClass << "\","
            << "\"icon\":\"" << config.icon << "\",";
    
    // Add device info - use same device ID format as sensors (uppercase member name)
    char canMemberDeviceId[64];
    snprintf(canMemberDeviceId, sizeof(canMemberDeviceId), "stiebel_%s", cm->Name);
    
    const char* canMemberFriendlyName = cm->Name;
    if (strcmp(cm->Name, "KESSEL") == 0) canMemberFriendlyName = "Kessel";
    else if (strcmp(cm->Name, "MANAGER") == 0) canMemberFriendlyName = "Manager";
    else if (strcmp(cm->Name, "HEIZMODUL") == 0) canMemberFriendlyName = "Heizmodul";
    
    payload << "\"device\":{\"identifiers\":[\"" << canMemberDeviceId << "\"],"
            << "\"name\":\"" << canMemberFriendlyName << "\","
            << "\"via_device\":\"stiebel_eltron_wpl13e\","
            << "\"manufacturer\":\"Stiebel Eltron\"}}";
    
    // Publish discovery message
    std::string payloadStr = payload.str();
    hscMqttPublish(discoveryTopic, payloadStr.c_str(), payloadStr.length(), true);
    
    ESP_LOGI("MQTT", "Discovery published for writable number: %s", config.friendlyName);
}

// Publish all writable number discoveries
void publishAllWritableNumberDiscoveries(bool forceRepublish = false) {
    if (forceRepublish) {
        discoveredWritableNumbers.clear();
        ESP_LOGI("MQTT", "Republishing all writable number discoveries");
    }
    
    ESP_LOGI("MQTT_DISC", "Publishing %d writable number discoveries", WRITABLE_NUMBER_COUNT);
    for (size_t i = 0; i < WRITABLE_NUMBER_COUNT; i++) {
        ESP_LOGI("MQTT_DISC", "Publishing writable number %d: %s", i, writableNumbers[i].signalName);
        publishWritableNumberDiscovery(writableNumbers[i], forceRepublish);
    }
    ESP_LOGI("MQTT_DISC", "Completed publishing writable number discoveries");
}

// ============================================================================
// WRITABLE SELECT DISCOVERY (MQTT Select entities)
// ============================================================================

void publishWritableSelectDiscovery(const WritableSelectConfig& config, bool forceRepublish = false) {
    if (!HA_MQTT_ENTITIES) return;   // ressl 2.3.0: native API only
    // Get CAN member
    const CanMember* cm = &CanMembers[config.member];
    
    // Build unique ID (lowercase with underscores)
    char uniqueId[128];
    snprintf(uniqueId, sizeof(uniqueId), "stiebel_%s_%s", cm->Name, config.signalName);
    std::string uniqueIdStr(uniqueId);
    std::transform(uniqueIdStr.begin(), uniqueIdStr.end(), uniqueIdStr.begin(), ::tolower);
    
    // Check if already published (unless forcing)
    if (!forceRepublish && discoveredWritableSelects.count(uniqueIdStr) > 0) {
        return;
    }
    
    // Build discovery topic
    std::ostringstream discoveryTopicStream;
    discoveryTopicStream << "homeassistant/select/heatingpump/" << uniqueIdStr << "/config";
    std::string discoveryTopic = discoveryTopicStream.str();
    
    // Build command and state topics
    std::ostringstream commandTopicStream;
    commandTopicStream << "heatingpump/" << cm->Name << "/" << config.signalName << "/set";
    std::string commandTopic = commandTopicStream.str();
    
    std::ostringstream stateTopicStream;
    stateTopicStream << "heatingpump/" << cm->Name << "/" << config.signalName << "/state";
    std::string stateTopic = stateTopicStream.str();
    
    // Build JSON payload
    std::ostringstream payload;
    payload << "{\"name\":\"" << config.friendlyName << "\","
            << "\"unique_id\":\"" << uniqueIdStr << "\","
            << "\"command_topic\":\"" << commandTopic << "\","
            << "\"state_topic\":\"" << stateTopic << "\","
            << "\"options\":[";
    
    // Add options array
    for (size_t i = 0; i < config.optionCount; i++) {
        if (i > 0) payload << ",";
        payload << "\"" << config.options[i] << "\"";
    }
    payload << "],";
    
    // Add icon if specified
    if (config.icon && strlen(config.icon) > 0) {
        payload << "\"icon\":\"" << config.icon << "\",";
    }
    
    // Add device info - use same device ID format as sensors (uppercase member name)
    char canMemberDeviceId[64];
    snprintf(canMemberDeviceId, sizeof(canMemberDeviceId), "stiebel_%s", cm->Name);
    
    const char* canMemberFriendlyName = cm->Name;
    if (strcmp(cm->Name, "KESSEL") == 0) canMemberFriendlyName = "Kessel";
    else if (strcmp(cm->Name, "MANAGER") == 0) canMemberFriendlyName = "Manager";
    else if (strcmp(cm->Name, "HEIZMODUL") == 0) canMemberFriendlyName = "Heizmodul";
    
    payload << "\"device\":{\"identifiers\":[\"" << canMemberDeviceId << "\"],"
            << "\"name\":\"" << canMemberFriendlyName << "\","
            << "\"via_device\":\"stiebel_eltron_wpl13e\","
            << "\"manufacturer\":\"Stiebel Eltron\"}}";
    
    // Publish discovery message
    std::string payloadStr = payload.str();
    hscMqttPublish(discoveryTopic, payloadStr.c_str(), payloadStr.length(), true);
    
    // Mark as discovered
    discoveredWritableSelects.insert(uniqueIdStr);
    
    ESP_LOGI("MQTT", "Discovery published for writable select: %s", config.friendlyName);
}

// Publish all writable select discoveries
void publishAllWritableSelectDiscoveries(bool forceRepublish = false) {
    if (forceRepublish) {
        discoveredWritableSelects.clear();
        ESP_LOGI("MQTT", "Republishing all writable select discoveries");
    }
    
    ESP_LOGI("MQTT_DISC", "Publishing %d writable select discoveries", WRITABLE_SELECT_COUNT);
    for (size_t i = 0; i < WRITABLE_SELECT_COUNT; i++) {
        ESP_LOGI("MQTT_DISC", "Publishing writable select %d: %s", i, writableSelects[i].signalName);
        publishWritableSelectDiscovery(writableSelects[i], forceRepublish);
    }
    ESP_LOGI("MQTT_DISC", "Completed publishing writable select discoveries");
}

// ============================================================================
// Note: Datetime controls are now implemented using Home Assistant input_datetime
// helpers synced via text_sensor platform and triggered by buttons.
// MQTT datetime discovery is not supported by Home Assistant.
// ============================================================================

void publishDate()
{
    // Publish discovery (only once - cached)
    publishCalculatedSensorDiscovery(calculatedSensors[0]);
    
    // Validate that all values have been received
    if (lastJahr < 0 || lastMonat < 0 || lastTag < 0) {
        ESP_LOGW("CALC", "Cannot publish date: sensors not initialized (Jahr=%d, Monat=%d, Tag=%d)", 
                 lastJahr, lastMonat, lastTag);
        return;
    }
    
    int ijahr = lastJahr;
    int imonat = lastMonat;
    int itag = lastTag;
    
    // Log raw values for debugging
    // ESP_LOGD("CALC", "Date values: Jahr=%d, Monat=%d, Tag=%d", ijahr, imonat, itag);
    
    // Validate ranges
    if (ijahr < 0 || ijahr > 99 || imonat < 1 || imonat > 12 || itag < 1 || itag > 31) {
        ESP_LOGW("CALC", "Date values out of range: Jahr=%d, Monat=%d, Tag=%d", ijahr, imonat, itag);
        return;
    }
    
    // Format date components
    std::string jahr = (ijahr < 10) ? "0" + std::to_string(ijahr) : std::to_string(ijahr);
    std::string monat = (imonat < 10) ? "0" + std::to_string(imonat) : std::to_string(imonat);
    std::string tag = (itag < 10) ? "0" + std::to_string(itag) : std::to_string(itag);
    
    std::string datumStr = "20" + jahr + "-" + monat + "-" + tag;
    
    // Publish state to MQTT
    const char* stateTopic = "heatingpump/calculated/date/state";
    hscMqttPublish(stateTopic, datumStr.c_str(), datumStr.length(), true);
    
    // Update native ESPHome sensor
    if (id(datum).state != datumStr) {
        id(datum).publish_state(datumStr);
    }
    
    ESP_LOGI("CALC", "Published date: %s (Jahr=%d, Monat=%d, Tag=%d)", datumStr.c_str(), ijahr, imonat, itag);
}

void publishTime()
{
    // Publish discovery (only once - cached)
    publishCalculatedSensorDiscovery(calculatedSensors[1]);
    
    // Validate that all values have been received
    if (lastStunde < 0 || lastMinute < 0 || lastSekunde < 0) {
        ESP_LOGW("CALC", "Cannot publish time: sensors not initialized (Stunde=%d, Minute=%d, Sekunde=%d)", 
                 lastStunde, lastMinute, lastSekunde);
        return;
    }
    
    int istunde = lastStunde;
    int iminute = lastMinute;
    int isekunde = lastSekunde;
    
    // Log raw values for debugging
    // ESP_LOGD("CALC", "Time values: Stunde=%d, Minute=%d, Sekunde=%d", istunde, iminute, isekunde);
    
    // Validate ranges
    if (istunde < 0 || istunde > 23 || iminute < 0 || iminute > 59 || isekunde < 0 || isekunde > 59) {
        ESP_LOGW("CALC", "Time values out of range: Stunde=%d, Minute=%d, Sekunde=%d", istunde, iminute, isekunde);
        return;
    }
    
    // Format time directly without helper function
    char zeit[9];
    snprintf(zeit, sizeof(zeit), "%02d:%02d:%02d", istunde, iminute, isekunde);
    
    // Publish state to MQTT
    const char* stateTopic = "heatingpump/calculated/time/state";
    hscMqttPublish(stateTopic, zeit, strlen(zeit), true);
    
    // Update native ESPHome sensor
    std::string zeitStr(zeit);
    if (id(uhrzeit).state != zeitStr) {
        id(uhrzeit).publish_state(zeitStr);
    }
    
    ESP_LOGI("CALC", "Published time: %s (Stunde=%d, Minute=%d, Sekunde=%d)", zeit, istunde, iminute, isekunde);
}

// Datetime states are now handled by Home Assistant input_datetime helpers
// Read by ESPHome text_sensors and written via button triggers

void publishBetriebsart(const std::string& sommerBetriebValue)
{
    // Publish discovery (only once - cached)
    publishCalculatedSensorDiscovery(calculatedSensors[2]);
    
    // Determine Betriebsart based on SOMMERBETRIEB value
    // SOMMERBETRIEB is et_little_bool type, so value is "on" or "off"
    std::string betriebsartStr;
    std::string icon;
    
    if (sommerBetriebValue == "on") {
        betriebsartStr = "Sommerbetrieb";
        icon = "mdi:white-balance-sunny";
    } else {
        betriebsartStr = "Normalbetrieb";
        icon = "mdi:circle-outline";
    }
    
    // Publish state to MQTT
    const char* stateTopic = "heatingpump/calculated/betriebsart/state";
    hscMqttPublish(stateTopic, betriebsartStr.c_str(), betriebsartStr.length(), true);
    
    // ressl 2.3.0: native "Betriebsart" entity removed (SOMMERBETRIEB is not polled)
    
    // ESP_LOGD("CALC", "Published Betriebsart: %s (SOMMERBETRIEB=%s)", betriebsartStr.c_str(), sommerBetriebValue.c_str());
}

void publishDeltaTContinuous()
{
    // Publish discovery (only once - cached)
    publishCalculatedSensorDiscovery(calculatedSensors[3]);
    
    // Check if both temperature values are valid
    if (std::isnan(lastWpVorlaufIst) || std::isnan(lastRuecklaufIstTemp)) {
        return; // Silently skip if values not ready
    }
    
    // Validate temperature range (sanity check)
    if (lastWpVorlaufIst < -50 || lastRuecklaufIstTemp < -50) {
        return; // Silently skip invalid ranges
    }
    
    float deltaT = lastWpVorlaufIst - lastRuecklaufIstTemp;
    
    // Publish state to MQTT
    char value[16];
    snprintf(value, sizeof(value), "%.2f", deltaT);
    const char* stateTopic = "heatingpump/calculated/delta_t_continuous/state";
    hscMqttPublish(stateTopic, value, strlen(value), true);
}

void publishDeltaTRunning()
{
    // Publish discovery (only once - cached)
    publishCalculatedSensorDiscovery(calculatedSensors[4]);
    
    // Check if compressor is running (value > 2 or not NaN and not 0)
    bool compressorRunning = (!std::isnan(lastVerdichterValue) && lastVerdichterValue > 2.0);
    
    if (!compressorRunning) {
        return; // Silently skip if compressor not running
    }
    
    // Check if both temperature values are valid
    if (std::isnan(lastWpVorlaufIst) || std::isnan(lastRuecklaufIstTemp)) {
        return; // Silently skip if values not ready
    }
    
    // Validate temperature range (sanity check)
    if (lastWpVorlaufIst < -50 || lastRuecklaufIstTemp < -50) {
        return; // Silently skip invalid ranges
    }
    
    float deltaT = lastWpVorlaufIst - lastRuecklaufIstTemp;
    
    // Publish state to MQTT
    char value[16];
    snprintf(value, sizeof(value), "%.2f", deltaT);
    const char* stateTopic = "heatingpump/calculated/delta_t_running/state";
    hscMqttPublish(stateTopic, value, strlen(value), true);
}

void publishCompressorActive()
{
    // Publish discovery (only once - cached)
    publishCalculatedSensorDiscovery(calculatedSensors[5]);
    
    // Check if compressor value is valid
    if (std::isnan(lastVerdichterValue)) {
        return; // Silently skip if value invalid
    }
    
    // Compressor is active if value > 2
    bool isActive = (lastVerdichterValue > 2.0);
    
    // Publish state to MQTT
    const char* state = isActive ? "on" : "off";
    const char* stateTopic = "heatingpump/calculated/compressor_active/state";
    hscMqttPublish(stateTopic, state, strlen(state), true);
    // Note: Delta T running is published separately by scheduler
}

// Helper: Check if pattern appears anywhere in text (case-insensitive substring match)
// Get or create cached UID for a signal (eliminates repeated string ops)
// Helper: determine live compressor state from CAN relay 0x0a20 (preferred) + Shelly fallback
inline bool isWpf7CompressorRunning(float shellyPowerW)
{
    bool relayOn = hasRelayMask && relaisVerdichterOn(lastRelayMask);
    bool shellyHigh = (!std::isnan(shellyPowerW) && shellyPowerW > COMPRESSOR_OVERRIDE_POWER_W);
    bool shellyFallback = (!std::isnan(shellyPowerW) && shellyPowerW > COMPRESSOR_POWER_THRESHOLD);
    return hasRelayMask ? (relayOn || shellyHigh) : shellyFallback;
}

// Publish R410A condenser approach (T_kond - VL) & hot-gas superheat (HG - T_kond) during compressor runs
void publishRefrigerantDiagnostics(bool compressorOn)
{
    if (!compressorOn || std::isnan(lastHighPressureBar)) {
        if (!id(verfluessiger_graedigkeit).has_state() || !std::isnan(id(verfluessiger_graedigkeit).state)) {
            id(verfluessiger_graedigkeit).publish_state(NAN);
        }
        if (!id(heissgas_ueberhitzung).has_state() || !std::isnan(id(heissgas_ueberhitzung).state)) {
            id(heissgas_ueberhitzung).publish_state(NAN);
        }
        return;
    }
    float tKond = r410aCondensingTempC(lastHighPressureBar);
    if (std::isnan(tKond)) return;

    if (!std::isnan(lastWpVorlaufIst) && lastWpVorlaufIst > 5.0f) {
        float approach = tKond - lastWpVorlaufIst;
        if (!id(verfluessiger_graedigkeit).has_state() ||
            std::isnan(id(verfluessiger_graedigkeit).state) ||
            fabsf(id(verfluessiger_graedigkeit).state - approach) > 0.05f) {
            id(verfluessiger_graedigkeit).publish_state(approach);
        }
    }
    if (!std::isnan(lastHeissgasTemp) && lastHeissgasTemp > 5.0f) {
        float superheat = lastHeissgasTemp - tKond;
        if (!id(heissgas_ueberhitzung).has_state() ||
            std::isnan(id(heissgas_ueberhitzung).state) ||
            fabsf(id(heissgas_ueberhitzung).state - superheat) > 0.05f) {
            id(heissgas_ueberhitzung).publish_state(superheat);
        }
    }
}

// Debounced minimum-flow protection check for WPF 7 (datasheet min warm side = 0.70 m³/h)
void checkLowFlowGuard()
{
    float power = id(shelly_active_power).state;
    bool compressorOn = isWpf7CompressorRunning(power);
    bool quellenpumpeOn = hasRelayMask && ((lastRelayMask & RELAY_BIT_QUELLENPUMPE) != 0);
    bool hpDemandingFlow = compressorOn || quellenpumpeOn;

    bool wiloConnected = id(wilo_connected_bs).has_state() && id(wilo_connected_bs).state;
    float wiloFlow = id(wilo_flow_rate).has_state() ? id(wilo_flow_rate).state : NAN;

    bool alarmState = id(wp_durchfluss_zu_niedrig).has_state() && id(wp_durchfluss_zu_niedrig).state;
    unsigned long now = millis();

    if (!hpDemandingFlow || !wiloConnected || std::isnan(wiloFlow)) {
        lowFlowSinceMs = 0;
        if (!id(wp_durchfluss_zu_niedrig).has_state() || alarmState) {
            id(wp_durchfluss_zu_niedrig).publish_state(false);
        }
        return;
    }

    if (wiloFlow < WPF7_MIN_FLOW_WARN_M3H) {
        if (lowFlowSinceMs == 0) {
            lowFlowSinceMs = now;
        }
        if ((unsigned long)(now - lowFlowSinceMs) >= WPF7_LOW_FLOW_DEBOUNCE_MS) {
            if (!id(wp_durchfluss_zu_niedrig).has_state() || !alarmState) {
                ESP_LOGW("WPF7_FLOW", "Low heating flow %.2f m³/h (< %.2f m³/h) while HP active!", wiloFlow, WPF7_MIN_FLOW_WARN_M3H);
                id(wp_durchfluss_zu_niedrig).publish_state(true);
            }
        }
    } else if (wiloFlow >= WPF7_MIN_FLOW_CLEAR_M3H) {
        lowFlowSinceMs = 0;
        if (!id(wp_durchfluss_zu_niedrig).has_state() || alarmState) {
            id(wp_durchfluss_zu_niedrig).publish_state(false);
        }
    }
}

void publishCOPEstimate()
{
    float power = id(shelly_active_power).state;
    float vorlauf = lastWpVorlaufIst;
    float ruecklauf = lastRuecklaufIstTemp;

    // Publish electrical power to native sensor
    if (!std::isnan(power)) {
        id(electrical_power_display).publish_state(power);
    }

    // Power breakdown: Shelly total vs Wilo Stratos PICO
    bool wiloConnected = id(wilo_connected_bs).has_state() && id(wilo_connected_bs).state;
    float wiloPower = (wiloConnected && id(wilo_power_sensor).has_state()) ? id(wilo_power_sensor).state : NAN;
    if (!std::isnan(power) && !std::isnan(wiloPower)) {
        float wpOnlyW = power - wiloPower;
        if (wpOnlyW < 0.0f) wpOnlyW = 0.0f;
        id(wp_power_without_wilo).publish_state(wpOnlyW);
        if (power >= 5.0f) {
            float sharePct = (wiloPower / power) * 100.0f;
            if (sharePct > 100.0f) sharePct = 100.0f;
            if (sharePct < 0.0f) sharePct = 0.0f;
            id(wilo_power_share).publish_state(sharePct);
        } else {
            id(wilo_power_share).publish_state(NAN);
        }
    }

    // Live volume flow from Wilo Stratos PICO (0x08FE) with safe fallback
    float flowM3h = FALLBACK_FLOW_RATE_M3H;
    bool usingWiloFlow = false;
    if (wiloConnected && id(wilo_flow_rate).has_state() &&
        !std::isnan(id(wilo_flow_rate).state) && id(wilo_flow_rate).state >= 0.05f) {
        flowM3h = id(wilo_flow_rate).state;
        usingWiloFlow = true;
    }
    const char *srcTxt = usingWiloFlow ? "Wilo Live" : "Fallback (0.90 m³/h)";
    if (!id(cop_flow_source).has_state() || id(cop_flow_source).state != srcTxt) {
        id(cop_flow_source).publish_state(srcTxt);
    }

    // Detect compressor running via CAN relay 0x0a20 (preferred) + Shelly fallback
    bool compressorOn = isWpf7CompressorRunning(power);
    id(compressor_running).publish_state(compressorOn ? 1.0f : 0.0f);
    publishRefrigerantDiagnostics(compressorOn);

    if (!compressorOn || std::isnan(vorlauf) || std::isnan(ruecklauf) || std::isnan(power)) {
        // Compressor off or invalid values:
        // - estimated_thermal_power / estimated_cop publish NAN (keeps existing dashboard text)
        // - thermal_power_live publishes 0.0 kW so HA Riemann energy integration is exact
        id(estimated_thermal_power).publish_state(NAN);
        id(estimated_cop).publish_state(NAN);
        id(thermal_power_live).publish_state(0.0f);
        return;
    }

    float deltaT = vorlauf - ruecklauf;
    if (deltaT < 0.5f) {
        id(estimated_thermal_power).publish_state(0.0f);
        id(thermal_power_live).publish_state(0.0f);
        id(estimated_cop).publish_state(NAN);
        return;
    }

    // Q_thermal = flow_rate (m³/h) × 1.163 (kWh/m³·K) × ΔT (K) → kW
    float thermalPower = flowM3h * WATER_FACTOR * deltaT;
    id(estimated_thermal_power).publish_state(thermalPower);
    id(thermal_power_live).publish_state(thermalPower);

    // System COP = thermal output / Shelly total electrical input (incl. compressor, brine & Wilo pump)
    float electricalKW = power / 1000.0f;
    float validCop = NAN;
    if (electricalKW > 0.1f) {
        float cop = thermalPower / electricalKW;
        if (cop >= 1.0f && cop <= 10.0f) {
            validCop = cop;
            id(estimated_cop).publish_state(cop);
            ESP_LOGI("COP", "Power=%.0fW (Wilo=%.1fW), Flow=%.2f m³/h (%s), ΔT=%.1fK, Q=%.2fkW, COP=%.2f",
                     power, std::isnan(wiloPower) ? -1.0f : wiloPower, flowM3h,
                     usingWiloFlow ? "Wilo" : "Fallback", deltaT, thermalPower, cop);
        } else {
            id(estimated_cop).publish_state(NAN);
        }
    } else {
        id(estimated_cop).publish_state(NAN);
    }

    // Optional MQTT mirror (no-op when HA_MQTT_ENTITIES == 0)
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", thermalPower);
    hscMqttPublish("heatingpump/calculated/thermal_power/state", std::string(buf));
    if (!std::isnan(validCop)) {
        snprintf(buf, sizeof(buf), "%.2f", validCop);
        hscMqttPublish("heatingpump/calculated/cop/state", std::string(buf));
    }
    snprintf(buf, sizeof(buf), "%.0f", power);
    hscMqttPublish("heatingpump/calculated/electrical_power/state", std::string(buf));
    hscMqttPublish("heatingpump/calculated/compressor_running/state", std::string(compressorOn ? "on" : "off"));
}

inline std::string getOrCreateUID(const CanMember &cm, const char* signalName) {
    // Create cache key
    char cacheKey[256];
    snprintf(cacheKey, sizeof(cacheKey), "%u:%s", (unsigned)cm.CanId, signalName);
    
    // Check cache first
    auto it = uidCache.find(cacheKey);
    if (it != uidCache.end()) {
        return it->second;
    }
    
    // Generate UID if not cached (no need to lookup CanMember - already have it)
    char uid[128];
    snprintf(uid, sizeof(uid), "stiebel_%s_%s", cm.Name, signalName);
    std::string result(uid);
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    std::replace(result.begin(), result.end(), ' ', '_');
    
    // Store in cache
    uidCache[cacheKey] = result;
    return result;
}

// Publish MQTT Discovery config for a signal
void publishMqttDiscovery(const CanMember &cm, const ElsterIndex *ei) {
    if (!HA_MQTT_ENTITIES) return;   // ressl 2.3.0: native API only
    
    // Get cached UID (avoids repeated string operations)
    std::string uid = getOrCreateUID(cm, ei->Name);
    
    // Note: Caller is responsible for checking discoveredSignals and inserting uid
    // This function just publishes the MQTT discovery message
    
    // Get friendly name: use ei->friendlyName or fallback to ei->Name
    const char* friendlyName = (ei->hasMetadata && ei->friendlyName) ? ei->friendlyName : ei->Name;
    
    // Get metadata: use ei fields if available, otherwise fall back to type defaults
    const char *component, *deviceClass, *unit, *stateClass, *icon;
    const char *payloadOn = nullptr, *payloadOff = nullptr;
    
    if (ei->hasMetadata && ei->haComponent) {
        // Use metadata from ElsterTable
        component = ei->haComponent;
        deviceClass = ei->haDeviceClass ? ei->haDeviceClass : "";
        unit = ei->unit ? ei->unit : "";
        stateClass = ei->stateClass ? ei->stateClass : "";
        icon = ei->icon ? ei->icon : "";
        payloadOn = ei->payloadOn;
        payloadOff = ei->payloadOff;
    } else {
        // Fall back to type-based defaults
        getTypeDefaults((ElsterType)ei->Type, component, deviceClass, unit, stateClass, icon);
        // For binary sensors, set default payloads
        if (strcmp(component, "binary_sensor") == 0) {
            payloadOn = "on";
            payloadOff = "off";
        }
    }
    
    // Build discovery topic
    char discoveryTopic[256];
    snprintf(discoveryTopic, sizeof(discoveryTopic), 
             "homeassistant/%s/heatingpump/%s/config", component, uid.c_str());
    
    // Build state topic
    char stateTopic[128];
    snprintf(stateTopic, sizeof(stateTopic), "heatingpump/%s/%s/state", cm.Name, ei->Name);
    
    // Build JSON payload efficiently using ostringstream
    std::ostringstream payload;
    payload << "{\"name\":\"" << friendlyName << "\","
            << "\"unique_id\":\"" << uid << "\","
            << "\"state_topic\":\"" << stateTopic << "\","
            << "\"availability_topic\":\"heatingpump/status\"";
    
    // For binary sensors, specify payload values
    if (strcmp(component, "binary_sensor") == 0 && payloadOn && payloadOff) {
        payload << ",\"payload_on\":\"" << payloadOn << "\","
                << "\"payload_off\":\"" << payloadOff << "\"";
    }
    
    // Add optional fields only if non-empty
    if (deviceClass[0] != '\0') {
        payload << ",\"device_class\":\"" << deviceClass << "\"";
    }
    if (unit[0] != '\0') {
        payload << ",\"unit_of_measurement\":\"" << unit << "\"";
    }
    // State class only for numeric sensors
    if (stateClass[0] != '\0') {
        bool isNumericType = (ei->Type == et_dec_val || 
                              ei->Type == et_cent_val || 
                              ei->Type == et_mil_val || 
                              ei->Type == et_byte ||
                              ei->Type == et_double_val ||
                              ei->Type == et_triple_val ||
                              ei->Type == et_little_endian);
        if (isNumericType) {
            payload << ",\"state_class\":\"" << stateClass << "\"";
        }
    }
    if (icon[0] != '\0') {
        payload << ",\"icon\":\"" << icon << "\"";
    }
    
    // Device info - create individual device per CAN member as sub-device
    // Main device ID for the heat pump
    const char* mainDeviceId = "stiebel_eltron_wpl13e";
    
    // Create unique device ID for this CAN member
    char canMemberDeviceId[64];
    snprintf(canMemberDeviceId, sizeof(canMemberDeviceId), "stiebel_%s", cm.Name);
    
    // Convert CAN member name to friendly German name
    const char* canMemberFriendlyName = cm.Name;
    if (strcmp(cm.Name, "KESSEL") == 0) canMemberFriendlyName = "Kessel";
    else if (strcmp(cm.Name, "MANAGER") == 0) canMemberFriendlyName = "Manager";
    else if (strcmp(cm.Name, "HEIZMODUL") == 0) canMemberFriendlyName = "Heizmodul";
    else if (strcmp(cm.Name, "FEHLERSPEICHER") == 0) canMemberFriendlyName = "Fehlerspeicher";
    else if (strcmp(cm.Name, "MIXER1") == 0) canMemberFriendlyName = "Mischer 1";
    else if (strcmp(cm.Name, "MIXER2") == 0) canMemberFriendlyName = "Mischer 2";
    else if (strcmp(cm.Name, "WMZ1") == 0) canMemberFriendlyName = "Wärmemengenzähler 1";
    else if (strcmp(cm.Name, "WMZ2") == 0) canMemberFriendlyName = "Wärmemengenzähler 2";
    
    payload << ",\"device\":{\"identifiers\":[\"" << canMemberDeviceId << "\"],"
            << "\"name\":\"" << canMemberFriendlyName << "\","
            << "\"via_device\":\"" << mainDeviceId << "\","
            << "\"manufacturer\":\"Stiebel Eltron\"}}";
    
    // Publish discovery message with retain flag
    std::string payloadStr = payload.str();
    hscMqttPublish(discoveryTopic, payloadStr.c_str(), payloadStr.length(), true);
    
    ESP_LOGI("MQTT", "Discovery published for %s", friendlyName);
}

// Republish all MQTT discoveries (for periodic refresh)
void republishAllDiscoveries() {
    ESP_LOGI("MQTT", "Republishing all MQTT discoveries (%d signals)", discoveredSignals.size());
    
    // Create a copy of discovered signals to iterate over, filtering out blacklisted signals
    std::set<std::string> signalsToRepublish;
    int blacklistedCount = 0;
    
    for (const auto& uid : discoveredSignals) {
        // Extract signal name from UID (format: "can_id_signalName")
        // Find last underscore to get signal name
        size_t lastUnderscore = uid.find_last_of('_');
        if (lastUnderscore != std::string::npos) {
            std::string signalName = uid.substr(lastUnderscore + 1);
            
            // Skip blacklisted signals
            if (isPermanentlyBlacklisted(signalName.c_str())) {
                blacklistedCount++;
                ESP_LOGD("MQTT", "Skipping blacklisted signal during republish: %s", signalName.c_str());
                continue;
            }
        }
        
        signalsToRepublish.insert(uid);
    }
    
    if (blacklistedCount > 0) {
        ESP_LOGI("MQTT", "Filtered out %d blacklisted signals during republish", blacklistedCount);
    }
    
    // Clear the set and re-add only non-blacklisted signals
    discoveredSignals = signalsToRepublish;
    
    // Force republish by reading all known signals
    // This will trigger processAndUpdate which calls publishMqttDiscovery
    for (const auto& signal : signalsToRepublish) {
        ESP_LOGD("MQTT", "Marked for republish: %s", signal.c_str());
    }
    
    // Republish all calculated sensor discoveries using unified system
    publishAllCalculatedSensorDiscoveries(true);
    
    ESP_LOGI("MQTT", "Discovery refresh complete - will republish as signals are received");
}

// Publish signal state to MQTT
void publishMqttState(const CanMember &cm, const ElsterIndex *ei, const std::string &value) {
    // Input validation
    if (!ei || !ei->Name || value.empty()) {
        ESP_LOGW("MQTT", "Invalid signal data, skipping state publish");
        return;
    }
    
    // Build state topic with bounds checking
    char stateTopic[128];
    int written = snprintf(stateTopic, sizeof(stateTopic), "heatingpump/%s/%s/state", cm.Name, ei->Name);
    if (written >= sizeof(stateTopic)) {
        ESP_LOGW("MQTT", "Topic too long for %s/%s, truncated", cm.Name, ei->Name);
    }
    
    // Publish state with retain flag
    hscMqttPublish(stateTopic, value.c_str(), value.length(), true);
}

// Diagnostics removed for simplification

// Track which COP values we have valid data for
static std::unordered_map<std::string, float> copEnergyValues;

// Publish MQTT discovery for COP sensors
void publishCOPDiscovery() {
    if (!HA_MQTT_ENTITIES) return;   // ressl 2.3.0: native API only
    static bool discoveryPublished = false;
    if (discoveryPublished) return;
    
    // COP WW
    {
        const char* discoveryTopic = "homeassistant/sensor/heatingpump/cop_ww/config";
        std::ostringstream payload;
        payload << "{\"name\":\"" LNAME_COP_WW "\","
                << "\"unique_id\":\"stiebel_cop_ww\","
                << "\"state_topic\":\"heatingpump/calculated/cop_ww/state\","
                << "\"icon\":\"mdi:water-boiler\","
                << "\"state_class\":\"measurement\","
                << "\"device\":{\"identifiers\":[\"stiebel_eltron_wpl13e\"],"
                << "\"name\":\"Stiebel Eltron Wärmepumpe\","
                << "\"manufacturer\":\"Stiebel Eltron\"}}";
        std::string payloadStr = payload.str();
        hscMqttPublish(discoveryTopic, payloadStr.c_str(), payloadStr.length(), true);
    }
    
    // COP Heizung
    {
        const char* discoveryTopic = "homeassistant/sensor/heatingpump/cop_heiz/config";
        std::ostringstream payload;
        payload << "{\"name\":\"" LNAME_COP_HEIZ "\","
                << "\"unique_id\":\"stiebel_cop_heiz\","
                << "\"state_topic\":\"heatingpump/calculated/cop_heiz/state\","
                << "\"icon\":\"mdi:radiator\","
                << "\"state_class\":\"measurement\","
                << "\"device\":{\"identifiers\":[\"stiebel_eltron_wpl13e\"],"
                << "\"name\":\"Stiebel Eltron Wärmepumpe\","
                << "\"manufacturer\":\"Stiebel Eltron\"}}";
        std::string payloadStr = payload.str();
        hscMqttPublish(discoveryTopic, payloadStr.c_str(), payloadStr.length(), true);
    }
    
    // COP Gesamt
    {
        const char* discoveryTopic = "homeassistant/sensor/heatingpump/cop_gesamt/config";
        std::ostringstream payload;
        payload << "{\"name\":\"" LNAME_COP_GESAMT "\","
                << "\"unique_id\":\"stiebel_cop_gesamt\","
                << "\"state_topic\":\"heatingpump/calculated/cop_gesamt/state\","
                << "\"icon\":\"mdi:chart-line\","
                << "\"state_class\":\"measurement\","
                << "\"device\":{\"identifiers\":[\"stiebel_eltron_wpl13e\"],"
                << "\"name\":\"Stiebel Eltron Wärmepumpe\","
                << "\"manufacturer\":\"Stiebel Eltron\"}}";
        std::string payloadStr = payload.str();
        hscMqttPublish(discoveryTopic, payloadStr.c_str(), payloadStr.length(), true);
    }
    
    discoveryPublished = true;
    ESP_LOGI("MQTT", "Discovery published for COP sensors");
}

// Store energy value when received for COP calculation
void storeCOPEnergyValue(const char* signalName, const std::string &value) {
    // Validate the string contains a valid number (no exceptions available)
    if (value.empty()) {
        ESP_LOGW("COP", "Empty value for %s", signalName);
        return;
    }
    
    // Check for valid numeric characters
    bool hasDigit = false;
    bool isValid = true;
    for (size_t i = 0; i < value.length(); i++) {
        char c = value[i];
        if (c >= '0' && c <= '9') {
            hasDigit = true;
        } else if (c != '.' && c != '-' && c != '+' && c != ' ') {
            isValid = false;
            break;
        }
    }
    
    if (!hasDigit || !isValid) {
        ESP_LOGW("COP", "Invalid numeric value for %s: %s", signalName, value.c_str());
        return;
    }
    
    // Parse the value (std::stof may still fail but won't throw in this context)
    float fval = std::atof(value.c_str());
    copEnergyValues[signalName] = fval;
    ESP_LOGD("COP", "Stored %s = %.3f", signalName, fval);
}

// Calculate and publish COP values if all required data is available
void updateCOPCalculations() {
    publishCOPDiscovery();
    
    // COP WW: (WAERMEERTRAG_WW_SUM + WAERMEERTRAG_2WE_WW_SUM) / EL_AUFNAHMELEISTUNG_WW_SUM
    if (copEnergyValues.find("WAERMEERTRAG_WW_SUM_MWH") != copEnergyValues.end() &&
        copEnergyValues.find("WAERMEERTRAG_2WE_WW_SUM_MWH") != copEnergyValues.end() &&
        copEnergyValues.find("EL_AUFNAHMELEISTUNG_WW_SUM_MWH") != copEnergyValues.end()) {
        
        float el_ww = copEnergyValues["EL_AUFNAHMELEISTUNG_WW_SUM_MWH"];
        if (el_ww > 0.001f) { // Avoid division by zero
            float waerme_ww = copEnergyValues["WAERMEERTRAG_WW_SUM_MWH"] + copEnergyValues["WAERMEERTRAG_2WE_WW_SUM_MWH"];
            float cop_ww = waerme_ww / el_ww;
            
            char valueStr[16];
            snprintf(valueStr, sizeof(valueStr), "%.2f", cop_ww);
            const char* stateTopic = "heatingpump/calculated/cop_ww/state";
            hscMqttPublish(stateTopic, valueStr, strlen(valueStr), true);
            ESP_LOGI("COP", "COP WW: %.2f (Wärme: %.3f MWh, El: %.3f MWh)", cop_ww, waerme_ww, el_ww);
        }
    }
    
    // COP Heizung: (WAERMEERTRAG_HEIZ_SUM + WAERMEERTRAG_2WE_HEIZ_SUM) / EL_AUFNAHMELEISTUNG_HEIZ_SUM
    if (copEnergyValues.find("WAERMEERTRAG_HEIZ_SUM_MWH") != copEnergyValues.end() &&
        copEnergyValues.find("WAERMEERTRAG_2WE_HEIZ_SUM_MWH") != copEnergyValues.end() &&
        copEnergyValues.find("EL_AUFNAHMELEISTUNG_HEIZ_SUM_MWH") != copEnergyValues.end()) {
        
        float el_heiz = copEnergyValues["EL_AUFNAHMELEISTUNG_HEIZ_SUM_MWH"];
        if (el_heiz > 0.001f) { // Avoid division by zero
            float waerme_heiz = copEnergyValues["WAERMEERTRAG_HEIZ_SUM_MWH"] + copEnergyValues["WAERMEERTRAG_2WE_HEIZ_SUM_MWH"];
            float cop_heiz = waerme_heiz / el_heiz;
            
            char valueStr[16];
            snprintf(valueStr, sizeof(valueStr), "%.2f", cop_heiz);
            const char* stateTopic = "heatingpump/calculated/cop_heiz/state";
            hscMqttPublish(stateTopic, valueStr, strlen(valueStr), true);
            ESP_LOGI("COP", "COP Heizung: %.2f (Wärme: %.3f MWh, El: %.3f MWh)", cop_heiz, waerme_heiz, el_heiz);
        }
    }
    
    // COP Gesamt: (all WAERMEERTRAG) / (all EL_AUFNAHMELEISTUNG)
    if (copEnergyValues.find("WAERMEERTRAG_HEIZ_SUM_MWH") != copEnergyValues.end() &&
        copEnergyValues.find("WAERMEERTRAG_2WE_HEIZ_SUM_MWH") != copEnergyValues.end() &&
        copEnergyValues.find("WAERMEERTRAG_WW_SUM_MWH") != copEnergyValues.end() &&
        copEnergyValues.find("WAERMEERTRAG_2WE_WW_SUM_MWH") != copEnergyValues.end() &&
        copEnergyValues.find("EL_AUFNAHMELEISTUNG_HEIZ_SUM_MWH") != copEnergyValues.end() &&
        copEnergyValues.find("EL_AUFNAHMELEISTUNG_WW_SUM_MWH") != copEnergyValues.end()) {
        
        float el_total = copEnergyValues["EL_AUFNAHMELEISTUNG_HEIZ_SUM_MWH"] + copEnergyValues["EL_AUFNAHMELEISTUNG_WW_SUM_MWH"];
        if (el_total > 0.001f) { // Avoid division by zero
            float waerme_total = copEnergyValues["WAERMEERTRAG_HEIZ_SUM_MWH"] + copEnergyValues["WAERMEERTRAG_2WE_HEIZ_SUM_MWH"] +
                               copEnergyValues["WAERMEERTRAG_WW_SUM_MWH"] + copEnergyValues["WAERMEERTRAG_2WE_WW_SUM_MWH"];
            float cop_gesamt = waerme_total / el_total;
            
            char valueStr[16];
            snprintf(valueStr, sizeof(valueStr), "%.2f", cop_gesamt);
            const char* stateTopic = "heatingpump/calculated/cop_gesamt/state";
            hscMqttPublish(stateTopic, valueStr, strlen(valueStr), true);
            ESP_LOGI("COP", "COP Gesamt: %.2f (Wärme: %.3f MWh, El: %.3f MWh)", cop_gesamt, waerme_total, el_total);
        }
    }
}

// Validation removed for simplification - all values are published as-is
// Invalid values like -255, -32768, etc. will be visible in Home Assistant
// Users can filter these in HA automations/dashboards if needed

// ============================================================================
// SG READY (ressl 2.3.0) - ROOM-SETPOINT BOOST
// ============================================================================
// The WPF 7 has no DHW tank, so the former boost of EINSTELL_SPEICHERSOLLTEMP
// (the DHW setpoint) had no effect. SG Ready now shifts the heating curve via
// the day room setpoint (RAUMSOLLTEMP_I) and the operating mode:
//   1 - EVU Sperre : mode "Bereitschaft" (frost protection), room setpoint = baseline
//   2 - Normal     : baseline mode and baseline room setpoint restored
//   3 - Empfohlen  : mode "Tagbetrieb", room setpoint = baseline + boost 3
//   4 - Zwang      : mode "Tagbetrieb", room setpoint = baseline + boost 4
// The baseline is read from the WPM i (never assumed) and persisted in globals.
// Writes are skipped when the WPM i already has the value (EEPROM wear).

// Index of an operating mode name in BetriebsartList, -1 if unknown
inline int hscModeIndex(const std::string &name)
{
    for (int i = 0; i <= (int) High(BetriebsartList); i++) {
        if (name == BetriebsartList[i].Name) return i;
    }
    return -1;
}

// Option strings of the native select. They must match heatpump-include/sg_ready.yaml.
// Deliberately not the LNAME_* macros: a language override must not break the select.
static const char *const SG_SELECT_OPTIONS[4] = {"1 - EVU Sperre", "2 - Normal", "3 - Empfohlen", "4 - Zwang"};

// Current SG Ready option for the native select (template lambda)
esphome::optional<std::string> sgReadyCurrentOption()
{
    int s = id(sg_state);
    if (s < 1 || s > 4) return {};
    return std::string(SG_SELECT_OPTIONS[s - 1]);
}

static void sgWriteRoomSetpoint(float t)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", t);
    writeSignal(&CanMembers[cm_manager], "RAUMSOLLTEMP_I", buf);
    readSignal(&CanMembers[cm_manager], "RAUMSOLLTEMP_I");
}

static void sgWriteMode(const char *mode)
{
    writeSignal(&CanMembers[cm_manager], "PROGRAMMSCHALTER", mode);
    readSignal(&CanMembers[cm_manager], "PROGRAMMSCHALTER");
}

// Store the current WPM i values as baseline (only if none is stored yet)
static bool sgEnsureBaseline()
{
    if (id(sg_saved_room) > 0 && id(sg_saved_mode) >= 0) return true;
    float room = id(raumsolltemp_i).state;
    int mode = hscModeIndex(hscLastProgrammschalter);
    if (std::isnan(room) || room < 10.0f || room > 30.0f || mode < 0) {
        ESP_LOGW("SG_READY", "No valid baseline yet (room=%.1f, mode='%s') - request ignored",
                 room, hscLastProgrammschalter.c_str());
        return false;
    }
    if (mode == 0) {  // Notbetrieb: heat pump in fault mode, do not take over
        ESP_LOGW("SG_READY", "WPM i is in Notbetrieb - SG Ready request ignored");
        return false;
    }
    id(sg_saved_room) = (int) lroundf(room * 10.0f);
    id(sg_saved_mode) = mode;
    ESP_LOGI("SG_READY", "Baseline saved: room setpoint %.1f degC, mode %s", room, BetriebsartList[mode].Name);
    return true;
}

static void sgSetRoomIfDifferent(float target)
{
    float cur = id(raumsolltemp_i).state;
    if (std::isnan(cur) || fabsf(cur - target) > 0.05f) sgWriteRoomSetpoint(target);
}

static void sgSetModeIfDifferent(const char *mode)
{
    if (hscLastProgrammschalter != mode) sgWriteMode(mode);
}

static void sgRestoreBaseline()
{
    if (id(sg_saved_room) > 0) sgSetRoomIfDifferent(id(sg_saved_room) / 10.0f);
    int mode = id(sg_saved_mode);
    if (mode >= 0 && mode <= (int) High(BetriebsartList)) sgSetModeIfDifferent(BetriebsartList[mode].Name);
    if (id(sg_saved_room) > 0 || mode >= 0) {
        ESP_LOGI("SG_READY", "Baseline restored (room %.1f degC, mode %s)", id(sg_saved_room) / 10.0f,
                 (mode >= 0 && mode <= (int) High(BetriebsartList)) ? BetriebsartList[mode].Name : "-");
    }
    id(sg_saved_room) = 0;
    id(sg_saved_mode) = -1;
}

static void sgApplyState(int state)
{
    switch (state) {
        case 2:
            sgRestoreBaseline();
            break;
        case 1:
            if (!sgEnsureBaseline()) return;
            sgSetRoomIfDifferent(id(sg_saved_room) / 10.0f);
            sgSetModeIfDifferent("Bereitschaft");
            break;
        case 3:
        case 4: {
            if (!sgEnsureBaseline()) return;
            float boost = (state == 3) ? id(sg_boost_3).state : id(sg_boost_4).state;
            if (std::isnan(boost) || boost < 0.0f) boost = 0.0f;
            float saved = id(sg_saved_room) / 10.0f;
            float target = std::min(saved + boost, std::max(saved, SG_MAX_ROOM_SETPOINT));
            sgSetRoomIfDifferent(target);
            sgSetModeIfDifferent("Tagbetrieb");
            ESP_LOGI("SG_READY", "Boost: room setpoint %.1f + %.1f -> %.1f degC", saved, boost, target);
            break;
        }
        default:
            return;
    }
    id(sg_state) = state;
    ESP_LOGI("SG_READY", "SG Ready state %d (%s) applied", state, SG_SELECT_OPTIONS[state - 1]);
}

// Request received before the first CAN values were read (applied by sgReadyLoop)
static std::string sgPendingOption;

// Entry point for the native select (set_action)
void sgReadyRequest(const std::string &option)
{
    int state = (!option.empty() && option[0] >= '1' && option[0] <= '4') ? option[0] - '0' : 0;
    if (state == 0) {
        ESP_LOGW("SG_READY", "Invalid SG Ready option '%s'", option.c_str());
        return;
    }
    if (state == id(sg_state)) {
        sgPendingOption.clear();
        return;
    }
    if (!requestManagerStarted || hscLastProgrammschalter.empty() || std::isnan(id(raumsolltemp_i).state)) {
        // e.g. right after an ESP reboot: apply as soon as the first values are read
        ESP_LOGW("SG_READY", "WPM i values not read yet - SG Ready request %d queued", state);
        sgPendingOption = option;
        return;
    }
    sgPendingOption.clear();
    uint32_t now = millis();
    if (state != 2 && sgLastApplyMs != 0 && (uint32_t)(now - sgLastApplyMs) < SG_MIN_INTERVAL_MS) {
        ESP_LOGW("SG_READY", "Last SG Ready change %lus ago (minimum %lus) - request %d ignored",
                 (unsigned long) ((now - sgLastApplyMs) / 1000), (unsigned long) (SG_MIN_INTERVAL_MS / 1000), state);
        return;
    }
    sgApplyState(state);
    if (id(sg_state) == state) sgLastApplyMs = now;
}

// Called every second (heatingpump.yaml interval): applies a queued request
void sgReadyLoop()
{
    if (sgPendingOption.empty()) return;
    if (!requestManagerStarted || hscLastProgrammschalter.empty() || std::isnan(id(raumsolltemp_i).state)) return;
    std::string option = sgPendingOption;
    sgPendingOption.clear();
    sgReadyRequest(option);
}

// ROOM TEMPERATURE FEED (2.4.0) removed in 2.5.0: a WPM i uses a room temperature only
// from a registered FEK. Room control runs in Home Assistant via RAUMSOLLTEMP_I/NACHT.

// ============================================================================
// COMPILE-TIME STRING HASHING FOR FAST SIGNAL DISPATCH
// ============================================================================

// Compile-time DJB2 hash function (constexpr for C++11 compatibility)
constexpr uint32_t hash_impl(const char* str, uint32_t hash = 5381) {
    return (*str == '\0') ? hash : hash_impl(str + 1, ((hash << 5) + hash) + static_cast<uint32_t>(*str));
}

constexpr uint32_t hash(const char* str) {
    return hash_impl(str);
}

// Runtime hash for signal names (inline for performance)
inline uint32_t hash_runtime(const char* str) {
    uint32_t h = 5381;
    while (*str) {
        h = ((h << 5) + h) + static_cast<uint32_t>(*str);
        str++;
    }
    return h;
}

// Pre-computed compile-time hashes for all monitored signals
constexpr uint32_t HASH_JAHR = hash("JAHR");
constexpr uint32_t HASH_MONAT = hash("MONAT");
constexpr uint32_t HASH_TAG = hash("TAG");
constexpr uint32_t HASH_STUNDE = hash("STUNDE");
constexpr uint32_t HASH_MINUTE = hash("MINUTE");
constexpr uint32_t HASH_SEKUNDE = hash("SEKUNDE");
constexpr uint32_t HASH_SOMMERBETRIEB = hash("SOMMERBETRIEB");
constexpr uint32_t HASH_WPVORLAUFIST = hash("WPVORLAUFIST");
constexpr uint32_t HASH_RUECKLAUFISTTEMP = hash("RUECKLAUFISTTEMP");
constexpr uint32_t HASH_VERDICHTER = hash("VERDICHTER");
constexpr uint32_t HASH_EL_AUFNAHMELEISTUNG_HEIZ = hash("EL_AUFNAHMELEISTUNG_HEIZ_SUM_MWH");
constexpr uint32_t HASH_EL_AUFNAHMELEISTUNG_WW = hash("EL_AUFNAHMELEISTUNG_WW_SUM_MWH");
constexpr uint32_t HASH_WAERMEERTRAG_2WE_WW = hash("WAERMEERTRAG_2WE_WW_SUM_MWH");
constexpr uint32_t HASH_WAERMEERTRAG_2WE_HEIZ = hash("WAERMEERTRAG_2WE_HEIZ_SUM_MWH");
constexpr uint32_t HASH_WAERMEERTRAG_WW = hash("WAERMEERTRAG_WW_SUM_MWH");
constexpr uint32_t HASH_WAERMEERTRAG_HEIZ = hash("WAERMEERTRAG_HEIZ_SUM_MWH");



// WPF 7 specific signal hashes
constexpr uint32_t HASH_SAMMLERISTTEMP = hash("SAMMLERISTTEMP");
constexpr uint32_t HASH_AUSSENTEMP = hash("AUSSENTEMP");
constexpr uint32_t HASH_QUELLENSOLLTEMPERATUR = hash("QUELLENSOLLTEMPERATUR");
constexpr uint32_t HASH_HEISSGAS_TEMP = hash("HEISSGAS_TEMP");
// ressl 2.5.0: diagnostics (raw values) & 2.9.0 decoded WPM i signals
constexpr uint32_t HASH_QUELLE_IST = hash("QUELLE_IST");
constexpr uint32_t HASH_MASCHINENDRUCK = hash("MASCHINENDRUCK");
constexpr uint32_t HASH_SAMMEL_RELAISSTATUS = hash("SAMMEL_RELAISSTATUS");
constexpr uint32_t HASH_ZWEITER_WE_STATUS = hash("ZWEITER_WE_STATUS");
constexpr uint32_t HASH_LAUFZEIT_WP1 = hash("LAUFZEIT_WP1");
constexpr uint32_t HASH_LAUFZEIT_DHC1 = hash("LAUFZEIT_DHC1");
constexpr uint32_t HASH_LAUFZEIT_DHC2 = hash("LAUFZEIT_DHC2");
constexpr uint32_t HASH_STILLSTANDZEIT_0 = hash("STILLSTANDZEIT_0");
constexpr uint32_t HASH_HEIZKURVE = hash("HEIZKURVE");
constexpr uint32_t HASH_VORLAUFSOLLTEMP = hash("VORLAUFSOLLTEMP");
constexpr uint32_t HASH_PUFFERSOLL = hash("PUFFERSOLL");
constexpr uint32_t HASH_FEHLERMELDUNG = hash("FEHLERMELDUNG");
constexpr uint32_t HASH_EVU_SPERRE_AKTIV = hash("EVU_SPERRE_AKTIV");
constexpr uint32_t HASH_WW_ECO = hash("WW_ECO");
constexpr uint32_t HASH_PROGRAMMSCHALTER = hash("PROGRAMMSCHALTER");
constexpr uint32_t HASH_INITIALISIERUNG = hash("INITIALISIERUNG");
constexpr uint32_t HASH_BRENNER = hash("BRENNER");
constexpr uint32_t HASH_RAUMSOLLTEMP_I = hash("RAUMSOLLTEMP_I");
constexpr uint32_t HASH_RAUMSOLLTEMP_II = hash("RAUMSOLLTEMP_II");
constexpr uint32_t HASH_RAUMSOLLTEMP_III = hash("RAUMSOLLTEMP_III");
constexpr uint32_t HASH_RAUMSOLLTEMP_NACHT = hash("RAUMSOLLTEMP_NACHT");
constexpr uint32_t HASH_EINSTELL_SPEICHERSOLLTEMP = hash("EINSTELL_SPEICHERSOLLTEMP");
constexpr uint32_t HASH_EINSTELL_SPEICHERSOLLTEMP2 = hash("EINSTELL_SPEICHERSOLLTEMP2");

// ============================================================================
// NATIVE ESPHOME SENSOR UPDATE FUNCTION
// ============================================================================

/**
 * Update native ESPHome sensors (appears in HA via ESPHome API)
 * Called from updateSensor() after MQTT publication
 */
// ---- RELAIS & WPMi pure logic begin (host-tested) ----
// ressl 2.5.0: set bits of SAMMEL_RELAISSTATUS (bit 0 = LSB), e.g. "0x016C: 2,3,5,6,8"
std::string relaisBitsText(uint16_t v)
{
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "0x%04X:", (unsigned) v);
    bool first = true;
    for (int b = 0; b < 16; b++) {
        if (!(v & (1u << b))) continue;
        n += snprintf(buf + n, sizeof(buf) - n, first ? " %d" : ",%d", b);
        first = false;
    }
    if (first) snprintf(buf + n, sizeof(buf) - n, " -");
    return std::string(buf);
}

// ressl 2.9.0: human-readable relay status from ComfortSoft 4.2.0 CWpmIStatusPage
std::string relaisKlartext(uint16_t v)
{
    std::string out;
    auto add = [&](const char *label) {
        if (!out.empty()) out += ", ";
        out += label;
    };
    if (relaisVerdichterOn(v))           add("Verdichter");
    if (v & RELAY_BIT_QUELLENPUMPE)      add("Quellenpumpe");
    if (v & RELAY_BIT_PUFFERLADEPUMPE)   add("Pufferladepumpe");
    if (v & RELAY_BIT_HEIZKREISPUMPE)    add("Heizkreispumpe");
    if (v & RELAY_BIT_DHC1)              add("DHC1");
    if (v & RELAY_BIT_DHC2)              add("DHC2");
    if (v & RELAY_BIT_WW_VENTIL)         add("WW-Ventil");
    if (v & RELAY_BIT_KUEHLEN)           add("Kühlen");
    return out.empty() ? std::string("Alle aus") : out;
}

// ressl 2.9.0: Official Stiebel Eltron WPM i error codes (WPC_WPMi manual p. 36 + ElsterTable)
std::string wpmiErrorText(int code)
{
    switch (code) {
        case 0:   return "Keine Störung";
        case 2:   return "E2: Schütz klebt";
        case 3:   return "E3: HD-Sensor";
        case 4:   return "E4: Hochdruck";
        case 5:   return "E5: Verdampferfühler";
        case 6:   return "E6: Relaistreiber";
        case 7:   return "E7: Relaispegel";
        case 8:   return "E8: Hexschalter";
        case 11:  return "E11: Reset Baustein";
        case 12:  return "E12: Niederdruck";
        case 13:  return "E13: ROM";
        case 14:  return "E14: Quellen-Mintemp";
        case 16:  return "E16: Abtauen";
        case 18:  return "E18: Heißgasfühler";
        case 20:  return "E20: Schütz klebt";
        case 21:  return "E21: Keine Leistung";
        case 22:  return "E22: Niederdruck";
        case 23:  return "E23: Hochdruck";
        case 24:  return "E24: HD-Sensor max";
        case 26:  return "E26: Niederdruck";
        case 27:  return "E27: ND-Druck";
        case 28:  return "E28: HD-Druck";
        case 29:  return "E29: HD-Sensor max";
        case 30:  return "E30: Heißgas max";
        case 31:  return "E31: HD-Sensor";
        case 32:  return "E32: Einfrierschutz";
        case 33:  return "E33: Keine Leistung";
        case 70:  return "E70: Fühler Mischervorlauf";
        case 71:  return "E71: Fühler Quellenvorlauf";
        case 72:  return "E72: Fühler WP-Vorlauf";
        case 73:  return "E73: Fühler WP-Rücklauf";
        case 75:  return "E75: Fühler Außentemperatur";
        case 76:  return "E76: Fühler Warmwasser";
        case 80:  return "E80: Fühler Fernbedienung";
        case 130: return "E130: HD-Sensor";
        default: {
            char buf[32];
            snprintf(buf, sizeof(buf), "Störung Code %d", code);
            return std::string(buf);
        }
    }
}
// ---- RELAIS & WPMi pure logic end ----

void updateNativeSensors(const char* signalName, const std::string& value, const std::string& publishValue) {
    uint32_t signalHash = hash_runtime(signalName);
    
    // Parse float value once for numeric sensors
    char* endPtr;
    float floatValue = strtof(value.c_str(), &endPtr);
    bool isValidFloat = (endPtr != value.c_str() && *endPtr == '\0');
    
    switch (signalHash) {
        // Temperature sensors
        case HASH_AUSSENTEMP:
            if (isValidFloat && id(aussentemp).state != floatValue) {
                id(aussentemp).publish_state(floatValue);
            }
            break;
            
        case HASH_RUECKLAUFISTTEMP:
            if (isValidFloat) {
                lastRuecklaufIstTemp = floatValue;
                if (!id(ruecklaufisttemp).has_state() || id(ruecklaufisttemp).state != floatValue) {
                    id(ruecklaufisttemp).publish_state(floatValue);
                }
                if (!std::isnan(lastWpVorlaufIst) && lastWpVorlaufIst > -50.0f && floatValue > -50.0f) {
                    float deltaT = lastWpVorlaufIst - floatValue;
                    if (!id(delta_t).has_state() || fabsf(id(delta_t).state - deltaT) > 0.005f) {
                        id(delta_t).publish_state(deltaT);
                    }
                }
            }
            break;
            
        case HASH_WPVORLAUFIST:
            if (isValidFloat) {
                lastWpVorlaufIst = floatValue;
                if (!id(wpvorlaufist).has_state() || id(wpvorlaufist).state != floatValue) {
                    id(wpvorlaufist).publish_state(floatValue);
                }
                // Calculate and publish Delta T
                if (!std::isnan(lastRuecklaufIstTemp) && lastRuecklaufIstTemp > -50.0f && floatValue > -50.0f) {
                    float deltaT = floatValue - lastRuecklaufIstTemp;
                    if (!id(delta_t).has_state() || fabsf(id(delta_t).state - deltaT) > 0.005f) {
                        id(delta_t).publish_state(deltaT);
                    }
                }
            }
            break;
            
        // SAMMLERISTTEMP removed — no solar thermal connected
        case HASH_SAMMLERISTTEMP:
            break;
            
        case HASH_QUELLENSOLLTEMPERATUR:
            if (isValidFloat && id(quellensolltemperatur).state != floatValue) {
                id(quellensolltemperatur).publish_state(floatValue);
            }
            break;
            
        case HASH_HEISSGAS_TEMP:
            if (isValidFloat) {
                lastHeissgasTemp = floatValue;
                if (!id(heissgas_temp).has_state() || id(heissgas_temp).state != floatValue) {
                    id(heissgas_temp).publish_state(floatValue);
                }
            }
            break;

        // ressl 2.5.0/2.9.0: diagnostics & decoded WPM i sensors
        case HASH_QUELLE_IST:
            if (isValidFloat && id(quelle_ist).state != floatValue) {
                id(quelle_ist).publish_state(floatValue);
            }
            break;

        case HASH_MASCHINENDRUCK:
            if (isValidFloat) {
                if (!id(maschinendruck_raw).has_state() || id(maschinendruck_raw).state != floatValue) {
                    id(maschinendruck_raw).publish_state(floatValue);
                }
                float pressureBar = floatValue / 10.0f;
                lastHighPressureBar = pressureBar;
                if (!id(hochdruck_kaeltekreis).has_state() || fabsf(id(hochdruck_kaeltekreis).state - pressureBar) > 0.05f) {
                    id(hochdruck_kaeltekreis).publish_state(pressureBar);
                }
                float tKond = r410aCondensingTempC(pressureBar);
                if (!std::isnan(tKond) && (!id(kondensationstemp_r410a).has_state() || fabsf(id(kondensationstemp_r410a).state - tKond) > 0.05f)) {
                    id(kondensationstemp_r410a).publish_state(tKond);
                }
            }
            break;

        case HASH_SAMMEL_RELAISSTATUS:
            if (isValidFloat) {
                uint16_t mask = static_cast<uint16_t>(lroundf(floatValue));
                lastRelayMask = mask;
                hasRelayMask = true;
                if (!id(relaisstatus_raw).has_state() || id(relaisstatus_raw).state != floatValue) {
                    id(relaisstatus_raw).publish_state(floatValue);
                    id(relaisstatus_bits).publish_state(relaisBitsText(mask));
                    id(relaisstatus_klartext).publish_state(relaisKlartext(mask));
                }
                auto pubBin = [](esphome::binary_sensor::BinarySensor &bs, bool val) {
                    if (!bs.has_state() || bs.state != val) bs.publish_state(val);
                };
                pubBin(id(relais_verdichter),      relaisVerdichterOn(mask));
                pubBin(id(relais_quellenpumpe),    (mask & RELAY_BIT_QUELLENPUMPE) != 0);
                pubBin(id(relais_pufferladepumpe), (mask & RELAY_BIT_PUFFERLADEPUMPE) != 0);
                pubBin(id(relais_heizkreispumpe),  (mask & RELAY_BIT_HEIZKREISPUMPE) != 0);
                pubBin(id(relais_dhc1),            (mask & RELAY_BIT_DHC1) != 0);
                pubBin(id(relais_dhc2),            (mask & RELAY_BIT_DHC2) != 0);
            }
            break;

        case HASH_ZWEITER_WE_STATUS:
            if (isValidFloat && id(zweiter_we_status_raw).state != floatValue) {
                id(zweiter_we_status_raw).publish_state(floatValue);
            }
            break;

        case HASH_LAUFZEIT_WP1:
            if (isValidFloat && floatValue >= 0.0f) {
                if (!id(laufzeit_wp1_raw).has_state() || id(laufzeit_wp1_raw).state != floatValue) {
                    id(laufzeit_wp1_raw).publish_state(floatValue);
                }
                if (!id(verdichter_betriebsstunden).has_state() || id(verdichter_betriebsstunden).state != floatValue) {
                    id(verdichter_betriebsstunden).publish_state(floatValue);
                }
            }
            break;

        case HASH_LAUFZEIT_DHC1:
            if (isValidFloat && floatValue >= 0.0f && (!id(dhc1_betriebsstunden).has_state() || id(dhc1_betriebsstunden).state != floatValue)) {
                id(dhc1_betriebsstunden).publish_state(floatValue);
            }
            break;

        case HASH_LAUFZEIT_DHC2:
            if (isValidFloat && floatValue >= 0.0f && (!id(dhc2_betriebsstunden).has_state() || id(dhc2_betriebsstunden).state != floatValue)) {
                id(dhc2_betriebsstunden).publish_state(floatValue);
            }
            break;

        case HASH_STILLSTANDZEIT_0:
            if (isValidFloat && floatValue >= 0.0f && (!id(verdichter_rest_stillstand).has_state() || id(verdichter_rest_stillstand).state != floatValue)) {
                id(verdichter_rest_stillstand).publish_state(floatValue);
            }
            break;

        case HASH_HEIZKURVE:
            if (isValidFloat && floatValue >= 0.0f && floatValue <= 5.0f && (!id(heizkurve_wert).has_state() || fabsf(id(heizkurve_wert).state - floatValue) > 0.005f)) {
                id(heizkurve_wert).publish_state(floatValue);
            }
            break;

        case HASH_VORLAUFSOLLTEMP:
            if (isValidFloat && floatValue > -30.0f && floatValue < 80.0f && (!id(vorlauf_solltemp).has_state() || fabsf(id(vorlauf_solltemp).state - floatValue) > 0.05f)) {
                id(vorlauf_solltemp).publish_state(floatValue);
            }
            break;

        case HASH_SOMMERBETRIEB: {
            if (value == "on" || value == "off") {
                bool isSommer = (value == "on");
                if (!id(sommerbetrieb_aktiv).has_state() || id(sommerbetrieb_aktiv).state != isSommer) {
                    id(sommerbetrieb_aktiv).publish_state(isSommer);
                }
            }
            break;
        }

        case HASH_PUFFERSOLL:
            if (isValidFloat && id(puffersoll).state != floatValue) {
                id(puffersoll).publish_state(floatValue);
            }
            break;
            
        case HASH_RAUMSOLLTEMP_I:
            if (isValidFloat && id(raumsolltemp_i).state != floatValue) {
                id(raumsolltemp_i).publish_state(floatValue);
            }
            break;
            
        case HASH_RAUMSOLLTEMP_II:
            if (isValidFloat && id(raumsolltemp_ii).state != floatValue) {
                id(raumsolltemp_ii).publish_state(floatValue);
            }
            break;
            
        case HASH_RAUMSOLLTEMP_III:
            if (isValidFloat && id(raumsolltemp_iii).state != floatValue) {
                id(raumsolltemp_iii).publish_state(floatValue);
            }
            break;
            
        case HASH_RAUMSOLLTEMP_NACHT:
            if (isValidFloat && id(raumsolltemp_nacht).state != floatValue) {
                id(raumsolltemp_nacht).publish_state(floatValue);
            }
            break;
            
        // The number entities mirror these sensors through their lambdas (2.3.0)
        case HASH_EINSTELL_SPEICHERSOLLTEMP:
            if (isValidFloat && id(einstell_speichersolltemp).state != floatValue) {
                id(einstell_speichersolltemp).publish_state(floatValue);
            }
            break;
            
        case HASH_EINSTELL_SPEICHERSOLLTEMP2:
            if (isValidFloat && id(einstell_speichersolltemp2).state != floatValue) {
                id(einstell_speichersolltemp2).publish_state(floatValue);
            }
            break;
            
        case HASH_FEHLERMELDUNG:
            if (isValidFloat) {
                int errCode = static_cast<int>(lroundf(floatValue));
                if (!id(fehlermeldung).has_state() || id(fehlermeldung).state != floatValue) {
                    id(fehlermeldung).publish_state(floatValue);
                }
                std::string errTxt = wpmiErrorText(errCode);
                if (!id(fehler_status).has_state() || id(fehler_status).state != errTxt) {
                    id(fehler_status).publish_state(errTxt);
                }
                bool faultActive = (errCode != 0);
                if (!id(wp_stoerung_aktiv).has_state() || id(wp_stoerung_aktiv).state != faultActive) {
                    id(wp_stoerung_aktiv).publish_state(faultActive);
                }
            }
            break;
            
        // Binary sensors
        case HASH_EVU_SPERRE_AKTIV: {
            bool state = (publishValue == "on");
            if (!id(evu_sperre_aktiv).has_state() || id(evu_sperre_aktiv).state != state) {
                id(evu_sperre_aktiv).publish_state(state);
            }
            break;
        }
            
        // WW_ECO removed — no hot water tank connected
            
        // HASH_INITIALISIERUNG: removed in 2.3.0 (the MANAGER never answered it)

        case HASH_BRENNER: {
            bool state = (isValidFloat && floatValue > 0.5f);
            if (!id(brenner).has_state() || id(brenner).state != state) {
                id(brenner).publish_state(state);
            }
            break;
        }
            
        // Text sensors
        case HASH_PROGRAMMSCHALTER:
            if (id(programmschalter).state != value) {
                id(programmschalter).publish_state(value);
            }
            // ressl 2.3.0: only cache the value. The select shows it through its
            // lambda. The old make_call().perform() ran the select's set_action and
            // wrote every differing read-back to the WPM i (EEPROM).
            if (hscModeIndex(value) >= 0) {
                hscLastProgrammschalter = value;
            }
            break;
            
        default:
            // Signal not mapped to native sensor
            break;
    }
}

void updateSensor(const CanMember &cm, const ElsterIndex *ei, const std::string &value)
{
    // Skip empty values
    if (value.empty()) {
        return;
    }
    
    // Invert boolean logic for EVU_SPERRE_AKTIV signal
    // CAN: 1 = lock inactive (off), 0 = lock active (on)
    // MQTT: "off" = lock inactive, "on" = lock active
    std::string publishValue = value;
    if (strcmp(ei->Name, "EVU_SPERRE_AKTIV") == 0) {
        if (value == "on") {
            publishValue = "off";
        } else if (value == "off") {
            publishValue = "on";
        }
    }
    
    // Check if discovery is needed
    std::string uid = getOrCreateUID(cm, ei->Name);
    bool needsDiscovery = (discoveredSignals.find(uid) == discoveredSignals.end());
    
    if (needsDiscovery) {
        // Mark as discovered and publish discovery immediately
        discoveredSignals.insert(uid);
        publishMqttDiscovery(cm, ei);
    }
    
    // Publish state
    publishMqttState(cm, ei, publishValue);
    
    // Update native ESPHome sensors (appears in HA via ESPHome API)
    updateNativeSensors(ei->Name, value, publishValue);
    
    // Fast signal dispatch using compile-time hash (O(1) switch/jump table)
    const char* signalName = ei->Name;
    uint32_t signalHash = hash_runtime(signalName);
    
    switch (signalHash) {
        case HASH_JAHR: {
            char* endPtr;
            long intValue = strtol(value.c_str(), &endPtr, 10);
            if (endPtr != value.c_str() && *endPtr == '\0') {
                lastJahr = static_cast<int>(intValue);
                // ESP_LOGD("CALC", "Updated JAHR: %d", lastJahr);
            }
            break;
        }
        
        case HASH_MONAT: {
            char* endPtr;
            long intValue = strtol(value.c_str(), &endPtr, 10);
            if (endPtr != value.c_str() && *endPtr == '\0') {
                lastMonat = static_cast<int>(intValue);
                // ESP_LOGD("CALC", "Updated MONAT: %d", lastMonat);
            }
            break;
        }
        
        case HASH_TAG: {
            char* endPtr;
            long intValue = strtol(value.c_str(), &endPtr, 10);
            if (endPtr != value.c_str() && *endPtr == '\0') {
                lastTag = static_cast<int>(intValue);
                // ESP_LOGD("CALC", "Updated TAG: %d", lastTag);
                publishDate();
            }
            break;
        }
        
        case HASH_STUNDE: {
            char* endPtr;
            long intValue = strtol(value.c_str(), &endPtr, 10);
            if (endPtr != value.c_str() && *endPtr == '\0') {
                lastStunde = static_cast<int>(intValue);
                // ESP_LOGD("CALC", "Updated STUNDE: %d", lastStunde);
            }
            break;
        }
        
        case HASH_MINUTE: {
            char* endPtr;
            long intValue = strtol(value.c_str(), &endPtr, 10);
            if (endPtr != value.c_str() && *endPtr == '\0') {
                lastMinute = static_cast<int>(intValue);
                // ESP_LOGD("CALC", "Updated MINUTE: %d", lastMinute);
                publishTime();
            }
            break;
        }
        
        case HASH_SEKUNDE: {
            char* endPtr;
            long intValue = strtol(value.c_str(), &endPtr, 10);
            if (endPtr != value.c_str() && *endPtr == '\0') {
                lastSekunde = static_cast<int>(intValue);
                // ESP_LOGD("CALC", "Updated SEKUNDE: %d", lastSekunde);
            }
            break;
        }
        
        case HASH_SOMMERBETRIEB:
            publishBetriebsart(value);
            break;
        
        case HASH_WPVORLAUFIST: {
            char* endPtr;
            float floatValue = strtof(value.c_str(), &endPtr);
            if (endPtr != value.c_str() && *endPtr == '\0') {
                lastWpVorlaufIst = floatValue;
                // Delta T will be calculated and published by scheduler
            } else {
                ESP_LOGW("CALC", "Failed to parse WPVORLAUFIST value: %s", value.c_str());
            }
            break;
        }
        
        case HASH_RUECKLAUFISTTEMP: {
            char* endPtr;
            float floatValue = strtof(value.c_str(), &endPtr);
            if (endPtr != value.c_str() && *endPtr == '\0') {
                lastRuecklaufIstTemp = floatValue;
                // Delta T will be calculated and published by scheduler
            } else {
                ESP_LOGW("CALC", "Failed to parse RUECKLAUFISTTEMP value: %s", value.c_str());
            }
            break;
        }
        
        case HASH_VERDICHTER: {
            char* endPtr;
            float floatValue = strtof(value.c_str(), &endPtr);
            if (endPtr != value.c_str() && *endPtr == '\0') {
                lastVerdichterValue = floatValue;
                // Compressor state will be calculated and published by scheduler
            } else {
                ESP_LOGW("CALC", "Failed to parse VERDICHTER value: %s", value.c_str());
            }
            break;
        }
        
        case HASH_EL_AUFNAHMELEISTUNG_HEIZ:
        case HASH_EL_AUFNAHMELEISTUNG_WW:
        case HASH_WAERMEERTRAG_2WE_WW:
        case HASH_WAERMEERTRAG_2WE_HEIZ:
        case HASH_WAERMEERTRAG_WW:
        case HASH_WAERMEERTRAG_HEIZ:
            storeCOPEnergyValue(signalName, value);
            updateCOPCalculations();
            break;
        
        default:
            // Signal not monitored for calculated sensors - no action needed
            break;
    }
}

// Helper function to generate random number in range [min, max) using ESP32 hardware RNG
// esp_random() uses hardware entropy sources and doesn't need seeding
unsigned long getRandomInRange(unsigned long min, unsigned long max) {
    if (min >= max) return min;
    unsigned long range = max - min;
    return min + (esp_random() % range);
}

void publishCanDiagnostics() {
    twai_status_info_t status;
    if (twai_get_status_info(&status) != ESP_OK) return;

    if (status.state == TWAI_STATE_BUS_OFF) {
        ESP_LOGW("CAN_RECOVERY", "TWAI bus-off detected, initiating recovery");
        twai_initiate_recovery();
    } else if (status.state == TWAI_STATE_STOPPED) {
        // twai_initiate_recovery() leaves the controller STOPPED once recovery
        // completes (128 bus-free occurrences) — must explicitly restart.
        // STOPPED here always means "post-recovery": the boot-time STOPPED
        // window (before twai_start_v2() in setup_internal()) completes
        // long before this 30s-cadence diagnostic loop ever runs.
        ESP_LOGW("CAN_RECOVERY", "TWAI stopped after recovery, restarting");
        twai_start();
    }

    // Publish discovery (only once per UID — cached internally)
    publishCalculatedSensorDiscovery(calculatedSensors[6]);
    publishCalculatedSensorDiscovery(calculatedSensors[7]);
    publishCalculatedSensorDiscovery(calculatedSensors[8]);
    publishCalculatedSensorDiscovery(calculatedSensors[9]);

    char buf[16];

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)status.tx_error_counter);
    hscMqttPublish("heatingpump/calculated/can_tec/state", buf, strlen(buf), true);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)status.rx_error_counter);
    hscMqttPublish("heatingpump/calculated/can_rec/state", buf, strlen(buf), true);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)status.bus_error_count);
    hscMqttPublish("heatingpump/calculated/can_bus_errors/state", buf, strlen(buf), true);

    const char* stateStr;
    switch (status.state) {
        case TWAI_STATE_STOPPED:    stateStr = "Gestoppt";  break;
        case TWAI_STATE_RUNNING:    stateStr = "Läuft";     break;
        case TWAI_STATE_BUS_OFF:    stateStr = "Bus-Off";   break;
        case TWAI_STATE_RECOVERING: stateStr = "Erholung";  break;
        default:                    stateStr = "Unbekannt"; break;
    }
    hscMqttPublish("heatingpump/calculated/can_state/state",
                            stateStr, strlen(stateStr), true);
}

// Process calculated sensor updates with frequency-based scheduling
// This function should be called regularly from the main loop
void processCalculatedSensors() {
    unsigned long now = millis();
    
    // Initialize scheduled update times on first run (with random offsets to spread load)
    static bool initialized = false;
    if (!initialized && requestManagerStarted) {
        // Initialize all calculated sensor schedules with random offsets
        ESP_LOGI("CALC_SCHED", "Initializing calculated sensor schedules with random offsets");
        
        // Delta T sensors (30s frequency)
        unsigned long deltaT_interval = CALC_DELTA_T_FREQUENCY * 1000UL;
        nextDeltaTUpdate = now + getRandomInRange(0, deltaT_interval + 1);
        
        // Compressor sensor (30s frequency)
        unsigned long compressor_interval = CALC_COMPRESSOR_FREQUENCY * 1000UL;
        nextCompressorUpdate = now + getRandomInRange(0, compressor_interval + 1);
        
        // Date/Time sensors (1min frequency)
        unsigned long datetime_interval = CALC_DATETIME_FREQUENCY * 1000UL;
        nextDateTimeUpdate = now + getRandomInRange(0, datetime_interval + 1);
        
        // Betriebsart sensor (1min frequency)
        unsigned long betriebsart_interval = CALC_BETRIEBSART_FREQUENCY * 1000UL;
        nextBetriebsartUpdate = now + getRandomInRange(0, betriebsart_interval + 1);

        // COP estimation (30s frequency)
        unsigned long cop_interval = CALC_DELTA_T_FREQUENCY * 1000UL;
        nextCOPUpdate = now + getRandomInRange(5000, cop_interval + 1);

        // Low-flow protection watchdog (5s frequency)
        nextLowFlowCheck = now + 5000UL;

        // CAN diagnostics (30s frequency)
        unsigned long can_diag_interval = CALC_CAN_DIAG_FREQUENCY * 1000UL;
        nextCanDiagUpdate = now + getRandomInRange(0, can_diag_interval + 1);

        initialized = true;
        ESP_LOGI("CALC_SCHED", "Calculated sensor scheduler initialized");
    }
    
    if (!initialized) return; // Wait for request manager to start
    
    // Check and publish Delta T sensors
    if (hscTimeReached(now, nextDeltaTUpdate)) {
        publishDeltaTContinuous();
        publishDeltaTRunning();
        nextDeltaTUpdate = now + (CALC_DELTA_T_FREQUENCY * 1000UL) + getRandomInRange(0, 1000);
    }
    
    // Check and publish Compressor sensor
    if (hscTimeReached(now, nextCompressorUpdate)) {
        publishCompressorActive();
        nextCompressorUpdate = now + (CALC_COMPRESSOR_FREQUENCY * 1000UL) + getRandomInRange(0, 1000);
    }
    
    // Check and publish COP estimation
    if (hscTimeReached(now, nextCOPUpdate)) {
        publishCOPEstimate();
        nextCOPUpdate = now + (CALC_DELTA_T_FREQUENCY * 1000UL) + getRandomInRange(0, 1000);
    }

    // Check WPF 7 minimum flow guard (5s cadence)
    if (hscTimeReached(now, nextLowFlowCheck)) {
        checkLowFlowGuard();
        nextLowFlowCheck = now + 5000UL;
    }

    // Check and publish Date/Time sensors
    if (hscTimeReached(now, nextDateTimeUpdate)) {
        publishDate();
        publishTime();
        nextDateTimeUpdate = now + (CALC_DATETIME_FREQUENCY * 1000UL) + getRandomInRange(0, 1000);
    }
    
    // Check and publish Betriebsart sensor (only if SOMMERBETRIEB value is available)
    if (hscTimeReached(now, nextBetriebsartUpdate)) {
        // Betriebsart requires a value parameter, so we skip auto-publishing
        // It will be published when SOMMERBETRIEB signal is received
        nextBetriebsartUpdate = now + (CALC_BETRIEBSART_FREQUENCY * 1000UL) + getRandomInRange(0, 1000);
    }

    // Check and publish CAN diagnostic sensors
    if (hscTimeReached(now, nextCanDiagUpdate)) {
        publishCanDiagnostics();
        nextCanDiagUpdate = now + (CALC_CAN_DIAG_FREQUENCY * 1000UL) + getRandomInRange(0, 1000);
    }
}

// Timeout tracking removed for simplification
// Signals that don't respond will simply not update in Home Assistant

// Process signal request table with frequency-based scheduling
void processSignalRequests() {
    unsigned long now = millis();
    
    // Startup delay: wait before starting signal requests
    if (!requestManagerStarted) {
        if (requestManagerStartTime == 0) {
            requestManagerStartTime = now;
            ESP_LOGI("REQUEST_MGR", "Starting signal request manager (%ds startup delay)", STARTUP_DELAY_MS / 1000);
            return;
        }
        
        if (now - requestManagerStartTime < STARTUP_DELAY_MS) {
            return; // Still in startup delay
        }
        
        requestManagerStarted = true;
        ESP_LOGI("REQUEST_MGR", "Signal request manager active - processing %d signal definitions", 
                 SIGNAL_REQUEST_COUNT);
        
        // Initialize all signal schedules with random offsets to spread out initial load
        ESP_LOGI("REQUEST_MGR", "Initializing signal schedules with random offsets to prevent burst");
        for (int i = 0; i < SIGNAL_REQUEST_COUNT; i++) {
            const SignalRequest& req = signalRequests[i];
            const ElsterIndex* ei = GetElsterIndex(req.signalName);
            if (!ei || ei->Index == 0xFFFF) continue;
            
            // Skip blacklisted signals during initialization
            if (isPermanentlyBlacklisted(ei->Name)) continue;
            
            unsigned long intervalMs = req.frequency * 1000UL;
            // Cap initial boot stagger to 90 s so even 10-min counters populate promptly after reboot
            unsigned long initSpanMs = (intervalMs > 90000UL) ? 90000UL : intervalMs;
            
            if (req.member == cm_other) {
                // Schedule for all members
                const CanMember* allMembers[] = {
                    &CanMembers[cm_kessel],
                    &CanMembers[cm_manager],
                    &CanMembers[cm_heizmodul]
                };
                for (size_t m = 0; m < 3; m++) {
                    std::string key = std::string(allMembers[m]->Name) + "_" + ei->Name;
                    // Random offset within boot window using ESP32 hardware RNG
                    unsigned long randomOffset = getRandomInRange(0, initSpanMs + 1);
                    nextRequestTime[key] = now + randomOffset;
                }
            } else {
                // Schedule for specific member
                const CanMember* member = &CanMembers[req.member];
                std::string key = std::string(member->Name) + "_" + ei->Name;
                // Random offset within boot window using ESP32 hardware RNG
                unsigned long randomOffset = getRandomInRange(0, initSpanMs + 1);
                nextRequestTime[key] = now + randomOffset;
            }
        }
        ESP_LOGI("REQUEST_MGR", "Initialized %d signal schedules", nextRequestTime.size());
    }
    
    // Rate limiting: send up to MAX_REQUESTS_PER_ITERATION requests per iteration
    // Randomization in scheduling prevents bursts
    int requestsSentThisIteration = 0;
    
    // Round-robin processing: start from last position to prevent starvation of signals at end of array
    // Process all signals, wrapping around, until we hit rate limit or complete full cycle
    int processedCount = 0;
    int currentIndex = signalProcessingStartIndex;
    
    while (processedCount < SIGNAL_REQUEST_COUNT && requestsSentThisIteration < MAX_REQUESTS_PER_ITERATION) {
        const SignalRequest& req = signalRequests[currentIndex];
        processedCount++;
        
        const ElsterIndex* ei = GetElsterIndex(req.signalName);
        if (!ei || ei->Index == 0xFFFF) {
            // Move to next signal and continue
            currentIndex = (currentIndex + 1) % SIGNAL_REQUEST_COUNT;
            continue; // Signal not found in table
        }
        
        // Skip blacklisted signals - don't request them on CAN bus
        if (isPermanentlyBlacklisted(ei->Name)) {
            currentIndex = (currentIndex + 1) % SIGNAL_REQUEST_COUNT;
            continue; // Signal is blacklisted
        }
        
        unsigned long intervalMs = req.frequency * 1000UL;
        
        // Determine which members to request from
        if (req.member == cm_other) {
            // Request from all members (each tracked independently)
            const CanMember* allMembers[] = {
                &CanMembers[cm_kessel],
                &CanMembers[cm_manager],
                &CanMembers[cm_heizmodul]
            };
            
            // Track how many members we've sent to in this cm_other group to stagger them
            int sentInThisGroup = 0;
            
            for (const auto* member : allMembers) {
                // Don't break - just skip remaining members for this signal
                // The outer loop will continue with next signal
                if (requestsSentThisIteration >= MAX_REQUESTS_PER_ITERATION) {
                    break; // Hit rate limit for this iteration
                }
                
                // Use ei->Name (from ElsterTable) to match blacklist key format
                std::string key = std::string(member->Name) + "_" + ei->Name;
                
                // Get the next scheduled time for this signal
                unsigned long nextScheduled = nextRequestTime[key];
                
                // Check if this signal is overdue (current time >= scheduled time)
                if (hscTimeReached(now, nextScheduled)) {
                    // For cm_other: only send to ONE member per iteration to prevent bursts
                    // Other members will be checked in subsequent iterations
                    if (sentInThisGroup == 0) {
                        readSignal(member, ei);
                        requestsSentThisIteration++;
                        sentInThisGroup++;
                        
                        // Calculate next scheduled time with random offset (0 to 5% of interval)
                        // This keeps signals from synchronizing while staying close to target frequency
                        unsigned long maxJitter = (intervalMs / 20); // 5% of interval
                        if (maxJitter < 500) maxJitter = 500; // Minimum 500ms jitter
                        unsigned long randomDelay = getRandomInRange(0, maxJitter + 1);
                        nextRequestTime[key] = now + intervalMs + randomDelay;
                        
                        // ESP_LOGD("REQUEST_MGR", "Sent %s, next in %lums", key.c_str(), intervalMs + randomDelay);
                    }
                    // Else: skip this member for now, will be checked next iteration
                }
            }
        } else {
            // Request from specific member
            const CanMember* member = &CanMembers[req.member];
            std::string key = std::string(member->Name) + "_" + ei->Name;
            
            // Get the next scheduled time for this signal
            unsigned long nextScheduled = nextRequestTime[key];
            
            // Check if this signal is overdue (current time >= scheduled time)
            if (hscTimeReached(now, nextScheduled)) {
                readSignal(member, ei);
                requestsSentThisIteration++;
                
                // Calculate next scheduled time with random offset (0 to 5% of interval)
                // This keeps signals from synchronizing while staying close to target frequency
                unsigned long maxJitter = (intervalMs / 20); // 5% of interval
                if (maxJitter < 500) maxJitter = 500; // Minimum 500ms jitter
                unsigned long randomDelay = getRandomInRange(0, maxJitter + 1);
                nextRequestTime[key] = now + intervalMs + randomDelay;
                
                // ESP_LOGD("REQUEST_MGR", "Sent %s, next in %lums", key.c_str(), intervalMs + randomDelay);
            }
        }
        
        // Move to next signal in round-robin fashion (wrap around at end)
        currentIndex = (currentIndex + 1) % SIGNAL_REQUEST_COUNT;
    }
}

// ressl 2.3.0: (member, index) pairs that are actually polled. Only answers
// for these pairs update entities. Everything else (CAN scan answers, traffic of
// other bus members) used to be attributed by signal name alone, so a value read
// from another member could overwrite the native sensor of the same name.
static std::unordered_set<uint32_t> hscPollKeys;
static bool hscPollKeysBuilt = false;

inline uint32_t hscPollKey(uint32_t canId, uint16_t index)
{
    return (canId << 16) | index;
}

static void hscBuildPollKeys()
{
    for (size_t i = 0; i < SIGNAL_REQUEST_COUNT; i++) {
        const SignalRequest &req = signalRequests[i];
        const ElsterIndex *ei = GetElsterIndex(req.signalName);
        if (!ei || ei->Index == 0) continue;   // INDEX_NOT_FOUND
        if (req.member == cm_other) {
            hscPollKeys.insert(hscPollKey(CanMembers[cm_kessel].CanId, ei->Index));
            hscPollKeys.insert(hscPollKey(CanMembers[cm_manager].CanId, ei->Index));
            hscPollKeys.insert(hscPollKey(CanMembers[cm_heizmodul].CanId, ei->Index));
        } else {
            hscPollKeys.insert(hscPollKey(CanMembers[req.member].CanId, ei->Index));
        }
    }
    hscPollKeysBuilt = true;
    ESP_LOGI("REQUEST_MGR", "%u polled (member, index) pairs", (unsigned) hscPollKeys.size());
}

// Defined in can_scan.h (read-only CAN scan)
bool canScanOnFrame(uint32_t can_id, const std::vector<uint8_t> &msg);
// Defined in bus_monitor.h (passive bus monitor, ressl 2.4.1)
void busMonOnFrame(uint32_t can_id, const std::vector<uint8_t> &msg);

void processAndUpdate(uint32_t can_id, std::vector<uint8_t> msg)
{
    // ressl 2.4.1: the passive monitor sees every frame, before anything is dropped
    busMonOnFrame(can_id, msg);

    if (msg.size() < 7) return;

    // Elster type nibble: 0 = write, 1 = read request, 2 = response.
    // A read request (e.g. from another bus member) carries no value; decoding
    // it used to publish a bogus 0 for that signal.
    if ((msg[0] & 0x0F) == 0x01) return;

    canScanOnFrame(can_id, msg);

    const uint16_t index = (msg[2] == 0xfa) ? (uint16_t)((msg[3] << 8) | msg[4]) : (uint16_t) msg[2];
    if (!hscPollKeysBuilt) hscBuildPollKeys();
    if (hscPollKeys.find(hscPollKey(can_id, index)) == hscPollKeys.end()) return;

    std::string value;
    const CanMember *cm = nullptr;
    const ElsterIndex *ei = processCanMessage(msg, can_id, value, &cm);

    // Skip permanently blacklisted signals
    if (isPermanentlyBlacklisted(ei->Name))
    {
        return; // Reject before lookup, parsing, formatting, logging
    }

    updateSensor(*cm, ei, value);
    return;
}

void updateTime(CanMember cm, const char *str_time)
{
    ESP_LOGI("WRITE UHRZEIT VIA BUTTON", "%s", str_time);

    char stunde[3];
    char minute[3];
    char sekunde[3];

    strncpy(stunde, str_time, sizeof(stunde) - 1);
    stunde[2] = '\0';
    strncpy(minute, str_time + 3, sizeof(minute) - 1);
    minute[2] = '\0';
    strncpy(sekunde, str_time + 6, sizeof(sekunde) - 1);
    sekunde[2] = '\0';
    const char *cstunde = stunde;
    const char *cminute = minute;
    const char *csekunde = sekunde;
    ESP_LOGI("WRITE", "Stunde: %s, Minute: %s, Sekunde: %s", cstunde, cminute, csekunde);
    writeSignal(&cm, "STUNDE", cstunde);
    readSignal(&cm, "STUNDE");
    writeSignal(&cm, "MINUTE", cminute);
    readSignal(&cm, "MINUTE");
    writeSignal(&cm, "SEKUNDE", csekunde);
    readSignal(&cm, "SEKUNDE");
}

void updateDate(CanMember cm, const char *str_date)
{
    ESP_LOGI("WRITE DATUM VIA BUTTON", "%s", str_date);
    char year[3];
    char month[3];
    char day[3];

    strncpy(year, str_date + 2, sizeof(year) - 1);
    year[2] = '\0';
    strncpy(month, str_date + 5, sizeof(month) - 1);
    month[2] = '\0';
    strncpy(day, str_date + 8, sizeof(day) - 1);
    day[2] = '\0';

    const char *cyear = year;
    const char *cmonth = month;
    const char *cday = day;
    ESP_LOGI("WRITE", "Year: %s, Month: %s, Day: %s", cyear, cmonth, cday);
    writeSignal(&cm, "JAHR", cyear);
    readSignal(&cm, "JAHR");
    writeSignal(&cm, "MONAT", cmonth);
    readSignal(&cm, "MONAT");
    writeSignal(&cm, "TAG", cday);
    readSignal(&cm, "TAG");
}

#endif // !defined(HA_DUMMY_BUILD)

#endif // ha_stiebel_control_H

