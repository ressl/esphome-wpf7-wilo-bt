/*
 * Configuration Constants for ha-stiebel-control
 * 
 * This file contains all configurable parameters and thresholds.
 * Edit these values to customize system behavior without touching core logic.
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// DEVICE MODEL
// ============================================================================
// The build injects the model unquoted (-DHA_DEVICE_MODEL=wpl13e). Quoting it
// inside the build flag only works with the old PlatformIO build; the ESP-IDF /
// ninja build used since ESPHome 2026.7 passes the flag through verbatim and
// the shell quotes end up in the source ("missing terminating ' character").
// So the flag stays quote-free and the string literal is made here.
#ifndef HA_DEVICE_MODEL
#define HA_DEVICE_MODEL unknown
#endif
#define HA_STRINGIFY_(x) #x
#define HA_STRINGIFY(x) HA_STRINGIFY_(x)
#define HA_DEVICE_MODEL_STR HA_STRINGIFY(HA_DEVICE_MODEL)

// ============================================================================
// REQUEST MANAGER SETTINGS
// ============================================================================

// Delay after boot before starting signal requests (milliseconds)
#define STARTUP_DELAY_MS 30000

// Maximum number of requests to send per iteration (rate limiting)
#define MAX_REQUESTS_PER_ITERATION 1

// ============================================================================
// TIMING INTERVALS (for use in signalRequests table)
// ============================================================================

// Request frequency definitions (in seconds)
#define FREQ_10S 10
#define FREQ_30S 30
#define FREQ_1MIN 60
#define FREQ_5MIN 300
#define FREQ_10MIN 600
#define FREQ_30MIN 1800
#define FREQ_60MIN 3600

// ============================================================================
// CALCULATED SENSOR SETTINGS
// ============================================================================

// Update frequency for calculated sensors (in seconds)
// These values determine how often calculated values are published
// Setting lower values increases MQTT traffic but provides more responsive updates

// Delta T sensors update frequency (based on temperature sensor frequency)
#define CALC_DELTA_T_FREQUENCY FREQ_30S

// Compressor active sensor update frequency (based on VERDICHTER frequency)
#define CALC_COMPRESSOR_FREQUENCY FREQ_30S

// Date/Time sensors update frequency (based on date/time signal frequency)
#define CALC_DATETIME_FREQUENCY FREQ_1MIN

// Betriebsart sensor update frequency (based on SOMMERBETRIEB frequency)
#define CALC_BETRIEBSART_FREQUENCY FREQ_1MIN

// CAN diagnostic sensors update frequency (ESP32-S3 / TWAI only)
#define CALC_CAN_DIAG_FREQUENCY FREQ_30S

// ============================================================================
// COP CALCULATION SETTINGS
// ============================================================================

// Minimum divisor value to avoid division by zero
#define COP_MIN_DIVISOR 0.001f

// Number of decimal places for COP values
#define COP_DECIMAL_PLACES 2

// ============================================================================
// INVALID VALUE DETECTION
// ============================================================================

// Invalid marker values (exact matches)
#define INVALID_VALUE_NEG_255 -255.0f
#define INVALID_VALUE_NEG_32768 -32768.0f
#define INVALID_VALUE_POS_32767 32767.0f

// Invalid value range limits
#define INVALID_VALUE_MIN -300.0f
#define INVALID_VALUE_MAX 1000.0f

// Epsilon for floating point comparisons
#define INVALID_VALUE_EPSILON 0.01f

// ============================================================================
// NON-BLOCKING DELAY SETTINGS
// ============================================================================

// Minimum random delay between CAN requests (milliseconds)
#define MIN_REQUEST_DELAY_MS 200

// Maximum random delay between CAN requests (milliseconds)
#define MAX_REQUEST_DELAY_MS 1000

// ============================================================================
// MQTT ENTITIES (ressl WPF7 fork)
// ============================================================================
// 0 = Home Assistant gets every value through the native ESPHome API only.
//     No custom MQTT discovery and no custom MQTT state topics are published.
//     The MQTT client itself stays (birth/will, log topic, CAN scan results).
// 1 = legacy behaviour: additional MQTT entities via custom discovery.
#ifndef HA_MQTT_ENTITIES
#define HA_MQTT_ENTITIES 0
#endif

// ============================================================================
// SG READY (room-setpoint boost, ressl WPF7 fork)
// ============================================================================
// Minimum time between two applied SG Ready changes (except "2 - Normal").
// Each change costs 1-2 EEPROM writes in the WPM i.
#define SG_MIN_INTERVAL_MS (10UL * 60UL * 1000UL)
// Hard upper limit for the boosted room setpoint (degC)
#define SG_MAX_ROOM_SETPOINT 25.0f

// ============================================================================
// CAN SCAN (read-only, ressl WPF7 fork)
// ============================================================================
// Pause between two scan read requests (ms). Regular polling continues in
// parallel (~0.6 req/s), so the bus sees at most ~2 requests per second.
#define CAN_SCAN_PERIOD_MS 750
// Probe requests per member during member discovery (phase 1)
#define CAN_SCAN_PROBES_PER_MEMBER 3
// Abort the scan if the TWAI transmit error counter exceeds this value
#define CAN_SCAN_MAX_TEC 32

#endif // CONFIG_H
