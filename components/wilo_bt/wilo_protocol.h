#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace wilo_protocol {

// Wilo BLE GATT Service & Characteristic UUIDs
static constexpr const char *WILO_SERVICE_UUID = "00000001-0000-1000-8000-0023f5000001";
static constexpr const char *WILO_TX_DATA_UUID = "00000002-0000-1000-8000-0023f5000001";
static constexpr const char *WILO_RX_DATA_UUID = "00000003-0000-1000-8000-0023f5000001";
static constexpr const char *WILO_RX_CREDITS_UUID = "00000004-0000-1000-8000-0023f5000001";
static constexpr const char *WILO_TX_CREDITS_UUID = "00000005-0000-1000-8000-0023f5000001";

// Flow-control credit grant written to WILO_TX_CREDITS_UUID
static constexpr uint8_t WILO_RX_CREDIT_GRANT = 0x18;
static constexpr uint8_t WILO_INITIAL_RX_CREDIT_WRITES = 4;
static constexpr uint8_t WILO_FALLBACK_TX_CREDITS = 20;

// Transport channel IDs (byte 0 of Wilo envelope [ch, frag, len_lo, len_hi, ...])
static constexpr uint8_t WILO_CHANNEL_TRANSPORT = 0x01;
static constexpr uint8_t WILO_CHANNEL_AUTH = 0x05;
static constexpr uint8_t WILO_CHANNEL_COAP = 0x1F;
static constexpr uint8_t WILO_ENVELOPE_VERSION = 0x03;  // 0x03 = unfragmented single envelope

// CoAP constants (RFC 7252)
static constexpr uint8_t COAP_CODE_GET = 0x01;   // 0.01
static constexpr uint8_t COAP_CODE_POST = 0x02;  // 0.02
static constexpr uint8_t COAP_CODE_PUT = 0x03;   // 0.03
static constexpr uint8_t COAP_CONTENT_FORMAT_CBOR = 60;
static constexpr uint16_t COAP_OPT_URI_PATH = 11;
static constexpr uint16_t COAP_OPT_CONTENT_FORMAT = 12;
static constexpr uint16_t COAP_OPT_WILO_SRC_ADDR = 2048;
static constexpr uint16_t COAP_OPT_WILO_DST_ADDR = 2050;
static constexpr uint16_t WILO_BATCH_KEY = 0xF000;

// Sentinel value used by Wilo firmware when a numeric process value is invalid/unavailable
static constexpr uint64_t WILO_INVALID_U32 = 0xFFFFFFFFULL;

// Verified Wilo Stratos PICO CoAP/CBOR register IDs (from Wilo-Assistant 6.13.0)
static constexpr uint16_t KEY_CONTROL_MODE = 0x0001;                // RW: BasicControlMode (1, 8, 10, 40, ...)
static constexpr uint16_t KEY_SETPOINT_NORM = 0x0079;               // RW: CF.SetPoint (Q14 normalized)
static constexpr uint16_t KEY_PRESSURE_INDEPENDENT_VALVE = 0x0191;  // RW: Pressure independent valve (0/1)
static constexpr uint16_t KEY_NIGHT_SETBACK = 0x025A;               // RW: CF.NightSetback (0/1)
static constexpr uint16_t KEY_MEDIUM_TEMP = 0x03A2;                 // RO: PV.MediumTemp ((raw - 14335) / 256 C)
static constexpr uint16_t KEY_MANUAL_RESTART = 0x05C9;              // RW: Manual restart trigger (1=start, 0=stop)
static constexpr uint16_t KEY_MANUAL_RESTART_STATUS = 0x05D3;       // RO: Manual restart status
static constexpr uint16_t KEY_AIR_VENTING = 0x0605;                 // RW: AV.Start/Stop (1=start, 0=stop)
static constexpr uint16_t KEY_AIR_VENTING_REMAINING = 0x060A;       // RO: AV.Status.RemainingTime (s)
static constexpr uint16_t KEY_AIR_VENTING_STATUS = 0x060F;          // RO: AV.Status.Status
static constexpr uint16_t KEY_PUMP_OPERATION_MODE = 0x0669;         // RW: CF.PumpOperationMode (1=ON, 0=OFF)
static constexpr uint16_t KEY_OVERRIDE_MODE = 0x066A;               // RW: OverrideModes.ConfigMode
static constexpr uint16_t KEY_ACTIVE_INF_OVERRIDE = 0x067F;         // RO: PumpState.ActiveInfluences.Override
static constexpr uint16_t KEY_ACTIVE_INF_FLOW = 0x0680;             // RO: PumpState.ActiveInfluences.FlowRelated
static constexpr uint16_t KEY_SETPOINT_MIN_MAP = 0x06F5;            // RO: CF.SetpointMinLimit map (Q14)
static constexpr uint16_t KEY_SETPOINT_MAX_MAP = 0x06F6;            // RO: CF.SetpointMaxLimit map (Q14)
static constexpr uint16_t KEY_SETPOINT_SCALE_MAP = 0x06F7;          // RO: Setpoint physical scale map
static constexpr uint16_t KEY_ACTUAL_HEAD = 0x08FD;                 // RO: PV.DeliveryHead (raw / 1024 m)
static constexpr uint16_t KEY_ACTUAL_FLOW = 0x08FE;                 // RO: PV.VolumeFlow ((raw - 0x80000000) / 1024 m3/h)
static constexpr uint16_t KEY_MAX_HEAD = 0x0939;                    // RO: DevP.PHdMax ((raw - 0x80000000) / 1024 m)
static constexpr uint16_t KEY_MAX_FLOW = 0x093B;                    // RO: DevP.VolFlowLimMax ((raw - 0x80000000) / 1024 m3/h)
static constexpr uint16_t KEY_MOTOR_SPEED = 0x09D9;                 // RO: PV.MotorSpeed ((raw - 32768) / 2 RPM)
static constexpr uint16_t KEY_OPERATING_SECONDS = 0x09DC;           // RO: PV.TotalMotorOperatingTime (s)
static constexpr uint16_t KEY_ELECTRICAL_POWER = 0x09EE;            // RO: PV.ElectricalInputPower (raw / 2 W)
static constexpr uint16_t KEY_TOTAL_ENERGY = 0x0A0C;                // RO: PV.TotalEnergyConsumption (raw / 128 kWh)
static constexpr uint16_t KEY_RESET_ENERGY = 0x0A22;                // W:  Reset energy meter (write 0)
static constexpr uint16_t KEY_ELECTRICAL_ENERGY = 0x0A23;           // RO: Resettable energy counter (raw / 128 kWh)
static constexpr uint16_t KEY_DEVICE_UUID = 0x0B36;                 // RO: DevP.UUID (16-byte UUID)
static constexpr uint16_t KEY_PUMP_TYPE = 0x0B3D;                   // RO: DevP.TypeLabelPumpName
static constexpr uint16_t KEY_PUMP_FAMILY = 0x0B3F;                 // RO: DevP.TypeLabelPumpType
static constexpr uint16_t KEY_SERIAL_NUMBER = 0x0B42;               // RO: Serial number string
static constexpr uint16_t KEY_SOFTWARE_VERSION = 0x0B44;            // RO: DevP.SwVersionBundle
static constexpr uint16_t KEY_APPLICATION_TYPE = 0x0BC4;            // RW: ApplicationType (0..2)
static constexpr uint16_t KEY_SUB_APPLICATION_TYPE = 0x0BC5;        // RW: SubApplicationType (0..6)
static constexpr uint16_t KEY_INSTALLATION_TYPE = 0x0BC6;           // RW: InstallationType (0..15)
static constexpr uint16_t KEY_ACTIVE_ALARMS = 0x0C8B;               // RO: Failure.List (array of active failure IDs)
static constexpr uint16_t KEY_FAILURE_DETAIL = 0x0C8C;              // RO: Failure detail (GET with CBOR failure ID)
static constexpr uint16_t KEY_DEVICE_NAME = 0x0F02;                 // RW: DevP.Name
static constexpr uint16_t KEY_DEVICE_LOCATION = 0x0F03;             // RW: DevP.Location

// Backward-compatible aliases for legacy names
static constexpr uint16_t KEY_STATUS_FLAG_0191 = KEY_PRESSURE_INDEPENDENT_VALVE;
static constexpr uint16_t KEY_STATUS_FLAG_025A = KEY_NIGHT_SETBACK;
static constexpr uint16_t KEY_UNKNOWN_03A2 = KEY_MEDIUM_TEMP;
static constexpr uint16_t KEY_RUN_CONFIG_0669 = KEY_PUMP_OPERATION_MODE;
static constexpr uint16_t KEY_FAULT_HISTORY = KEY_DEVICE_UUID;
static constexpr uint16_t KEY_LOCATION_NOTE = KEY_PUMP_FAMILY;
static constexpr uint16_t KEY_STATUS_0BC4 = KEY_APPLICATION_TYPE;
static constexpr uint16_t KEY_STATUS_0BC5 = KEY_SUB_APPLICATION_TYPE;
static constexpr uint16_t KEY_PUMP_STATUS = KEY_INSTALLATION_TYPE;

struct CborValue {
  enum class Type {
    UNSIGNED,
    NEGATIVE,
    FLOAT,
    BOOL,
    NIL,
    STRING,
    BYTES,
    ARRAY,
    MAP,
  };

  Type type{Type::NIL};
  uint64_t unsigned_value{0};
  int64_t signed_value{0};
  double float_value{0.0};
  bool bool_value{false};
  std::string string_value;
  std::vector<CborValue> array_values;
  std::vector<std::pair<CborValue, CborValue>> map_values;

  bool as_number(double *out) const;
  const CborValue *find_uint_key(uint64_t key) const;
};

struct WiloEnvelope {
  uint8_t channel{0};
  uint8_t version{0};
  uint16_t declared_length{0};
  std::vector<uint8_t> payload;
};

struct CoapMessage {
  uint8_t version{0};
  uint8_t type{0};
  uint8_t code{0};
  uint16_t message_id{0};
  std::vector<uint8_t> token;
  std::string uri_path;
  uint8_t src_addr{0};
  uint8_t dst_addr{0};
  bool has_src_addr{false};
  bool has_dst_addr{false};
  std::vector<uint8_t> payload;

  uint8_t code_class() const { return static_cast<uint8_t>(this->code >> 5); }
  uint8_t code_detail() const { return static_cast<uint8_t>(this->code & 0x1F); }
  bool is_success() const { return this->code_class() == 2; }
};

struct BootstrapReply {
  enum class Kind {
    UNKNOWN,
    MAX_FRAME_SIZE,
    TRANSPORT_FLAGS,
    CLIENT_ADDRESS,
    PIN_REQUIRED,
    AUTH_SUCCESS,
    AUTH_ERROR,
  };

  Kind kind{Kind::UNKNOWN};
  uint16_t value16{0};
  uint8_t value8{0};
};

struct FailureDetail {
  uint32_t trouble_code{0};
  bool is_error{true};  // < 500 = Error, >= 500 = Warning (PumpMessage.fromUserTroubleCode)
};

// Envelope & bootstrap frame builders
std::vector<uint8_t> build_wilo_envelope(uint8_t channel, const std::vector<uint8_t> &payload);
std::vector<uint8_t> build_bootstrap_mtu_req();
std::vector<uint8_t> build_bootstrap_flags_req();
std::vector<uint8_t> build_bootstrap_addr_req();
std::vector<uint8_t> build_pin_request_frame();
std::vector<uint8_t> build_auth_pin_frame(uint16_t pin);

// CoAP + CBOR frame builders
std::vector<uint8_t> encode_cbor_uint(uint64_t value);
std::vector<uint8_t> encode_cbor_query_keys(const std::vector<uint16_t> &keys);
std::vector<uint8_t> build_coap_envelope(uint8_t coap_code, uint16_t message_id, uint16_t primary_key,
                                         uint8_t client_addr, const std::vector<uint8_t> &cbor_payload,
                                         uint16_t uri_index = 0);
std::vector<uint8_t> build_query_frame(uint16_t message_id, const std::vector<uint16_t> &keys,
                                       uint8_t client_addr = 0x13);
// Indexed CoAP GET with CBOR argument payload (e.g. 0x0C8C FailureDetail for a failure ID from 0x0C8B)
std::vector<uint8_t> build_indexed_read_frame(uint16_t message_id, uint16_t key, uint64_t argument,
                                              uint8_t client_addr = 0x13);
// Generic CoAP write helper (CoAPWriteRequest -> 0.03 PUT with CBOR payload)
std::vector<uint8_t> build_write_frame(uint16_t message_id, uint16_t key, const std::vector<uint8_t> &cbor_payload,
                                       uint8_t client_addr = 0x13, uint8_t coap_code = COAP_CODE_PUT,
                                       uint16_t uri_index = 0);

// Parsers
bool parse_wilo_envelope(const uint8_t *data, size_t len, WiloEnvelope *out);
BootstrapReply parse_bootstrap_or_auth_reply(const WiloEnvelope &env);
bool parse_coap_message(const uint8_t *data, size_t len, CoapMessage *out);

// CBOR decoder & IEEE 754 binary16 helper
bool decode_cbor(const uint8_t *data, size_t len, CborValue *value);
float decode_half_float(uint16_t bits);

// Verified Wilo physical value decoders & encoders
bool is_invalid_raw_u32(uint64_t raw);
double decode_biased_u32_q10(uint64_t raw);
double decode_u16_q10(uint64_t raw);
double decode_biased_u16_q1(uint64_t raw);
bool decode_medium_temp_c(uint64_t raw, double *out_temp_c);
double decode_power_watts(uint64_t raw);
double decode_energy_kwh(uint64_t raw);
double decode_operating_hours(uint64_t raw);
double decode_setpoint_percent(uint64_t raw);
bool compute_physical_setpoint(uint64_t raw_setpoint, uint64_t control_mode, const CborValue &scale_map,
                               double *out_value);
bool encode_physical_setpoint(double physical_value, uint64_t control_mode, const CborValue &scale_map,
                              const CborValue *min_map, const CborValue *max_map, uint16_t *out_raw_q14);
bool parse_failure_detail(const CborValue &value, FailureDetail *out);
const char *trouble_code_description(uint32_t code);
std::string format_failure_summary(const std::vector<FailureDetail> &details, size_t unresolved_count);
std::string format_software_version(uint64_t raw);
std::string control_mode_to_string(uint64_t mode);
bool control_mode_from_string(const std::string &name, uint16_t *out_mode);

// JSON / hex formatting helpers
std::string cbor_to_json(const CborValue &value);
std::string key_map_to_json(const std::map<uint16_t, CborValue> &values);
std::string key_map_to_compact_json(const std::map<uint16_t, CborValue> &values, size_t max_len = 250,
                                    bool omit_uuid = true);
std::string escape_json(const std::string &value);
std::string key_to_hex(uint16_t key);
std::string bytes_to_hex(const uint8_t *data, size_t len);

}  // namespace wilo_protocol
