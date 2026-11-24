#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/button/button.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "wilo_protocol.h"

#ifdef USE_ESP32

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <esp_gattc_api.h>

namespace esphome {
namespace wilo_bt {

namespace espbt = esphome::esp32_ble_tracker;

class WiloBtComponent;

// Session authentication controls
class WiloSessionPinNumber : public number::Number, public Parented<WiloBtComponent> {
 public:
  WiloSessionPinNumber() = default;

 protected:
  void control(float value) override;
};

class WiloAuthenticateButton : public button::Button, public Parented<WiloBtComponent> {
 public:
  WiloAuthenticateButton() = default;

 protected:
  void press_action() override;
};

class WiloRequestPinButton : public button::Button, public Parented<WiloBtComponent> {
 public:
  WiloRequestPinButton() = default;

 protected:
  void press_action() override;
};

// Pump control entities (verified against Wilo-Assistant 6.13.0)
class WiloPumpRunSwitch : public switch_::Switch, public Parented<WiloBtComponent> {
 public:
  WiloPumpRunSwitch() = default;

 protected:
  void write_state(bool state) override;
};

class WiloNightSetbackSwitch : public switch_::Switch, public Parented<WiloBtComponent> {
 public:
  WiloNightSetbackSwitch() = default;

 protected:
  void write_state(bool state) override;
};

class WiloPressureIndependentValveSwitch : public switch_::Switch, public Parented<WiloBtComponent> {
 public:
  WiloPressureIndependentValveSwitch() = default;

 protected:
  void write_state(bool state) override;
};

class WiloAirVentingSwitch : public switch_::Switch, public Parented<WiloBtComponent> {
 public:
  WiloAirVentingSwitch() = default;

 protected:
  void write_state(bool state) override;
};

class WiloSetpointNumber : public number::Number, public Parented<WiloBtComponent> {
 public:
  WiloSetpointNumber() = default;

 protected:
  void control(float value) override;
};

class WiloSetpointPercentNumber : public number::Number, public Parented<WiloBtComponent> {
 public:
  WiloSetpointPercentNumber() = default;

 protected:
  void control(float value) override;
};

class WiloControlModeSelect : public select::Select, public Parented<WiloBtComponent> {
 public:
  WiloControlModeSelect() = default;

 protected:
  void control(const std::string &value) override;
};

class WiloManualRestartButton : public button::Button, public Parented<WiloBtComponent> {
 public:
  WiloManualRestartButton() = default;

 protected:
  void press_action() override;
};

class WiloResetEnergyButton : public button::Button, public Parented<WiloBtComponent> {
 public:
  WiloResetEnergyButton() = default;

 protected:
  void press_action() override;
};

class WiloBtComponent : public Component, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;
  void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) override;

  // Configuration setters
  void set_update_interval(uint32_t ms) { this->update_interval_ms_ = ms; }
  void set_config_poll_interval_ms(uint32_t ms) { this->config_poll_interval_ms_ = ms; }
  void set_debug_protocol(bool debug) { this->debug_protocol_ = debug; }
  void set_default_pin(uint16_t pin) {
    this->session_pin_ = pin;
    this->has_default_pin_ = true;
  }
  void set_auto_authenticate(bool auto_auth) { this->auto_authenticate_ = auto_auth; }

  // Auth controls
  void set_session_pin_number(WiloSessionPinNumber *num) { this->session_pin_number_ = num; }
  void set_authenticate_button(WiloAuthenticateButton *btn) { this->authenticate_button_ = btn; }
  void set_request_pin_button(WiloRequestPinButton *btn) { this->request_pin_button_ = btn; }

  // Pump controls
  void set_pump_run_switch(WiloPumpRunSwitch *sw) { this->pump_run_switch_ = sw; }
  void set_night_setback_switch(WiloNightSetbackSwitch *sw) { this->night_setback_switch_ = sw; }
  void set_pressure_independent_valve_switch(WiloPressureIndependentValveSwitch *sw) {
    this->pressure_independent_valve_switch_ = sw;
  }
  void set_air_venting_switch(WiloAirVentingSwitch *sw) { this->air_venting_switch_ = sw; }
  void set_setpoint_control_number(WiloSetpointNumber *num) { this->setpoint_control_number_ = num; }
  void set_setpoint_percent_control_number(WiloSetpointPercentNumber *num) {
    this->setpoint_percent_control_number_ = num;
  }
  void set_control_mode_select(WiloControlModeSelect *sel) { this->control_mode_select_ = sel; }
  void set_manual_restart_button(WiloManualRestartButton *btn) { this->manual_restart_button_ = btn; }
  void set_reset_energy_button(WiloResetEnergyButton *btn) { this->reset_energy_button_ = btn; }

  // Binary sensors
  void set_connected_binary_sensor(binary_sensor::BinarySensor *s) { this->connected_binary_sensor_ = s; }
  void set_authenticated_binary_sensor(binary_sensor::BinarySensor *s) { this->authenticated_binary_sensor_ = s; }
  void set_waiting_for_pin_binary_sensor(binary_sensor::BinarySensor *s) { this->waiting_for_pin_binary_sensor_ = s; }
  void set_active_fault_binary_sensor(binary_sensor::BinarySensor *s) { this->active_fault_binary_sensor_ = s; }
  void set_active_warning_binary_sensor(binary_sensor::BinarySensor *s) { this->active_warning_binary_sensor_ = s; }
  void set_air_venting_active_binary_sensor(binary_sensor::BinarySensor *s) {
    this->air_venting_active_binary_sensor_ = s;
  }

  // Numeric sensors
  void set_flow_rate_sensor(sensor::Sensor *s) { this->flow_rate_sensor_ = s; }
  void set_differential_pressure_sensor(sensor::Sensor *s) { this->differential_pressure_sensor_ = s; }
  void set_medium_temperature_sensor(sensor::Sensor *s) { this->medium_temperature_sensor_ = s; }
  void set_power_sensor(sensor::Sensor *s) { this->power_sensor_ = s; }
  void set_energy_sensor(sensor::Sensor *s) { this->energy_sensor_ = s; }
  void set_speed_sensor(sensor::Sensor *s) { this->speed_sensor_ = s; }
  void set_operating_hours_sensor(sensor::Sensor *s) { this->operating_hours_sensor_ = s; }
  void set_setpoint_sensor(sensor::Sensor *s) { this->setpoint_sensor_ = s; }
  void set_setpoint_percent_sensor(sensor::Sensor *s) { this->setpoint_percent_sensor_ = s; }
  void set_max_head_sensor(sensor::Sensor *s) { this->max_head_sensor_ = s; }
  void set_max_flow_sensor(sensor::Sensor *s) { this->max_flow_sensor_ = s; }
  void set_air_venting_remaining_sensor(sensor::Sensor *s) { this->air_venting_remaining_sensor_ = s; }
  void set_pump_status_code_sensor(sensor::Sensor *s) { this->pump_status_code_sensor_ = s; }
  void set_control_mode_code_sensor(sensor::Sensor *s) { this->control_mode_code_sensor_ = s; }

  // Text sensors
  void set_device_name_text_sensor(text_sensor::TextSensor *s) { this->device_name_text_sensor_ = s; }
  void set_pump_type_text_sensor(text_sensor::TextSensor *s) { this->pump_type_text_sensor_ = s; }
  void set_serial_number_text_sensor(text_sensor::TextSensor *s) { this->serial_number_text_sensor_ = s; }
  void set_software_version_text_sensor(text_sensor::TextSensor *s) { this->software_version_text_sensor_ = s; }
  void set_control_mode_text_sensor(text_sensor::TextSensor *s) { this->control_mode_text_sensor_ = s; }
  void set_active_alarms_text_sensor(text_sensor::TextSensor *s) { this->active_alarms_text_sensor_ = s; }
  void set_protocol_state_text_sensor(text_sensor::TextSensor *s) { this->protocol_state_text_sensor_ = s; }
  void set_raw_static_json_text_sensor(text_sensor::TextSensor *s) { this->raw_static_json_text_sensor_ = s; }
  void set_raw_live_json_text_sensor(text_sensor::TextSensor *s) { this->raw_live_json_text_sensor_ = s; }

  // Auth actions
  void set_session_pin(uint16_t pin);
  void trigger_authentication();
  void trigger_request_pin();

  // Verified pump control actions
  void command_pump_run(bool run);
  void command_night_setback(bool enabled);
  void command_pressure_independent_valve(bool enabled);
  void command_air_venting(bool start);
  void command_setpoint_meters(float meters);
  void command_setpoint_percent(float percent);
  void command_control_mode(const std::string &mode_name);
  void command_manual_restart();
  void command_reset_energy();

  // Generic low-level CoAP write API
  void queue_raw_write(uint16_t key, const std::vector<uint8_t> &cbor_payload,
                       uint8_t coap_code = wilo_protocol::COAP_CODE_PUT);

 protected:
  enum class ProtocolState {
    DISCONNECTED,
    CONNECTING,
    WAIT_ENCRYPTION,
    SUBSCRIBING_NOTIFY,
    SUBSCRIBING_INDICATE,
    FLOW_INIT,
    BOOTSTRAP,
    WAIT_PIN,
    AUTHENTICATING,
    READY,
  };

  enum class RequestBucket {
    STATIC_DATA,
    LIVE_DATA,
  };

  enum class RequestKind {
    QUERY,
    WRITE,
    FAILURE_DETAIL,
  };

  struct OutgoingFrame {
    std::vector<uint8_t> payload;
    uint16_t message_id{0};
    std::vector<uint16_t> query_keys;
    RequestBucket bucket{RequestBucket::STATIC_DATA};
    RequestKind kind{RequestKind::QUERY};
    uint64_t failure_id{0};
    uint32_t min_delay_ms{120};
    bool track_response{false};
    bool is_auth_frame{false};
    bool is_pin_request{false};
  };

  struct PendingRequest {
    std::vector<uint16_t> query_keys;
    RequestBucket bucket{RequestBucket::STATIC_DATA};
    RequestKind kind{RequestKind::QUERY};
    uint64_t failure_id{0};
    uint32_t sent_at_ms{0};
  };

  static const char *state_to_string_(ProtocolState state);

  void reset_session_state_(bool keep_cached_values);
  void set_protocol_state_(ProtocolState state);
  void publish_status_entities_();
  void publish_decoded_sensors_();
  void publish_alarm_entities_();
  void publish_raw_json_();

  bool discover_characteristics_();
  void start_subscriptions_();
  void begin_flow_init_();
  void start_bootstrap_();

  void queue_envelope_frame_(const std::vector<uint8_t> &payload, uint32_t delay_ms = 30, bool is_pin_request = false,
                             bool is_auth_frame = false);
  void queue_query_(const std::vector<uint16_t> &keys, RequestBucket bucket, bool high_priority = false);
  void queue_failure_detail_read_(uint64_t failure_id);
  void queue_authentication_frame_();
  void queue_write_readback_(uint16_t key);

  void maybe_schedule_polls_();
  void process_outgoing_queue_();
  void queue_rx_credit_grant_(uint8_t count = 1);
  void process_rx_credit_queue_();
  void expire_stale_requests_();

  void handle_notify_chunk_(const uint8_t *data, size_t len);
  void process_complete_envelope_(const wilo_protocol::WiloEnvelope &env);
  void handle_indicate_(const uint8_t *data, size_t len);
  void process_query_response_(uint16_t message_id, const wilo_protocol::CborValue &value);
  void store_values_in_bucket_(RequestBucket bucket, const std::vector<uint16_t> &query_keys,
                               const wilo_protocol::CborValue &value);
  void handle_active_alarms_list_(const wilo_protocol::CborValue &alarms);

  const wilo_protocol::CborValue *find_cached_value_(uint16_t key) const;
  bool send_tx_data_(const std::vector<uint8_t> &payload);
  bool send_tx_credit_grant_();

  ProtocolState protocol_state_{ProtocolState::DISCONNECTED};

  // Auth controls
  WiloSessionPinNumber *session_pin_number_{nullptr};
  WiloAuthenticateButton *authenticate_button_{nullptr};
  WiloRequestPinButton *request_pin_button_{nullptr};

  // Pump controls
  WiloPumpRunSwitch *pump_run_switch_{nullptr};
  WiloNightSetbackSwitch *night_setback_switch_{nullptr};
  WiloPressureIndependentValveSwitch *pressure_independent_valve_switch_{nullptr};
  WiloAirVentingSwitch *air_venting_switch_{nullptr};
  WiloSetpointNumber *setpoint_control_number_{nullptr};
  WiloSetpointPercentNumber *setpoint_percent_control_number_{nullptr};
  WiloControlModeSelect *control_mode_select_{nullptr};
  WiloManualRestartButton *manual_restart_button_{nullptr};
  WiloResetEnergyButton *reset_energy_button_{nullptr};

  // Binary sensors
  binary_sensor::BinarySensor *connected_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *authenticated_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *waiting_for_pin_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *active_fault_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *active_warning_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *air_venting_active_binary_sensor_{nullptr};

  // Numeric sensors
  sensor::Sensor *flow_rate_sensor_{nullptr};
  sensor::Sensor *differential_pressure_sensor_{nullptr};
  sensor::Sensor *medium_temperature_sensor_{nullptr};
  sensor::Sensor *power_sensor_{nullptr};
  sensor::Sensor *energy_sensor_{nullptr};
  sensor::Sensor *speed_sensor_{nullptr};
  sensor::Sensor *operating_hours_sensor_{nullptr};
  sensor::Sensor *setpoint_sensor_{nullptr};
  sensor::Sensor *setpoint_percent_sensor_{nullptr};
  sensor::Sensor *max_head_sensor_{nullptr};
  sensor::Sensor *max_flow_sensor_{nullptr};
  sensor::Sensor *air_venting_remaining_sensor_{nullptr};
  sensor::Sensor *pump_status_code_sensor_{nullptr};
  sensor::Sensor *control_mode_code_sensor_{nullptr};

  // Text sensors
  text_sensor::TextSensor *device_name_text_sensor_{nullptr};
  text_sensor::TextSensor *pump_type_text_sensor_{nullptr};
  text_sensor::TextSensor *serial_number_text_sensor_{nullptr};
  text_sensor::TextSensor *software_version_text_sensor_{nullptr};
  text_sensor::TextSensor *control_mode_text_sensor_{nullptr};
  text_sensor::TextSensor *active_alarms_text_sensor_{nullptr};
  text_sensor::TextSensor *protocol_state_text_sensor_{nullptr};
  text_sensor::TextSensor *raw_static_json_text_sensor_{nullptr};
  text_sensor::TextSensor *raw_live_json_text_sensor_{nullptr};

  // Discovered GATT handles
  uint16_t tx_data_handle_{0};
  uint16_t rx_data_handle_{0};
  uint16_t rx_credits_handle_{0};
  uint16_t tx_credits_handle_{0};
  uint16_t rx_data_cccd_handle_{0};
  uint16_t rx_credits_cccd_handle_{0};

  // Config
  uint32_t update_interval_ms_{5000};
  uint32_t config_poll_interval_ms_{60000};
  bool debug_protocol_{false};
  bool has_default_pin_{false};
  bool auto_authenticate_{false};

  // Runtime state
  uint16_t session_pin_{0};
  uint16_t next_message_id_{1};
  uint16_t max_frame_size_{180};
  uint8_t client_addr_{0x13};
  uint8_t tx_credits_{0};
  uint8_t initial_credit_writes_completed_{0};
  uint16_t pending_rx_credit_writes_{0};
  bool rx_credit_in_flight_{false};
  bool got_initial_tx_credits_{false};
  bool encryption_ready_{false};
  bool services_discovered_{false};
  bool auth_requested_{false};
  bool auto_auth_attempted_{false};
  bool static_poll_done_{false};
  bool air_venting_active_{false};

  // Timers
  uint32_t state_deadline_ms_{0};
  uint32_t next_tx_at_ms_{0};
  uint32_t next_live_poll_ms_{0};
  uint32_t next_config_poll_ms_{0};
  uint32_t rx_reassembly_started_ms_{0};

  // Queues & caches
  std::vector<uint8_t> rx_reassembly_buf_;
  std::deque<OutgoingFrame> outgoing_frames_;
  std::map<uint16_t, PendingRequest> pending_requests_;
  std::map<uint16_t, wilo_protocol::CborValue> static_values_;
  std::map<uint16_t, wilo_protocol::CborValue> live_values_;
  std::set<uint64_t> active_failure_ids_;
  std::map<uint64_t, wilo_protocol::FailureDetail> failure_details_;
};

}  // namespace wilo_bt
}  // namespace esphome

#endif
