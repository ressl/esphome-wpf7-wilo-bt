#include "wilo_bt.h"

#ifdef USE_ESP32

#include "esphome/components/esp32_ble/ble_uuid.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace esphome {
namespace wilo_bt {

static const char *const TAG = "wilo_bt";

static const espbt::ESPBTUUID SERVICE_UUID = espbt::ESPBTUUID::from_raw(wilo_protocol::WILO_SERVICE_UUID);
static const espbt::ESPBTUUID TX_DATA_UUID = espbt::ESPBTUUID::from_raw(wilo_protocol::WILO_TX_DATA_UUID);
static const espbt::ESPBTUUID RX_DATA_UUID = espbt::ESPBTUUID::from_raw(wilo_protocol::WILO_RX_DATA_UUID);
static const espbt::ESPBTUUID RX_CREDITS_UUID = espbt::ESPBTUUID::from_raw(wilo_protocol::WILO_RX_CREDITS_UUID);
static const espbt::ESPBTUUID TX_CREDITS_UUID = espbt::ESPBTUUID::from_raw(wilo_protocol::WILO_TX_CREDITS_UUID);

// Query groups verified against Wilo-Assistant 6.13.0 and live Stratos PICO
static const std::array<uint16_t, 6> METADATA_BATCH_KEYS = {
    wilo_protocol::KEY_DEVICE_NAME, wilo_protocol::KEY_PUMP_TYPE, wilo_protocol::KEY_PUMP_FAMILY,
    wilo_protocol::KEY_MAX_HEAD,    wilo_protocol::KEY_MAX_FLOW,  wilo_protocol::KEY_PUMP_OPERATION_MODE,
};

static const std::array<uint16_t, 4> STATIC_SINGLE_KEYS = {
    wilo_protocol::KEY_SOFTWARE_VERSION,
    wilo_protocol::KEY_SERIAL_NUMBER,
    wilo_protocol::KEY_DEVICE_UUID,
    wilo_protocol::KEY_ACTIVE_ALARMS,
};

// Polled periodically (0x06F5..0x06F7 return nested CBOR maps; 0x0C8B/0x060F/0x060A single GETs)
static const std::array<uint16_t, 8> CONFIG_SINGLE_KEYS = {
    wilo_protocol::KEY_CONTROL_MODE,
    wilo_protocol::KEY_SETPOINT_NORM,
    wilo_protocol::KEY_SETPOINT_MIN_MAP,
    wilo_protocol::KEY_SETPOINT_MAX_MAP,
    wilo_protocol::KEY_SETPOINT_SCALE_MAP,
    wilo_protocol::KEY_ACTIVE_ALARMS,
    wilo_protocol::KEY_AIR_VENTING_STATUS,
    wilo_protocol::KEY_AIR_VENTING_REMAINING,
};

static const std::array<uint16_t, 9> LIVE_BATCH_KEYS = {
    wilo_protocol::KEY_MAX_HEAD,          wilo_protocol::KEY_MAX_FLOW,     wilo_protocol::KEY_OPERATING_SECONDS,
    wilo_protocol::KEY_ACTUAL_HEAD,       wilo_protocol::KEY_ACTUAL_FLOW,  wilo_protocol::KEY_MOTOR_SPEED,
    wilo_protocol::KEY_MEDIUM_TEMP,       wilo_protocol::KEY_ELECTRICAL_POWER, wilo_protocol::KEY_ELECTRICAL_ENERGY,
};

static const std::array<uint16_t, 4> STATUS_BATCH_KEYS = {
    wilo_protocol::KEY_APPLICATION_TYPE,
    wilo_protocol::KEY_SUB_APPLICATION_TYPE,
    wilo_protocol::KEY_INSTALLATION_TYPE,
    wilo_protocol::KEY_PUMP_OPERATION_MODE,
};

static const std::array<uint16_t, 4> LIVE_SINGLE_KEYS = {
    wilo_protocol::KEY_NIGHT_SETBACK,
    wilo_protocol::KEY_PRESSURE_INDEPENDENT_VALVE,
    wilo_protocol::KEY_CONTROL_MODE,
    wilo_protocol::KEY_SETPOINT_NORM,
};

static constexpr uint32_t ENCRYPTION_WAIT_TIMEOUT_MS = 2500;
static constexpr uint32_t FLOW_INIT_TIMEOUT_MS = 2000;
static constexpr uint32_t BOOTSTRAP_TIMEOUT_MS = 2000;
static constexpr uint32_t AUTH_TIMEOUT_MS = 10000;
static constexpr uint32_t REQUEST_STALE_TIMEOUT_MS = 15000;
static constexpr uint32_t REASSEMBLY_TIMEOUT_MS = 2000;
static constexpr size_t MAX_OUTGOING_QUEUE_SIZE = 48;

// Auth entity callbacks
void WiloSessionPinNumber::control(float value) {
  const auto pin = static_cast<uint16_t>(std::lround(std::clamp(value, 0.0f, 9999.0f)));
  this->publish_state(pin);
  this->parent_->set_session_pin(pin);
}

void WiloAuthenticateButton::press_action() { this->parent_->trigger_authentication(); }

void WiloRequestPinButton::press_action() { this->parent_->trigger_request_pin(); }

// Pump control entity callbacks
void WiloPumpRunSwitch::write_state(bool state) { this->parent_->command_pump_run(state); }

void WiloNightSetbackSwitch::write_state(bool state) { this->parent_->command_night_setback(state); }

void WiloPressureIndependentValveSwitch::write_state(bool state) {
  this->parent_->command_pressure_independent_valve(state);
}

void WiloAirVentingSwitch::write_state(bool state) { this->parent_->command_air_venting(state); }

void WiloSetpointNumber::control(float value) { this->parent_->command_setpoint_meters(value); }

void WiloSetpointPercentNumber::control(float value) { this->parent_->command_setpoint_percent(value); }

void WiloControlModeSelect::control(const std::string &value) { this->parent_->command_control_mode(value); }

void WiloManualRestartButton::press_action() { this->parent_->command_manual_restart(); }

void WiloResetEnergyButton::press_action() { this->parent_->command_reset_energy(); }

const char *WiloBtComponent::state_to_string_(ProtocolState state) {
  switch (state) {
    case ProtocolState::DISCONNECTED:
      return "Disconnected";
    case ProtocolState::CONNECTING:
      return "Connecting";
    case ProtocolState::WAIT_ENCRYPTION:
      return "Encrypting";
    case ProtocolState::SUBSCRIBING_NOTIFY:
    case ProtocolState::SUBSCRIBING_INDICATE:
      return "Subscribing";
    case ProtocolState::FLOW_INIT:
      return "Flow Control Init";
    case ProtocolState::BOOTSTRAP:
      return "Bootstrap";
    case ProtocolState::WAIT_PIN:
      return "Waiting for PIN";
    case ProtocolState::AUTHENTICATING:
      return "Authenticating";
    case ProtocolState::READY:
      return "Ready";
  }
  return "Unknown";
}

void WiloBtComponent::setup() {
  this->reset_session_state_(true);
  if (this->session_pin_number_ != nullptr) {
    this->session_pin_number_->publish_state(this->session_pin_);
  }
  this->publish_status_entities_();
}

void WiloBtComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Wilo Smart Connect BT:");
  ESP_LOGCONFIG(TAG, "  Update Interval: %u ms", this->update_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Config Poll Interval: %u ms", this->config_poll_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Debug Protocol: %s", YESNO(this->debug_protocol_));
  LOG_NUMBER("  ", "Session PIN", this->session_pin_number_);
  LOG_BUTTON("  ", "Authenticate", this->authenticate_button_);
  LOG_BUTTON("  ", "Request PIN", this->request_pin_button_);
  LOG_SWITCH("  ", "Pump Run", this->pump_run_switch_);
  LOG_SWITCH("  ", "Night Setback", this->night_setback_switch_);
  LOG_SWITCH("  ", "Pressure Independent Valve", this->pressure_independent_valve_switch_);
  LOG_SWITCH("  ", "Air Venting", this->air_venting_switch_);
  LOG_NUMBER("  ", "Setpoint Control", this->setpoint_control_number_);
  LOG_NUMBER("  ", "Setpoint Percent Control", this->setpoint_percent_control_number_);
  LOG_SELECT("  ", "Control Mode Select", this->control_mode_select_);
  LOG_BUTTON("  ", "Manual Restart", this->manual_restart_button_);
  LOG_BUTTON("  ", "Reset Energy", this->reset_energy_button_);
  LOG_BINARY_SENSOR("  ", "Connected", this->connected_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Authenticated", this->authenticated_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Waiting For PIN", this->waiting_for_pin_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Active Fault", this->active_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Active Warning", this->active_warning_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Air Venting Active", this->air_venting_active_binary_sensor_);
  LOG_SENSOR("  ", "Flow Rate", this->flow_rate_sensor_);
  LOG_SENSOR("  ", "Differential Pressure", this->differential_pressure_sensor_);
  LOG_SENSOR("  ", "Medium Temperature", this->medium_temperature_sensor_);
  LOG_SENSOR("  ", "Power", this->power_sensor_);
  LOG_SENSOR("  ", "Energy", this->energy_sensor_);
  LOG_SENSOR("  ", "Speed", this->speed_sensor_);
  LOG_SENSOR("  ", "Operating Hours", this->operating_hours_sensor_);
  LOG_SENSOR("  ", "Setpoint", this->setpoint_sensor_);
  LOG_SENSOR("  ", "Setpoint Percent", this->setpoint_percent_sensor_);
  LOG_SENSOR("  ", "Max Head", this->max_head_sensor_);
  LOG_SENSOR("  ", "Max Flow", this->max_flow_sensor_);
  LOG_SENSOR("  ", "Air Venting Remaining", this->air_venting_remaining_sensor_);
  LOG_SENSOR("  ", "Pump Status Code", this->pump_status_code_sensor_);
  LOG_SENSOR("  ", "Control Mode Code", this->control_mode_code_sensor_);
  LOG_TEXT_SENSOR("  ", "Device Name", this->device_name_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Pump Type", this->pump_type_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Serial Number", this->serial_number_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Software Version", this->software_version_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Control Mode", this->control_mode_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Active Alarms", this->active_alarms_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Protocol State", this->protocol_state_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Raw Static JSON", this->raw_static_json_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Raw Live JSON", this->raw_live_json_text_sensor_);
}

void WiloBtComponent::loop() {
  const uint32_t now = millis();

  if (!this->rx_reassembly_buf_.empty() && (now - this->rx_reassembly_started_ms_) > REASSEMBLY_TIMEOUT_MS) {
    ESP_LOGW(TAG, "Discarding incomplete Wilo frame (%u bytes)", static_cast<unsigned>(this->rx_reassembly_buf_.size()));
    this->rx_reassembly_buf_.clear();
  }

  switch (this->protocol_state_) {
    case ProtocolState::WAIT_ENCRYPTION:
      if (this->encryption_ready_ || (this->state_deadline_ms_ != 0 && now >= this->state_deadline_ms_)) {
        if (!this->encryption_ready_) {
          ESP_LOGD(TAG, "Proceeding to GATT subscriptions after encryption wait timeout.");
        }
        this->start_subscriptions_();
      }
      break;
    case ProtocolState::FLOW_INIT:
      if (this->got_initial_tx_credits_ &&
          this->initial_credit_writes_completed_ >= wilo_protocol::WILO_INITIAL_RX_CREDIT_WRITES &&
          !this->rx_credit_in_flight_) {
        this->start_bootstrap_();
      } else if (this->state_deadline_ms_ != 0 && now >= this->state_deadline_ms_) {
        if (!this->got_initial_tx_credits_) {
          ESP_LOGW(TAG, "No initial TX credit indication received; seeding %u fallback credits.",
                   wilo_protocol::WILO_FALLBACK_TX_CREDITS);
          this->tx_credits_ = wilo_protocol::WILO_FALLBACK_TX_CREDITS;
          this->got_initial_tx_credits_ = true;
        }
        this->start_bootstrap_();
      }
      break;
    case ProtocolState::BOOTSTRAP:
      if (this->state_deadline_ms_ != 0 && now >= this->state_deadline_ms_) {
        ESP_LOGI(TAG, "Bootstrap complete (client_addr=0x%02X, max_frame=%u). Waiting for session PIN.",
                 this->client_addr_, this->max_frame_size_);
        this->state_deadline_ms_ = 0;
        this->set_protocol_state_(ProtocolState::WAIT_PIN);
      }
      break;
    case ProtocolState::AUTHENTICATING:
      if (this->state_deadline_ms_ != 0 && now >= this->state_deadline_ms_) {
        ESP_LOGW(TAG, "Session PIN authentication timed out. Check the PIN on the pump display and retry.");
        this->state_deadline_ms_ = 0;
        this->set_protocol_state_(ProtocolState::WAIT_PIN);
      }
      break;
    default:
      break;
  }

  this->process_rx_credit_queue_();
  this->maybe_schedule_polls_();
  this->process_outgoing_queue_();
  this->expire_stale_requests_();
}

void WiloBtComponent::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                          esp_ble_gattc_cb_param_t *param) {
  auto *client = this->parent();
  if (client == nullptr) {
    return;
  }

  switch (event) {
    case ESP_GATTC_CONNECT_EVT: {
      if (!client->check_addr(param->connect.remote_bda)) {
        return;
      }
      this->reset_session_state_(true);
      this->set_protocol_state_(ProtocolState::CONNECTING);
      this->encryption_ready_ = client->is_paired();
      if (!this->encryption_ready_) {
        const esp_err_t err = client->pair();
        if (err != ESP_OK) {
          ESP_LOGW(TAG, "[%s] BLE link encryption request failed (%d).", client->address_str(), err);
        }
      }
      break;
    }
    case ESP_GATTC_OPEN_EVT: {
      if (!client->check_addr(param->open.remote_bda)) {
        return;
      }
      if (param->open.status != ESP_GATT_OK && param->open.status != ESP_GATT_ALREADY_OPEN) {
        ESP_LOGW(TAG, "[%s] Connection open failed (status=%d).", client->address_str(), param->open.status);
        this->reset_session_state_(true);
      }
      break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      if (client->get_conn_id() != param->search_cmpl.conn_id) {
        return;
      }
      // Hold GATT cache until both CCCD writes finish
      this->node_state = espbt::ClientState::CONNECTED;
      if (!this->discover_characteristics_()) {
        ESP_LOGE(TAG, "[%s] Required Wilo GATT characteristics (%s) not found.", client->address_str(),
                 wilo_protocol::WILO_SERVICE_UUID);
        this->node_state = espbt::ClientState::ESTABLISHED;
        client->disconnect();
        return;
      }
      this->services_discovered_ = true;
      if (this->encryption_ready_ || client->is_paired()) {
        this->encryption_ready_ = true;
        this->start_subscriptions_();
      } else {
        this->set_protocol_state_(ProtocolState::WAIT_ENCRYPTION);
        this->state_deadline_ms_ = millis() + ENCRYPTION_WAIT_TIMEOUT_MS;
      }
      break;
    }
    case ESP_GATTC_WRITE_DESCR_EVT: {
      if (client->get_conn_id() != param->write.conn_id) {
        return;
      }
      if (param->write.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "[%s] CCCD write failed for handle 0x%04X (status=0x%02X).", client->address_str(),
                 param->write.handle, param->write.status);
        if ((param->write.status == ESP_GATT_INSUF_AUTHENTICATION ||
             param->write.status == ESP_GATT_INSUF_ENCRYPTION) &&
            !this->encryption_ready_) {
          client->pair();
          this->set_protocol_state_(ProtocolState::WAIT_ENCRYPTION);
          this->state_deadline_ms_ = millis() + ENCRYPTION_WAIT_TIMEOUT_MS;
        }
        return;
      }
      if (param->write.handle == this->rx_data_cccd_handle_ &&
          this->protocol_state_ == ProtocolState::SUBSCRIBING_NOTIFY) {
        this->set_protocol_state_(ProtocolState::SUBSCRIBING_INDICATE);
        const esp_err_t err = client->register_for_notify(this->rx_credits_handle_);
        if (err != ESP_OK) {
          ESP_LOGE(TAG, "[%s] Failed to register for credit indications (%d).", client->address_str(), err);
          this->node_state = espbt::ClientState::ESTABLISHED;
          client->disconnect();
        }
      } else if (param->write.handle == this->rx_credits_cccd_handle_ &&
                 this->protocol_state_ == ProtocolState::SUBSCRIBING_INDICATE) {
        // Both CCCDs are written; safe for BLEClient to release GATT cache now
        this->node_state = espbt::ClientState::ESTABLISHED;
        this->begin_flow_init_();
      }
      break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
      if (client->get_conn_id() != param->notify.conn_id) {
        return;
      }
      if (param->notify.handle == this->rx_data_handle_) {
        this->handle_notify_chunk_(param->notify.value, param->notify.value_len);
        this->queue_rx_credit_grant_(1);
      } else if (param->notify.handle == this->rx_credits_handle_) {
        this->handle_indicate_(param->notify.value, param->notify.value_len);
      }
      break;
    }
    case ESP_GATTC_WRITE_CHAR_EVT: {
      if (client->get_conn_id() != param->write.conn_id) {
        return;
      }
      if (param->write.handle == this->tx_credits_handle_) {
        this->rx_credit_in_flight_ = false;
        if (param->write.status != ESP_GATT_OK) {
          ESP_LOGW(TAG, "[%s] RX credit write failed (status=0x%02X).", client->address_str(), param->write.status);
        } else if (this->protocol_state_ == ProtocolState::FLOW_INIT) {
          this->initial_credit_writes_completed_++;
          if (this->got_initial_tx_credits_ &&
              this->initial_credit_writes_completed_ < wilo_protocol::WILO_INITIAL_RX_CREDIT_WRITES) {
            this->queue_rx_credit_grant_(1);
          }
        }
        this->process_rx_credit_queue_();
      }
      break;
    }
    case ESP_GATTC_DISCONNECT_EVT: {
      if (!client->check_addr(param->disconnect.remote_bda)) {
        return;
      }
      ESP_LOGI(TAG, "[%s] Disconnected (reason=0x%02X).", client->address_str(), param->disconnect.reason);
      this->reset_session_state_(true);
      break;
    }
    case ESP_GATTC_CLOSE_EVT: {
      if (client->get_conn_id() != param->close.conn_id) {
        return;
      }
      this->reset_session_state_(true);
      break;
    }
    default:
      break;
  }
}

void WiloBtComponent::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  auto *client = this->parent();
  if (client == nullptr) {
    return;
  }
  if (event == ESP_GAP_BLE_AUTH_CMPL_EVT && client->check_addr(param->ble_security.auth_cmpl.bd_addr)) {
    if (param->ble_security.auth_cmpl.success) {
      this->encryption_ready_ = true;
      ESP_LOGI(TAG, "[%s] BLE link encryption established.", client->address_str());
      if (this->services_discovered_ && this->protocol_state_ == ProtocolState::WAIT_ENCRYPTION) {
        this->start_subscriptions_();
      }
    } else {
      ESP_LOGW(TAG, "[%s] BLE link encryption failed (reason=0x%02X).", client->address_str(),
               param->ble_security.auth_cmpl.fail_reason);
    }
  }
}

void WiloBtComponent::set_session_pin(uint16_t pin) { this->session_pin_ = pin; }

void WiloBtComponent::trigger_authentication() {
  if (this->protocol_state_ == ProtocolState::DISCONNECTED) {
    ESP_LOGW(TAG, "Cannot authenticate while disconnected.");
    return;
  }
  if (this->protocol_state_ == ProtocolState::READY) {
    ESP_LOGI(TAG, "Already authenticated.");
    return;
  }
  if (this->protocol_state_ == ProtocolState::AUTHENTICATING) {
    ESP_LOGI(TAG, "Authentication already in flight.");
    return;
  }
  this->auth_requested_ = true;
  if (this->protocol_state_ == ProtocolState::WAIT_PIN) {
    this->queue_authentication_frame_();
  } else {
    ESP_LOGI(TAG, "Authentication queued; waiting for bootstrap to finish.");
  }
}

void WiloBtComponent::trigger_request_pin() {
  if (this->protocol_state_ == ProtocolState::DISCONNECTED || this->tx_data_handle_ == 0) {
    ESP_LOGW(TAG, "Cannot request PIN while disconnected.");
    return;
  }
  ESP_LOGI(TAG, "Re-sending session PIN request to pump display.");
  this->queue_envelope_frame_(wilo_protocol::build_pin_request_frame(), 50, true, false);
}

void WiloBtComponent::command_pump_run(bool run) {
  if (this->protocol_state_ != ProtocolState::READY) {
    ESP_LOGW(TAG, "Ignoring pump_run=%s: session not ready.", ONOFF(run));
    this->publish_decoded_sensors_();
    return;
  }
  ESP_LOGI(TAG, "Command: Pump Run -> %s (0x0669=%u)", ONOFF(run), run ? 1 : 0);
  if (this->pump_run_switch_ != nullptr) {
    this->pump_run_switch_->publish_state(run);
  }
  this->queue_raw_write(wilo_protocol::KEY_PUMP_OPERATION_MODE, wilo_protocol::encode_cbor_uint(run ? 1 : 0));
}

void WiloBtComponent::command_night_setback(bool enabled) {
  if (this->protocol_state_ != ProtocolState::READY) {
    ESP_LOGW(TAG, "Ignoring night_setback=%s: session not ready.", ONOFF(enabled));
    this->publish_decoded_sensors_();
    return;
  }
  ESP_LOGI(TAG, "Command: Night Setback -> %s (0x025A=%u)", ONOFF(enabled), enabled ? 1 : 0);
  if (this->night_setback_switch_ != nullptr) {
    this->night_setback_switch_->publish_state(enabled);
  }
  this->queue_raw_write(wilo_protocol::KEY_NIGHT_SETBACK, wilo_protocol::encode_cbor_uint(enabled ? 1 : 0));
}

void WiloBtComponent::command_pressure_independent_valve(bool enabled) {
  if (this->protocol_state_ != ProtocolState::READY) {
    ESP_LOGW(TAG, "Ignoring pressure_independent_valve=%s: session not ready.", ONOFF(enabled));
    this->publish_decoded_sensors_();
    return;
  }
  ESP_LOGI(TAG, "Command: Pressure Independent Valve -> %s (0x0191=%u)", ONOFF(enabled), enabled ? 1 : 0);
  if (this->pressure_independent_valve_switch_ != nullptr) {
    this->pressure_independent_valve_switch_->publish_state(enabled);
  }
  this->queue_raw_write(wilo_protocol::KEY_PRESSURE_INDEPENDENT_VALVE,
                        wilo_protocol::encode_cbor_uint(enabled ? 1 : 0));
}

void WiloBtComponent::command_air_venting(bool start) {
  if (this->protocol_state_ != ProtocolState::READY) {
    ESP_LOGW(TAG, "Ignoring air_venting=%s: session not ready.", ONOFF(start));
    this->publish_decoded_sensors_();
    return;
  }
  ESP_LOGI(TAG, "Command: Air Venting -> %s (0x0605=%u)", start ? "START" : "STOP", start ? 1 : 0);
  if (this->air_venting_switch_ != nullptr) {
    this->air_venting_switch_->publish_state(start);
  }
  this->queue_raw_write(wilo_protocol::KEY_AIR_VENTING, wilo_protocol::encode_cbor_uint(start ? 1 : 0));
}

void WiloBtComponent::command_setpoint_meters(float meters) {
  if (this->protocol_state_ != ProtocolState::READY) {
    ESP_LOGW(TAG, "Ignoring setpoint=%.2f m: session not ready.", meters);
    this->publish_decoded_sensors_();
    return;
  }
  const auto *mode_val = this->find_cached_value_(wilo_protocol::KEY_CONTROL_MODE);
  const auto *scale_map = this->find_cached_value_(wilo_protocol::KEY_SETPOINT_SCALE_MAP);
  const auto *min_map = this->find_cached_value_(wilo_protocol::KEY_SETPOINT_MIN_MAP);
  const auto *max_map = this->find_cached_value_(wilo_protocol::KEY_SETPOINT_MAX_MAP);

  if (mode_val == nullptr || mode_val->type != wilo_protocol::CborValue::Type::UNSIGNED || scale_map == nullptr) {
    ESP_LOGW(TAG, "Cannot write setpoint in meters yet: control mode or scale map (0x06F7) not loaded.");
    return;
  }
  const uint64_t mode = mode_val->unsigned_value;
  if (mode != 8 && mode != 9 && mode != 10 && mode != 16 && mode != 17) {
    ESP_LOGW(TAG, "Setpoint in meters is only valid in dp-c/dp-v modes (current mode=%llu). Use setpoint_percent_control.",
             static_cast<unsigned long long>(mode));
    this->publish_decoded_sensors_();
    return;
  }

  uint16_t raw_q14 = 0;
  if (!wilo_protocol::encode_physical_setpoint(meters, mode, *scale_map, min_map, max_map, &raw_q14)) {
    ESP_LOGW(TAG, "Failed to encode setpoint=%.2f m for mode=%llu.", meters, static_cast<unsigned long long>(mode));
    this->publish_decoded_sensors_();
    return;
  }

  double actual_m = 0.0;
  if (wilo_protocol::compute_physical_setpoint(raw_q14, mode, *scale_map, &actual_m) &&
      this->setpoint_control_number_ != nullptr) {
    // Snap to 0.1 m step so Home Assistant number box validation stays clean
    const float snapped_m = std::round(static_cast<float>(actual_m) * 10.0f) / 10.0f;
    this->setpoint_control_number_->publish_state(snapped_m);
  }
  ESP_LOGI(TAG, "Command: Setpoint -> %.2f m (mode=%llu, 0x0079=%u)", meters, static_cast<unsigned long long>(mode),
           raw_q14);
  this->queue_raw_write(wilo_protocol::KEY_SETPOINT_NORM, wilo_protocol::encode_cbor_uint(raw_q14));
}

void WiloBtComponent::command_setpoint_percent(float percent) {
  if (this->protocol_state_ != ProtocolState::READY) {
    ESP_LOGW(TAG, "Ignoring setpoint=%.1f%%: session not ready.", percent);
    this->publish_decoded_sensors_();
    return;
  }
  const auto *mode_val = this->find_cached_value_(wilo_protocol::KEY_CONTROL_MODE);
  const auto *min_map = this->find_cached_value_(wilo_protocol::KEY_SETPOINT_MIN_MAP);
  const auto *max_map = this->find_cached_value_(wilo_protocol::KEY_SETPOINT_MAX_MAP);

  double min_q14 = 0.0;
  double max_q14 = 16384.0;
  if (mode_val != nullptr && mode_val->type == wilo_protocol::CborValue::Type::UNSIGNED) {
    const uint64_t mode = mode_val->unsigned_value;
    if (mode == 40) {
      ESP_LOGW(TAG, "Dynamic Adapt mode has no manual setpoint; ignoring setpoint=%.1f%%.", percent);
      this->publish_decoded_sensors_();
      return;
    }
    if (min_map != nullptr) {
      if (const auto *e = min_map->find_uint_key(mode); e != nullptr) {
        double v = 0.0;
        if (e->as_number(&v) && v >= 0.0 && v <= 16384.0) {
          min_q14 = v;
        }
      }
    }
    if (max_map != nullptr) {
      if (const auto *e = max_map->find_uint_key(mode); e != nullptr) {
        double v = 0.0;
        if (e->as_number(&v) && v >= min_q14 && v <= 16384.0) {
          max_q14 = v;
        }
      }
    }
  }

  const double raw_f = std::round((static_cast<double>(std::clamp(percent, 0.0f, 100.0f)) / 100.0) * 16384.0);
  const auto raw_q14 = static_cast<uint16_t>(std::clamp(raw_f, min_q14, max_q14));
  if (this->setpoint_percent_control_number_ != nullptr) {
    const float snapped_pct = std::round(static_cast<float>(wilo_protocol::decode_setpoint_percent(raw_q14)));
    this->setpoint_percent_control_number_->publish_state(snapped_pct);
  }
  ESP_LOGI(TAG, "Command: Setpoint Percent -> %.1f%% (0x0079=%u)", percent, raw_q14);
  this->queue_raw_write(wilo_protocol::KEY_SETPOINT_NORM, wilo_protocol::encode_cbor_uint(raw_q14));
}

void WiloBtComponent::command_control_mode(const std::string &mode_name) {
  if (this->protocol_state_ != ProtocolState::READY) {
    ESP_LOGW(TAG, "Ignoring control_mode='%s': session not ready.", mode_name.c_str());
    this->publish_decoded_sensors_();
    return;
  }
  uint16_t mode_id = 0;
  if (!wilo_protocol::control_mode_from_string(mode_name, &mode_id)) {
    ESP_LOGW(TAG, "Unsupported control mode option '%s'.", mode_name.c_str());
    this->publish_decoded_sensors_();
    return;
  }
  ESP_LOGI(TAG, "Command: Control Mode -> '%s' (0x0001=%u)", mode_name.c_str(), mode_id);
  if (this->control_mode_select_ != nullptr) {
    this->control_mode_select_->publish_state(mode_name);
  }
  this->queue_raw_write(wilo_protocol::KEY_CONTROL_MODE, wilo_protocol::encode_cbor_uint(mode_id));
}

void WiloBtComponent::command_manual_restart() {
  if (this->protocol_state_ != ProtocolState::READY) {
    ESP_LOGW(TAG, "Ignoring manual_restart: session not ready.");
    return;
  }
  ESP_LOGI(TAG, "Command: Manual Restart (0x05C9=1)");
  this->queue_raw_write(wilo_protocol::KEY_MANUAL_RESTART, wilo_protocol::encode_cbor_uint(1));
}

void WiloBtComponent::command_reset_energy() {
  if (this->protocol_state_ != ProtocolState::READY) {
    ESP_LOGW(TAG, "Ignoring reset_energy: session not ready.");
    return;
  }
  ESP_LOGI(TAG, "Command: Reset Energy Meter (0x0A22=0)");
  this->queue_raw_write(wilo_protocol::KEY_RESET_ENERGY, wilo_protocol::encode_cbor_uint(0));
}

void WiloBtComponent::queue_raw_write(uint16_t key, const std::vector<uint8_t> &cbor_payload, uint8_t coap_code) {
  if (this->protocol_state_ != ProtocolState::READY) {
    ESP_LOGW(TAG, "Ignoring write to 0x%04X: session not ready.", key);
    return;
  }
  OutgoingFrame frame;
  frame.message_id = this->next_message_id_++;
  frame.query_keys = {key};
  frame.bucket = RequestBucket::STATIC_DATA;
  frame.kind = RequestKind::WRITE;
  frame.track_response = true;
  frame.min_delay_ms = 150;
  frame.payload = wilo_protocol::build_write_frame(frame.message_id, key, cbor_payload, this->client_addr_, coap_code);
  // Prioritize user control writes ahead of queued background telemetry polls
  this->outgoing_frames_.push_front(std::move(frame));
}

void WiloBtComponent::queue_write_readback_(uint16_t key) {
  switch (key) {
    case wilo_protocol::KEY_CONTROL_MODE:
      this->queue_query_({wilo_protocol::KEY_CONTROL_MODE}, RequestBucket::LIVE_DATA, true);
      this->queue_query_({wilo_protocol::KEY_SETPOINT_NORM}, RequestBucket::LIVE_DATA, true);
      this->queue_query_({wilo_protocol::KEY_SETPOINT_MIN_MAP}, RequestBucket::STATIC_DATA, false);
      this->queue_query_({wilo_protocol::KEY_SETPOINT_MAX_MAP}, RequestBucket::STATIC_DATA, false);
      this->queue_query_({wilo_protocol::KEY_SETPOINT_SCALE_MAP}, RequestBucket::STATIC_DATA, false);
      break;
    case wilo_protocol::KEY_AIR_VENTING:
      this->queue_query_({wilo_protocol::KEY_AIR_VENTING_STATUS}, RequestBucket::STATIC_DATA, true);
      this->queue_query_({wilo_protocol::KEY_AIR_VENTING_REMAINING}, RequestBucket::STATIC_DATA, true);
      break;
    case wilo_protocol::KEY_MANUAL_RESTART:
      this->queue_query_({wilo_protocol::KEY_MANUAL_RESTART_STATUS}, RequestBucket::STATIC_DATA, true);
      break;
    case wilo_protocol::KEY_RESET_ENERGY:
      this->queue_query_({wilo_protocol::KEY_ELECTRICAL_ENERGY}, RequestBucket::LIVE_DATA, true);
      break;
    default:
      this->queue_query_({key}, RequestBucket::LIVE_DATA, true);
      break;
  }
}

void WiloBtComponent::reset_session_state_(bool keep_cached_values) {
  this->protocol_state_ = ProtocolState::DISCONNECTED;
  this->tx_data_handle_ = 0;
  this->rx_data_handle_ = 0;
  this->rx_credits_handle_ = 0;
  this->tx_credits_handle_ = 0;
  this->rx_data_cccd_handle_ = 0;
  this->rx_credits_cccd_handle_ = 0;
  this->next_message_id_ = 1;
  this->max_frame_size_ = 180;
  this->client_addr_ = 0x13;
  this->tx_credits_ = 0;
  this->initial_credit_writes_completed_ = 0;
  this->pending_rx_credit_writes_ = 0;
  this->rx_credit_in_flight_ = false;
  this->got_initial_tx_credits_ = false;
  this->encryption_ready_ = false;
  this->services_discovered_ = false;
  this->auth_requested_ = false;
  this->auto_auth_attempted_ = false;
  this->static_poll_done_ = false;
  this->air_venting_active_ = false;
  this->state_deadline_ms_ = 0;
  this->next_tx_at_ms_ = 0;
  this->next_live_poll_ms_ = 0;
  this->next_config_poll_ms_ = 0;
  this->rx_reassembly_started_ms_ = 0;
  this->rx_reassembly_buf_.clear();
  this->outgoing_frames_.clear();
  this->pending_requests_.clear();
  if (!keep_cached_values) {
    this->static_values_.clear();
    this->live_values_.clear();
    this->active_failure_ids_.clear();
    this->failure_details_.clear();
  }
  this->publish_status_entities_();
}

void WiloBtComponent::set_protocol_state_(ProtocolState state) {
  if (this->protocol_state_ == state) {
    return;
  }
  this->protocol_state_ = state;
  ESP_LOGD(TAG, "Protocol state -> %s", state_to_string_(state));
  this->publish_status_entities_();
}

void WiloBtComponent::publish_status_entities_() {
  const bool is_connected = this->protocol_state_ != ProtocolState::DISCONNECTED;
  const bool is_authenticated = this->protocol_state_ == ProtocolState::READY;
  const bool is_waiting_pin = this->protocol_state_ == ProtocolState::WAIT_PIN;

  if (this->connected_binary_sensor_ != nullptr) {
    this->connected_binary_sensor_->publish_state(is_connected);
  }
  if (this->authenticated_binary_sensor_ != nullptr) {
    this->authenticated_binary_sensor_->publish_state(is_authenticated);
  }
  if (this->waiting_for_pin_binary_sensor_ != nullptr) {
    this->waiting_for_pin_binary_sensor_->publish_state(is_waiting_pin);
  }
  if (this->protocol_state_text_sensor_ != nullptr) {
    this->protocol_state_text_sensor_->publish_state(state_to_string_(this->protocol_state_));
  }
}

bool WiloBtComponent::discover_characteristics_() {
  auto *client = this->parent();
  auto *tx_data = client->get_characteristic(SERVICE_UUID, TX_DATA_UUID);
  auto *rx_data = client->get_characteristic(SERVICE_UUID, RX_DATA_UUID);
  auto *rx_credits = client->get_characteristic(SERVICE_UUID, RX_CREDITS_UUID);
  auto *tx_credits = client->get_characteristic(SERVICE_UUID, TX_CREDITS_UUID);
  if (tx_data == nullptr || rx_data == nullptr || rx_credits == nullptr || tx_credits == nullptr) {
    return false;
  }

  auto *rx_data_cccd = client->get_config_descriptor(rx_data->handle);
  auto *rx_credits_cccd = client->get_config_descriptor(rx_credits->handle);
  if (rx_data_cccd == nullptr || rx_credits_cccd == nullptr) {
    return false;
  }

  this->tx_data_handle_ = tx_data->handle;
  this->rx_data_handle_ = rx_data->handle;
  this->rx_credits_handle_ = rx_credits->handle;
  this->tx_credits_handle_ = tx_credits->handle;
  this->rx_data_cccd_handle_ = rx_data_cccd->handle;
  this->rx_credits_cccd_handle_ = rx_credits_cccd->handle;

  ESP_LOGD(TAG, "Discovered Wilo handles: tx=0x%04X rx=0x%04X(cccd=0x%04X) ind=0x%04X(cccd=0x%04X) cred=0x%04X",
           this->tx_data_handle_, this->rx_data_handle_, this->rx_data_cccd_handle_, this->rx_credits_handle_,
           this->rx_credits_cccd_handle_, this->tx_credits_handle_);
  return true;
}

void WiloBtComponent::start_subscriptions_() {
  this->state_deadline_ms_ = 0;
  this->set_protocol_state_(ProtocolState::SUBSCRIBING_NOTIFY);
  const esp_err_t err = this->parent()->register_for_notify(this->rx_data_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register for data notifications (%d).", err);
    this->node_state = espbt::ClientState::ESTABLISHED;
    this->parent()->disconnect();
  }
}

void WiloBtComponent::begin_flow_init_() {
  this->set_protocol_state_(ProtocolState::FLOW_INIT);
  this->initial_credit_writes_completed_ = 0;
  this->got_initial_tx_credits_ = false;
  this->tx_credits_ = 0;
  this->state_deadline_ms_ = millis() + FLOW_INIT_TIMEOUT_MS;
  this->queue_rx_credit_grant_(1);
  this->process_rx_credit_queue_();
}

void WiloBtComponent::start_bootstrap_() {
  this->outgoing_frames_.clear();
  this->pending_requests_.clear();
  this->set_protocol_state_(ProtocolState::BOOTSTRAP);
  this->state_deadline_ms_ = millis() + BOOTSTRAP_TIMEOUT_MS;

  this->queue_envelope_frame_(wilo_protocol::build_bootstrap_mtu_req(), 25);
  this->queue_envelope_frame_(wilo_protocol::build_bootstrap_flags_req(), 25);
  this->queue_envelope_frame_(wilo_protocol::build_bootstrap_addr_req(), 25);
  this->queue_envelope_frame_(wilo_protocol::build_pin_request_frame(), 50, true, false);
}

void WiloBtComponent::queue_envelope_frame_(const std::vector<uint8_t> &payload, uint32_t delay_ms, bool is_pin_request,
                                            bool is_auth_frame) {
  OutgoingFrame frame;
  frame.payload = payload;
  frame.min_delay_ms = delay_ms;
  frame.is_pin_request = is_pin_request;
  frame.is_auth_frame = is_auth_frame;
  this->outgoing_frames_.push_back(std::move(frame));
}

void WiloBtComponent::queue_query_(const std::vector<uint16_t> &keys, RequestBucket bucket, bool high_priority) {
  if (keys.empty() || (!high_priority && this->outgoing_frames_.size() >= MAX_OUTGOING_QUEUE_SIZE)) {
    return;
  }
  OutgoingFrame frame;
  frame.message_id = this->next_message_id_++;
  frame.query_keys = keys;
  frame.bucket = bucket;
  frame.kind = RequestKind::QUERY;
  frame.track_response = true;
  frame.min_delay_ms = 120;
  frame.payload = wilo_protocol::build_query_frame(frame.message_id, keys, this->client_addr_);
  if (high_priority) {
    this->outgoing_frames_.push_front(std::move(frame));
  } else {
    this->outgoing_frames_.push_back(std::move(frame));
  }
}

void WiloBtComponent::queue_failure_detail_read_(uint64_t failure_id) {
  if (this->outgoing_frames_.size() >= MAX_OUTGOING_QUEUE_SIZE) {
    return;
  }
  OutgoingFrame frame;
  frame.message_id = this->next_message_id_++;
  frame.query_keys = {wilo_protocol::KEY_FAILURE_DETAIL};
  frame.bucket = RequestBucket::STATIC_DATA;
  frame.kind = RequestKind::FAILURE_DETAIL;
  frame.failure_id = failure_id;
  frame.track_response = true;
  frame.min_delay_ms = 120;
  frame.payload = wilo_protocol::build_indexed_read_frame(frame.message_id, wilo_protocol::KEY_FAILURE_DETAIL,
                                                          failure_id, this->client_addr_);
  this->outgoing_frames_.push_back(std::move(frame));
}

void WiloBtComponent::queue_authentication_frame_() {
  if (this->session_pin_ > 9999) {
    ESP_LOGW(TAG, "Session PIN must be between 0000 and 9999.");
    return;
  }
  ESP_LOGI(TAG, "Sending session PIN authentication frame.");
  this->auth_requested_ = false;
  this->queue_envelope_frame_(wilo_protocol::build_auth_pin_frame(this->session_pin_), 150, false, true);
}

void WiloBtComponent::maybe_schedule_polls_() {
  const uint32_t now = millis();

  if (this->protocol_state_ == ProtocolState::WAIT_PIN && this->outgoing_frames_.empty()) {
    if (this->auth_requested_) {
      this->queue_authentication_frame_();
      return;
    }
    if (this->auto_authenticate_ && this->has_default_pin_ && !this->auto_auth_attempted_) {
      this->auto_auth_attempted_ = true;
      ESP_LOGI(TAG, "Auto-authenticating with configured module PIN (single attempt).");
      this->queue_authentication_frame_();
      return;
    }
  }

  if (this->protocol_state_ != ProtocolState::READY) {
    return;
  }

  if (!this->static_poll_done_) {
    this->queue_query_(std::vector<uint16_t>(METADATA_BATCH_KEYS.begin(), METADATA_BATCH_KEYS.end()),
                       RequestBucket::STATIC_DATA);
    for (uint16_t key : STATIC_SINGLE_KEYS) {
      this->queue_query_({key}, RequestBucket::STATIC_DATA);
    }
    this->static_poll_done_ = true;
    this->next_config_poll_ms_ = now;
    this->next_live_poll_ms_ = now;
  }

  if (now >= this->next_config_poll_ms_) {
    for (uint16_t key : CONFIG_SINGLE_KEYS) {
      this->queue_query_({key}, RequestBucket::STATIC_DATA);
    }
    this->next_config_poll_ms_ = now + this->config_poll_interval_ms_;
  }

  if (now >= this->next_live_poll_ms_) {
    // Only schedule a new live round if the queue isn't already backed up
    if (this->outgoing_frames_.size() <= 16) {
      this->queue_query_(std::vector<uint16_t>(LIVE_BATCH_KEYS.begin(), LIVE_BATCH_KEYS.end()),
                         RequestBucket::LIVE_DATA);
      this->queue_query_(std::vector<uint16_t>(STATUS_BATCH_KEYS.begin(), STATUS_BATCH_KEYS.end()),
                         RequestBucket::LIVE_DATA);
      for (uint16_t key : LIVE_SINGLE_KEYS) {
        this->queue_query_({key}, RequestBucket::LIVE_DATA);
      }
      if (this->air_venting_active_) {
        this->queue_query_({wilo_protocol::KEY_AIR_VENTING_STATUS}, RequestBucket::STATIC_DATA);
        this->queue_query_({wilo_protocol::KEY_AIR_VENTING_REMAINING}, RequestBucket::STATIC_DATA);
      }
    }
    this->next_live_poll_ms_ = now + this->update_interval_ms_;
  }
}

void WiloBtComponent::process_outgoing_queue_() {
  if (this->outgoing_frames_.empty()) {
    return;
  }
  if (this->protocol_state_ == ProtocolState::DISCONNECTED || this->tx_data_handle_ == 0) {
    return;
  }
  if (this->tx_credits_ == 0) {
    return;
  }
  const uint32_t now = millis();
  if (now < this->next_tx_at_ms_) {
    return;
  }

  OutgoingFrame frame = std::move(this->outgoing_frames_.front());
  this->outgoing_frames_.pop_front();

  if (!this->send_tx_data_(frame.payload)) {
    this->outgoing_frames_.push_front(std::move(frame));
    this->next_tx_at_ms_ = now + 200;
    return;
  }

  this->tx_credits_--;
  if (this->debug_protocol_) {
    ESP_LOGD(TAG, "TX (credits left=%u): %s", this->tx_credits_,
             wilo_protocol::bytes_to_hex(frame.payload.data(), frame.payload.size()).c_str());
  }

  if (frame.track_response) {
    this->pending_requests_[frame.message_id] =
        PendingRequest{std::move(frame.query_keys), frame.bucket, frame.kind, frame.failure_id, now};
  } else if (frame.is_auth_frame) {
    this->set_protocol_state_(ProtocolState::AUTHENTICATING);
    this->state_deadline_ms_ = now + AUTH_TIMEOUT_MS;
  } else if (frame.is_pin_request && this->protocol_state_ == ProtocolState::BOOTSTRAP) {
    this->state_deadline_ms_ = now + BOOTSTRAP_TIMEOUT_MS;
  }

  this->next_tx_at_ms_ = now + frame.min_delay_ms;
}

void WiloBtComponent::queue_rx_credit_grant_(uint8_t count) {
  if (this->pending_rx_credit_writes_ < 64) {
    this->pending_rx_credit_writes_ = static_cast<uint16_t>(this->pending_rx_credit_writes_ + count);
  }
  this->process_rx_credit_queue_();
}

void WiloBtComponent::process_rx_credit_queue_() {
  if (this->pending_rx_credit_writes_ == 0 || this->rx_credit_in_flight_) {
    return;
  }
  if (this->protocol_state_ == ProtocolState::DISCONNECTED || this->tx_credits_handle_ == 0) {
    return;
  }
  if (this->send_tx_credit_grant_()) {
    this->pending_rx_credit_writes_--;
  }
}

void WiloBtComponent::expire_stale_requests_() {
  const uint32_t now = millis();
  for (auto it = this->pending_requests_.begin(); it != this->pending_requests_.end();) {
    if ((now - it->second.sent_at_ms) > REQUEST_STALE_TIMEOUT_MS) {
      if (this->debug_protocol_) {
        ESP_LOGW(TAG, "Expiring unanswered CoAP request mid=0x%04X", it->first);
      }
      it = this->pending_requests_.erase(it);
    } else {
      ++it;
    }
  }
}

void WiloBtComponent::handle_indicate_(const uint8_t *data, size_t len) {
  if (len == 0) {
    return;
  }
  const uint8_t granted = data[0];
  const uint16_t updated = static_cast<uint16_t>(this->tx_credits_) + granted;
  this->tx_credits_ = static_cast<uint8_t>(std::min<uint16_t>(updated, 255));

  if (this->debug_protocol_) {
    ESP_LOGD(TAG, "RX indicate TX credits +%u (total=%u): %s", granted, this->tx_credits_,
             wilo_protocol::bytes_to_hex(data, len).c_str());
  }

  if (this->protocol_state_ == ProtocolState::FLOW_INIT && !this->got_initial_tx_credits_) {
    this->got_initial_tx_credits_ = true;
    if (this->initial_credit_writes_completed_ < wilo_protocol::WILO_INITIAL_RX_CREDIT_WRITES &&
        this->pending_rx_credit_writes_ == 0 && !this->rx_credit_in_flight_) {
      this->queue_rx_credit_grant_(1);
    }
  } else {
    this->queue_rx_credit_grant_(1);
  }
}

void WiloBtComponent::handle_notify_chunk_(const uint8_t *data, size_t len) {
  if (len == 0) {
    return;
  }
  if (this->debug_protocol_) {
    ESP_LOGD(TAG, "RX notify (%u B): %s", static_cast<unsigned>(len), wilo_protocol::bytes_to_hex(data, len).c_str());
  }

  // Fast path: standalone complete Wilo envelope when reassembly buffer is empty
  if (this->rx_reassembly_buf_.empty()) {
    if (len >= 4) {
      const uint16_t declared = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);
      if (len == static_cast<size_t>(4) + declared) {
        wilo_protocol::WiloEnvelope env;
        if (wilo_protocol::parse_wilo_envelope(data, len, &env)) {
          this->process_complete_envelope_(env);
        }
        return;
      }
      if (len < static_cast<size_t>(4) + declared && declared <= 1024) {
        this->rx_reassembly_buf_.assign(data, data + len);
        this->rx_reassembly_started_ms_ = millis();
        return;
      }
    }
    return;
  }

  // Continuation chunk
  this->rx_reassembly_buf_.insert(this->rx_reassembly_buf_.end(), data, data + len);
  if (this->rx_reassembly_buf_.size() >= 4) {
    const uint16_t declared =
        static_cast<uint16_t>(this->rx_reassembly_buf_[2]) | (static_cast<uint16_t>(this->rx_reassembly_buf_[3]) << 8);
    if (this->rx_reassembly_buf_.size() >= static_cast<size_t>(4) + declared) {
      wilo_protocol::WiloEnvelope env;
      if (wilo_protocol::parse_wilo_envelope(this->rx_reassembly_buf_.data(), this->rx_reassembly_buf_.size(), &env)) {
        this->rx_reassembly_buf_.clear();
        this->process_complete_envelope_(env);
      } else {
        this->rx_reassembly_buf_.clear();
      }
    } else if (this->rx_reassembly_buf_.size() > 1028) {
      this->rx_reassembly_buf_.clear();
    }
  }
}

void WiloBtComponent::process_complete_envelope_(const wilo_protocol::WiloEnvelope &env) {
  if (env.channel == wilo_protocol::WILO_CHANNEL_TRANSPORT || env.channel == wilo_protocol::WILO_CHANNEL_AUTH) {
    const auto reply = wilo_protocol::parse_bootstrap_or_auth_reply(env);
    switch (reply.kind) {
      case wilo_protocol::BootstrapReply::Kind::MAX_FRAME_SIZE:
        this->max_frame_size_ = reply.value16;
        ESP_LOGD(TAG, "Pump negotiated max_frame_size=%u", this->max_frame_size_);
        break;
      case wilo_protocol::BootstrapReply::Kind::TRANSPORT_FLAGS:
        ESP_LOGD(TAG, "Pump transport flags=0x%04X", reply.value16);
        break;
      case wilo_protocol::BootstrapReply::Kind::CLIENT_ADDRESS:
        this->client_addr_ = reply.value8;
        ESP_LOGI(TAG, "Pump assigned CoAP client address 0x%02X", this->client_addr_);
        break;
      case wilo_protocol::BootstrapReply::Kind::PIN_REQUIRED:
        ESP_LOGI(TAG, "Pump is displaying a 4-digit session PIN (status=0x%02X). Enter it in HA and press Authenticate.",
                 reply.value8);
        this->state_deadline_ms_ = 0;
        if (this->protocol_state_ == ProtocolState::BOOTSTRAP || this->protocol_state_ == ProtocolState::AUTHENTICATING) {
          this->set_protocol_state_(ProtocolState::WAIT_PIN);
        }
        break;
      case wilo_protocol::BootstrapReply::Kind::AUTH_SUCCESS:
        ESP_LOGI(TAG, "Session authenticated! Starting telemetry polling.");
        this->state_deadline_ms_ = 0;
        this->set_protocol_state_(ProtocolState::READY);
        break;
      case wilo_protocol::BootstrapReply::Kind::AUTH_ERROR:
        ESP_LOGW(TAG, "Pump rejected session PIN (code=0x%02X). Check PIN and retry.", reply.value8);
        this->state_deadline_ms_ = 0;
        this->set_protocol_state_(ProtocolState::WAIT_PIN);
        break;
      case wilo_protocol::BootstrapReply::Kind::UNKNOWN:
        ESP_LOGD(TAG, "Unhandled control envelope ch=0x%02X payload=%s", env.channel,
                 wilo_protocol::bytes_to_hex(env.payload.data(), env.payload.size()).c_str());
        break;
    }
    return;
  }

  if (env.channel != wilo_protocol::WILO_CHANNEL_COAP) {
    return;
  }

  wilo_protocol::CoapMessage coap;
  if (!wilo_protocol::parse_coap_message(env.payload.data(), env.payload.size(), &coap)) {
    ESP_LOGW(TAG, "Failed to parse CoAP message from Wilo notify frame.");
    return;
  }

  auto pending_it = this->pending_requests_.find(coap.message_id);

  if (!coap.is_success()) {
    if (pending_it != this->pending_requests_.end() && pending_it->second.kind == RequestKind::WRITE &&
        !pending_it->second.query_keys.empty()) {
      ESP_LOGW(TAG, "CoAP write to 0x%04X rejected (%u.%02u, mid=0x%04X)", pending_it->second.query_keys.front(),
               coap.code_class(), coap.code_detail(), coap.message_id);
      this->pending_requests_.erase(pending_it);
      // Restore UI entities to cached true pump state
      this->publish_decoded_sensors_();
    } else {
      ESP_LOGD(TAG, "CoAP non-2xx response %u.%02u for mid=0x%04X uri=%s", coap.code_class(), coap.code_detail(),
               coap.message_id, coap.uri_path.c_str());
      if (pending_it != this->pending_requests_.end()) {
        this->pending_requests_.erase(pending_it);
      }
    }
    return;
  }

  // Handle write acknowledgement (typically 2.04 Changed with empty payload)
  if (pending_it != this->pending_requests_.end() && pending_it->second.kind == RequestKind::WRITE) {
    const uint16_t key = pending_it->second.query_keys.empty() ? 0 : pending_it->second.query_keys.front();
    ESP_LOGI(TAG, "CoAP write to 0x%04X succeeded (%u.%02u, mid=0x%04X).", key, coap.code_class(), coap.code_detail(),
             coap.message_id);
    this->pending_requests_.erase(pending_it);
    if (key != 0) {
      this->queue_write_readback_(key);
    }
    return;
  }

  if (coap.payload.empty()) {
    if (pending_it != this->pending_requests_.end()) {
      this->pending_requests_.erase(pending_it);
    }
    return;
  }

  wilo_protocol::CborValue decoded;
  if (!wilo_protocol::decode_cbor(coap.payload.data(), coap.payload.size(), &decoded)) {
    ESP_LOGW(TAG, "Failed to decode CBOR payload for mid=0x%04X.", coap.message_id);
    if (pending_it != this->pending_requests_.end()) {
      this->pending_requests_.erase(pending_it);
    }
    return;
  }

  this->process_query_response_(coap.message_id, decoded);
}

void WiloBtComponent::process_query_response_(uint16_t message_id, const wilo_protocol::CborValue &value) {
  auto it = this->pending_requests_.find(message_id);
  if (it == this->pending_requests_.end()) {
    if (this->debug_protocol_) {
      ESP_LOGD(TAG, "Received CoAP response for unknown/expired mid=0x%04X", message_id);
    }
    return;
  }

  if (it->second.kind == RequestKind::FAILURE_DETAIL) {
    const uint64_t failure_id = it->second.failure_id;
    this->pending_requests_.erase(it);
    wilo_protocol::FailureDetail detail;
    if (wilo_protocol::parse_failure_detail(value, &detail)) {
      this->failure_details_[failure_id] = detail;
      ESP_LOGW(TAG, "Decoded active pump %s code %u (id=%llu)", detail.is_error ? "ERROR" : "WARNING",
               static_cast<unsigned>(detail.trouble_code), static_cast<unsigned long long>(failure_id));
    } else if (this->debug_protocol_) {
      ESP_LOGD(TAG, "Could not parse 0x0C8C failure detail for id=%llu: %s",
               static_cast<unsigned long long>(failure_id), wilo_protocol::cbor_to_json(value).c_str());
    }
    this->publish_alarm_entities_();
    return;
  }

  const RequestBucket bucket = it->second.bucket;
  const std::vector<uint16_t> query_keys = std::move(it->second.query_keys);
  this->pending_requests_.erase(it);

  this->store_values_in_bucket_(bucket, query_keys, value);
  if (query_keys.size() == 1 && query_keys.front() == wilo_protocol::KEY_ACTIVE_ALARMS) {
    this->handle_active_alarms_list_(value);
  }
  this->publish_decoded_sensors_();
  this->publish_alarm_entities_();
  this->publish_raw_json_();
}

void WiloBtComponent::store_values_in_bucket_(RequestBucket bucket, const std::vector<uint16_t> &query_keys,
                                              const wilo_protocol::CborValue &value) {
  auto &target = bucket == RequestBucket::LIVE_DATA ? this->live_values_ : this->static_values_;

  if (query_keys.size() > 1 && value.type == wilo_protocol::CborValue::Type::MAP) {
    bool stored_any = false;
    for (const auto &entry : value.map_values) {
      if (entry.first.type == wilo_protocol::CborValue::Type::UNSIGNED) {
        target[static_cast<uint16_t>(entry.first.unsigned_value)] = entry.second;
        stored_any = true;
      }
    }
    if (stored_any) {
      return;
    }
  }

  if (query_keys.size() == 1) {
    target[query_keys.front()] = value;
  }
}

void WiloBtComponent::handle_active_alarms_list_(const wilo_protocol::CborValue &alarms) {
  if (alarms.type != wilo_protocol::CborValue::Type::ARRAY) {
    return;
  }

  std::set<uint64_t> current_ids;
  for (const auto &item : alarms.array_values) {
    if (item.type == wilo_protocol::CborValue::Type::UNSIGNED) {
      current_ids.insert(item.unsigned_value);
    }
  }

  // Remove cleared failure details
  for (auto it = this->failure_details_.begin(); it != this->failure_details_.end();) {
    if (current_ids.find(it->first) == current_ids.end()) {
      it = this->failure_details_.erase(it);
    } else {
      ++it;
    }
  }

  // Fetch 0x0C8C details for any newly reported failure IDs
  for (uint64_t id : current_ids) {
    if (this->failure_details_.find(id) == this->failure_details_.end()) {
      this->queue_failure_detail_read_(id);
    }
  }
  this->active_failure_ids_ = std::move(current_ids);
}

const wilo_protocol::CborValue *WiloBtComponent::find_cached_value_(uint16_t key) const {
  auto it_live = this->live_values_.find(key);
  if (it_live != this->live_values_.end()) {
    return &it_live->second;
  }
  auto it_static = this->static_values_.find(key);
  if (it_static != this->static_values_.end()) {
    return &it_static->second;
  }
  return nullptr;
}

void WiloBtComponent::publish_decoded_sensors_() {
  auto find_val = [this](uint16_t key) -> const wilo_protocol::CborValue * { return this->find_cached_value_(key); };

  // Text metadata
  if (this->device_name_text_sensor_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_DEVICE_NAME);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::STRING) {
      this->device_name_text_sensor_->publish_state(v->string_value);
    }
  }
  if (this->pump_type_text_sensor_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_PUMP_TYPE);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::STRING) {
      this->pump_type_text_sensor_->publish_state(v->string_value);
    }
  }
  if (this->serial_number_text_sensor_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_SERIAL_NUMBER);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::STRING) {
      this->serial_number_text_sensor_->publish_state(v->string_value);
    }
  }
  if (this->software_version_text_sensor_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_SOFTWARE_VERSION);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::UNSIGNED) {
      this->software_version_text_sensor_->publish_state(wilo_protocol::format_software_version(v->unsigned_value));
    }
  }

  // Live numeric telemetry (skipping Wilo 0xFFFFFFFF invalid sentinel)
  if (this->flow_rate_sensor_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_ACTUAL_FLOW);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::UNSIGNED &&
        !wilo_protocol::is_invalid_raw_u32(v->unsigned_value)) {
      this->flow_rate_sensor_->publish_state(static_cast<float>(wilo_protocol::decode_biased_u32_q10(v->unsigned_value)));
    }
  }
  if (this->differential_pressure_sensor_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_ACTUAL_HEAD);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::UNSIGNED &&
        !wilo_protocol::is_invalid_raw_u32(v->unsigned_value)) {
      this->differential_pressure_sensor_->publish_state(
          static_cast<float>(wilo_protocol::decode_u16_q10(v->unsigned_value)));
    }
  }
  if (this->medium_temperature_sensor_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_MEDIUM_TEMP);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::UNSIGNED) {
      double temp_c = 0.0;
      if (wilo_protocol::decode_medium_temp_c(v->unsigned_value, &temp_c)) {
        this->medium_temperature_sensor_->publish_state(static_cast<float>(temp_c));
      }
    }
  }
  if (this->power_sensor_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_ELECTRICAL_POWER);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::UNSIGNED &&
        !wilo_protocol::is_invalid_raw_u32(v->unsigned_value)) {
      this->power_sensor_->publish_state(static_cast<float>(wilo_protocol::decode_power_watts(v->unsigned_value)));
    }
  }
  if (this->energy_sensor_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_ELECTRICAL_ENERGY);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::UNSIGNED &&
        !wilo_protocol::is_invalid_raw_u32(v->unsigned_value)) {
      this->energy_sensor_->publish_state(static_cast<float>(wilo_protocol::decode_energy_kwh(v->unsigned_value)));
    }
  }
  if (this->speed_sensor_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_MOTOR_SPEED);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::UNSIGNED &&
        !wilo_protocol::is_invalid_raw_u32(v->unsigned_value)) {
      this->speed_sensor_->publish_state(static_cast<float>(wilo_protocol::decode_biased_u16_q1(v->unsigned_value)));
    }
  }
  if (this->operating_hours_sensor_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_OPERATING_SECONDS);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::UNSIGNED &&
        !wilo_protocol::is_invalid_raw_u32(v->unsigned_value)) {
      this->operating_hours_sensor_->publish_state(
          static_cast<float>(wilo_protocol::decode_operating_hours(v->unsigned_value)));
    }
  }
  if (this->max_head_sensor_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_MAX_HEAD);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::UNSIGNED &&
        !wilo_protocol::is_invalid_raw_u32(v->unsigned_value)) {
      this->max_head_sensor_->publish_state(static_cast<float>(wilo_protocol::decode_biased_u32_q10(v->unsigned_value)));
    }
  }
  if (this->max_flow_sensor_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_MAX_FLOW);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::UNSIGNED &&
        !wilo_protocol::is_invalid_raw_u32(v->unsigned_value)) {
      this->max_flow_sensor_->publish_state(static_cast<float>(wilo_protocol::decode_biased_u32_q10(v->unsigned_value)));
    }
  }
  if (this->pump_status_code_sensor_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_INSTALLATION_TYPE);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::UNSIGNED) {
      this->pump_status_code_sensor_->publish_state(static_cast<float>(v->unsigned_value));
    }
  }

  // Sync switches with true pump state
  if (this->pump_run_switch_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_PUMP_OPERATION_MODE);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::UNSIGNED) {
      this->pump_run_switch_->publish_state(v->unsigned_value == 1);
    }
  }
  if (this->night_setback_switch_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_NIGHT_SETBACK);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::UNSIGNED) {
      this->night_setback_switch_->publish_state(v->unsigned_value == 1);
    }
  }
  if (this->pressure_independent_valve_switch_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_PRESSURE_INDEPENDENT_VALVE);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::UNSIGNED) {
      this->pressure_independent_valve_switch_->publish_state(v->unsigned_value == 1);
    }
  }

  // Air venting status (0x060F): CoAP AV.Status.Status == 4 means venting running (3 = idle/completed)
  if (const auto *v = find_val(wilo_protocol::KEY_AIR_VENTING_STATUS);
      v != nullptr && v->type == wilo_protocol::CborValue::Type::UNSIGNED) {
    this->air_venting_active_ = (v->unsigned_value == 4);
    if (this->air_venting_active_binary_sensor_ != nullptr) {
      this->air_venting_active_binary_sensor_->publish_state(this->air_venting_active_);
    }
    if (this->air_venting_switch_ != nullptr) {
      this->air_venting_switch_->publish_state(this->air_venting_active_);
    }
  }
  if (this->air_venting_remaining_sensor_ != nullptr) {
    if (const auto *v = find_val(wilo_protocol::KEY_AIR_VENTING_REMAINING);
        v != nullptr && v->type == wilo_protocol::CborValue::Type::UNSIGNED &&
        !wilo_protocol::is_invalid_raw_u32(v->unsigned_value)) {
      this->air_venting_remaining_sensor_->publish_state(static_cast<float>(v->unsigned_value));
    }
  }

  // Control mode & select sync
  const auto *mode_val = find_val(wilo_protocol::KEY_CONTROL_MODE);
  if (mode_val != nullptr && mode_val->type == wilo_protocol::CborValue::Type::UNSIGNED) {
    const std::string mode_str = wilo_protocol::control_mode_to_string(mode_val->unsigned_value);
    if (this->control_mode_code_sensor_ != nullptr) {
      this->control_mode_code_sensor_->publish_state(static_cast<float>(mode_val->unsigned_value));
    }
    if (this->control_mode_text_sensor_ != nullptr) {
      this->control_mode_text_sensor_->publish_state(mode_str);
    }
    if (this->control_mode_select_ != nullptr) {
      uint16_t check_mode = 0;
      if (wilo_protocol::control_mode_from_string(mode_str, &check_mode)) {
        if (!this->control_mode_select_->has_state() || this->control_mode_select_->current_option() != mode_str) {
          this->control_mode_select_->publish_state(mode_str);
        }
      }
    }
  }

  // Setpoint sensors & control number sync
  const auto *sp_val = find_val(wilo_protocol::KEY_SETPOINT_NORM);
  if (sp_val != nullptr && sp_val->type == wilo_protocol::CborValue::Type::UNSIGNED) {
    const float sp_pct = static_cast<float>(wilo_protocol::decode_setpoint_percent(sp_val->unsigned_value));
    if (this->setpoint_percent_sensor_ != nullptr) {
      this->setpoint_percent_sensor_->publish_state(sp_pct);
    }
    if (this->setpoint_percent_control_number_ != nullptr) {
      const float snapped_pct = std::round(sp_pct);
      if (!this->setpoint_percent_control_number_->has_state() ||
          std::fabs(this->setpoint_percent_control_number_->state - snapped_pct) >= 0.5f) {
        this->setpoint_percent_control_number_->publish_state(snapped_pct);
      }
    }
    if (mode_val != nullptr && mode_val->type == wilo_protocol::CborValue::Type::UNSIGNED) {
      if (const auto *scale_map = find_val(wilo_protocol::KEY_SETPOINT_SCALE_MAP); scale_map != nullptr) {
        double phys_sp = 0.0;
        if (wilo_protocol::compute_physical_setpoint(sp_val->unsigned_value, mode_val->unsigned_value, *scale_map,
                                                     &phys_sp)) {
          const float sp_f = static_cast<float>(phys_sp);
          if (this->setpoint_sensor_ != nullptr) {
            this->setpoint_sensor_->publish_state(sp_f);
          }
          const uint64_t mode = mode_val->unsigned_value;
          if (this->setpoint_control_number_ != nullptr &&
              (mode == 8 || mode == 9 || mode == 10 || mode == 16 || mode == 17)) {
            const float snapped_m = std::round(sp_f * 10.0f) / 10.0f;
            if (!this->setpoint_control_number_->has_state() ||
                std::fabs(this->setpoint_control_number_->state - snapped_m) >= 0.05f) {
              this->setpoint_control_number_->publish_state(snapped_m);
            }
          }
        }
      }
    }
  }
}

void WiloBtComponent::publish_alarm_entities_() {
  // Authoritative fault & warning state ONLY from 0x0C8B (Failure.List) + 0x0C8C (FailureDetail)
  const auto *alarms = this->find_cached_value_(wilo_protocol::KEY_ACTIVE_ALARMS);
  if (alarms == nullptr || alarms->type != wilo_protocol::CborValue::Type::ARRAY) {
    return;
  }

  std::vector<wilo_protocol::FailureDetail> details;
  bool has_error = false;
  bool has_warning = false;
  size_t unresolved = 0;

  for (uint64_t id : this->active_failure_ids_) {
    auto it = this->failure_details_.find(id);
    if (it != this->failure_details_.end()) {
      details.push_back(it->second);
      if (it->second.is_error) {
        has_error = true;
      } else {
        has_warning = true;
      }
    } else {
      unresolved++;
      has_error = true;  // Treat unresolved active failure ID conservatively until 0x0C8C returns
    }
  }

  if (this->active_fault_binary_sensor_ != nullptr) {
    this->active_fault_binary_sensor_->publish_state(has_error);
  }
  if (this->active_warning_binary_sensor_ != nullptr) {
    this->active_warning_binary_sensor_->publish_state(has_warning);
  }
  if (this->active_alarms_text_sensor_ != nullptr) {
    this->active_alarms_text_sensor_->publish_state(wilo_protocol::format_failure_summary(details, unresolved));
  }
}

void WiloBtComponent::publish_raw_json_() {
  // Keep text_sensor states strictly <= 250 chars so Home Assistant never rejects them (>255 limit)
  if (this->raw_static_json_text_sensor_ != nullptr && !this->static_values_.empty()) {
    this->raw_static_json_text_sensor_->publish_state(
        wilo_protocol::key_map_to_compact_json(this->static_values_, 250, true));
  }
  if (this->raw_live_json_text_sensor_ != nullptr && !this->live_values_.empty()) {
    this->raw_live_json_text_sensor_->publish_state(
        wilo_protocol::key_map_to_compact_json(this->live_values_, 250, true));
  }
}

bool WiloBtComponent::send_tx_data_(const std::vector<uint8_t> &payload) {
  auto *client = this->parent();
  if (client == nullptr || this->tx_data_handle_ == 0 || client->get_conn_id() == esp32_ble_client::UNSET_CONN_ID) {
    return false;
  }
  const esp_err_t err = esp_ble_gattc_write_char(client->get_gattc_if(), client->get_conn_id(), this->tx_data_handle_,
                                                 payload.size(), const_cast<uint8_t *>(payload.data()),
                                                 ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "TX data write failed (err=%d).", err);
    return false;
  }
  return true;
}

bool WiloBtComponent::send_tx_credit_grant_() {
  auto *client = this->parent();
  if (client == nullptr || this->tx_credits_handle_ == 0 || client->get_conn_id() == esp32_ble_client::UNSET_CONN_ID) {
    return false;
  }
  uint8_t credit = wilo_protocol::WILO_RX_CREDIT_GRANT;
  const esp_err_t err =
      esp_ble_gattc_write_char(client->get_gattc_if(), client->get_conn_id(), this->tx_credits_handle_, sizeof(credit),
                               &credit, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "RX credit grant write failed (err=%d).", err);
    return false;
  }
  this->rx_credit_in_flight_ = true;
  return true;
}

}  // namespace wilo_bt
}  // namespace esphome

#endif
