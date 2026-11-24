import esphome.codegen as cg
from esphome.components import (
    binary_sensor,
    ble_client,
    button,
    number,
    select,
    sensor,
    switch,
    text_sensor,
)
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_MODE,
    CONF_UPDATE_INTERVAL,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLUME_FLOW_RATE,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_CELSIUS,
    UNIT_CUBIC_METER_PER_HOUR,
    UNIT_HOUR,
    UNIT_KILOWATT_HOURS,
    UNIT_METER,
    UNIT_PERCENT,
    UNIT_REVOLUTIONS_PER_MINUTE,
    UNIT_SECOND,
    UNIT_WATT,
)

AUTO_LOAD = [
    "binary_sensor",
    "button",
    "number",
    "select",
    "sensor",
    "switch",
    "text_sensor",
]
DEPENDENCIES = ["ble_client"]

CONF_CONFIG_POLL_INTERVAL = "config_poll_interval"
CONF_DEBUG_PROTOCOL = "debug_protocol"
CONF_PIN = "pin"
CONF_AUTO_AUTHENTICATE = "auto_authenticate"

# Auth controls
CONF_SESSION_PIN = "session_pin"
CONF_AUTHENTICATE = "authenticate"
CONF_REQUEST_PIN = "request_pin"

# Pump controls
CONF_PUMP_RUN = "pump_run"
CONF_NIGHT_SETBACK = "night_setback"
CONF_PRESSURE_INDEPENDENT_VALVE = "pressure_independent_valve"
CONF_AIR_VENTING = "air_venting"
CONF_SETPOINT_CONTROL = "setpoint_control"
CONF_SETPOINT_PERCENT_CONTROL = "setpoint_percent_control"
CONF_CONTROL_MODE_SELECT = "control_mode_select"
CONF_MANUAL_RESTART = "manual_restart"
CONF_RESET_ENERGY = "reset_energy"

# Binary sensors
CONF_CONNECTED = "connected"
CONF_AUTHENTICATED = "authenticated"
CONF_WAITING_FOR_PIN = "waiting_for_pin"
CONF_ACTIVE_FAULT = "active_fault"
CONF_ACTIVE_WARNING = "active_warning"
CONF_AIR_VENTING_ACTIVE = "air_venting_active"

# Numeric sensors
CONF_FLOW_RATE = "flow_rate"
CONF_DIFFERENTIAL_PRESSURE = "differential_pressure"
CONF_MEDIUM_TEMPERATURE = "medium_temperature"
CONF_POWER = "power"
CONF_ENERGY = "energy"
CONF_SPEED = "speed"
CONF_OPERATING_HOURS = "operating_hours"
CONF_SETPOINT = "setpoint"
CONF_SETPOINT_PERCENT = "setpoint_percent"
CONF_MAX_HEAD = "max_head"
CONF_MAX_FLOW = "max_flow"
CONF_AIR_VENTING_REMAINING = "air_venting_remaining"
CONF_PUMP_STATUS_CODE = "pump_status_code"
CONF_CONTROL_MODE_CODE = "control_mode_code"

# Text sensors
CONF_DEVICE_NAME = "device_name"
CONF_PUMP_TYPE = "pump_type"
CONF_SERIAL_NUMBER = "serial_number"
CONF_SOFTWARE_VERSION = "software_version"
CONF_CONTROL_MODE = "control_mode"
CONF_ACTIVE_ALARMS = "active_alarms"
CONF_PROTOCOL_STATE = "protocol_state"
CONF_RAW_STATIC_JSON = "raw_static_json"
CONF_RAW_LIVE_JSON = "raw_live_json"

STRATOS_PICO_CONTROL_MODES = [
    "Constant Pressure (dp-c)",
    "Variable Pressure (dp-v)",
    "Constant Speed (n-const)",
    "Dynamic Adapt",
]

wilo_bt_ns = cg.esphome_ns.namespace("wilo_bt")
WiloBtComponent = wilo_bt_ns.class_(
    "WiloBtComponent", cg.Component, ble_client.BLEClientNode
)

# Auth control classes
WiloSessionPinNumber = wilo_bt_ns.class_("WiloSessionPinNumber", number.Number)
WiloAuthenticateButton = wilo_bt_ns.class_("WiloAuthenticateButton", button.Button)
WiloRequestPinButton = wilo_bt_ns.class_("WiloRequestPinButton", button.Button)

# Pump control classes
WiloPumpRunSwitch = wilo_bt_ns.class_("WiloPumpRunSwitch", switch.Switch)
WiloNightSetbackSwitch = wilo_bt_ns.class_("WiloNightSetbackSwitch", switch.Switch)
WiloPressureIndependentValveSwitch = wilo_bt_ns.class_(
    "WiloPressureIndependentValveSwitch", switch.Switch
)
WiloAirVentingSwitch = wilo_bt_ns.class_("WiloAirVentingSwitch", switch.Switch)
WiloSetpointNumber = wilo_bt_ns.class_("WiloSetpointNumber", number.Number)
WiloSetpointPercentNumber = wilo_bt_ns.class_(
    "WiloSetpointPercentNumber", number.Number
)
WiloControlModeSelect = wilo_bt_ns.class_("WiloControlModeSelect", select.Select)
WiloManualRestartButton = wilo_bt_ns.class_("WiloManualRestartButton", button.Button)
WiloResetEnergyButton = wilo_bt_ns.class_("WiloResetEnergyButton", button.Button)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(WiloBtComponent),
            cv.Optional(
                CONF_UPDATE_INTERVAL, default="5s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_CONFIG_POLL_INTERVAL, default="60s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_DEBUG_PROTOCOL, default=False): cv.boolean,
            cv.Optional(CONF_PIN): cv.int_range(min=0, max=9999),
            cv.Optional(CONF_AUTO_AUTHENTICATE, default=False): cv.boolean,
            # Auth controls
            cv.Optional(CONF_SESSION_PIN): number.number_schema(
                WiloSessionPinNumber,
                icon="mdi:form-textbox-password",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ).extend(
                {
                    cv.Optional(CONF_MODE, default="BOX"): cv.enum(
                        number.NUMBER_MODES, upper=True
                    )
                }
            ),
            cv.Optional(CONF_AUTHENTICATE): button.button_schema(
                WiloAuthenticateButton,
                icon="mdi:lock-open-check",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_REQUEST_PIN): button.button_schema(
                WiloRequestPinButton,
                icon="mdi:dialpad",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            # Pump controls (default_restore_mode="DISABLED" prevents unintended writes on ESP reboot)
            cv.Optional(CONF_PUMP_RUN): switch.switch_schema(
                WiloPumpRunSwitch,
                icon="mdi:pump",
                default_restore_mode="DISABLED",
            ),
            cv.Optional(CONF_NIGHT_SETBACK): switch.switch_schema(
                WiloNightSetbackSwitch,
                icon="mdi:weather-night",
                entity_category=ENTITY_CATEGORY_CONFIG,
                default_restore_mode="DISABLED",
            ),
            cv.Optional(CONF_PRESSURE_INDEPENDENT_VALVE): switch.switch_schema(
                WiloPressureIndependentValveSwitch,
                icon="mdi:pipe-valve",
                entity_category=ENTITY_CATEGORY_CONFIG,
                default_restore_mode="DISABLED",
            ),
            cv.Optional(CONF_AIR_VENTING): switch.switch_schema(
                WiloAirVentingSwitch,
                icon="mdi:air-filter",
                entity_category=ENTITY_CATEGORY_CONFIG,
                default_restore_mode="DISABLED",
            ),
            cv.Optional(CONF_SETPOINT_CONTROL): number.number_schema(
                WiloSetpointNumber,
                unit_of_measurement=UNIT_METER,
                icon="mdi:target",
            ).extend(
                {
                    cv.Optional(CONF_MODE, default="BOX"): cv.enum(
                        number.NUMBER_MODES, upper=True
                    )
                }
            ),
            cv.Optional(CONF_SETPOINT_PERCENT_CONTROL): number.number_schema(
                WiloSetpointPercentNumber,
                unit_of_measurement=UNIT_PERCENT,
                icon="mdi:percent",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ).extend(
                {
                    cv.Optional(CONF_MODE, default="SLIDER"): cv.enum(
                        number.NUMBER_MODES, upper=True
                    )
                }
            ),
            cv.Optional(CONF_CONTROL_MODE_SELECT): select.select_schema(
                WiloControlModeSelect,
                icon="mdi:tune-variant",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_MANUAL_RESTART): button.button_schema(
                WiloManualRestartButton,
                icon="mdi:restart",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_RESET_ENERGY): button.button_schema(
                WiloResetEnergyButton,
                icon="mdi:counter",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            # Binary sensors
            cv.Optional(CONF_CONNECTED): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
                icon="mdi:bluetooth-connect",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_AUTHENTICATED): binary_sensor.binary_sensor_schema(
                icon="mdi:shield-check",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_WAITING_FOR_PIN): binary_sensor.binary_sensor_schema(
                icon="mdi:timer-sand",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_ACTIVE_FAULT): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_PROBLEM,
            ),
            cv.Optional(CONF_ACTIVE_WARNING): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_PROBLEM,
                icon="mdi:alert-outline",
            ),
            cv.Optional(CONF_AIR_VENTING_ACTIVE): binary_sensor.binary_sensor_schema(
                icon="mdi:air-filter",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            # Numeric sensors
            cv.Optional(CONF_FLOW_RATE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CUBIC_METER_PER_HOUR,
                icon="mdi:water-pump",
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_VOLUME_FLOW_RATE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_DIFFERENTIAL_PRESSURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_METER,
                icon="mdi:gauge",
                accuracy_decimals=2,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_MEDIUM_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_POWER): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_ENERGY): sensor.sensor_schema(
                unit_of_measurement=UNIT_KILOWATT_HOURS,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_ENERGY,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional(CONF_SPEED): sensor.sensor_schema(
                unit_of_measurement=UNIT_REVOLUTIONS_PER_MINUTE,
                icon="mdi:speedometer",
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_OPERATING_HOURS): sensor.sensor_schema(
                unit_of_measurement=UNIT_HOUR,
                icon="mdi:clock-outline",
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_DURATION,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional(CONF_SETPOINT): sensor.sensor_schema(
                unit_of_measurement=UNIT_METER,
                icon="mdi:target",
                accuracy_decimals=2,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_SETPOINT_PERCENT): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                icon="mdi:percent",
                accuracy_decimals=1,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_MAX_HEAD): sensor.sensor_schema(
                unit_of_measurement=UNIT_METER,
                icon="mdi:arrow-collapse-up",
                accuracy_decimals=1,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_MAX_FLOW): sensor.sensor_schema(
                unit_of_measurement=UNIT_CUBIC_METER_PER_HOUR,
                icon="mdi:waves",
                accuracy_decimals=1,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_AIR_VENTING_REMAINING): sensor.sensor_schema(
                unit_of_measurement=UNIT_SECOND,
                icon="mdi:timer-outline",
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_DURATION,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_PUMP_STATUS_CODE): sensor.sensor_schema(
                icon="mdi:state-machine",
                accuracy_decimals=0,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_CONTROL_MODE_CODE): sensor.sensor_schema(
                icon="mdi:tune",
                accuracy_decimals=0,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            # Text sensors
            cv.Optional(CONF_DEVICE_NAME): text_sensor.text_sensor_schema(
                icon="mdi:pump",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_PUMP_TYPE): text_sensor.text_sensor_schema(
                icon="mdi:cog-transfer",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_SERIAL_NUMBER): text_sensor.text_sensor_schema(
                icon="mdi:identifier",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_SOFTWARE_VERSION): text_sensor.text_sensor_schema(
                icon="mdi:chip",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_CONTROL_MODE): text_sensor.text_sensor_schema(
                icon="mdi:tune-variant",
            ),
            cv.Optional(CONF_ACTIVE_ALARMS): text_sensor.text_sensor_schema(
                icon="mdi:alert-circle-outline",
            ),
            cv.Optional(CONF_PROTOCOL_STATE): text_sensor.text_sensor_schema(
                icon="mdi:bluetooth-settings",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_RAW_STATIC_JSON): text_sensor.text_sensor_schema(
                icon="mdi:code-json",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_RAW_LIVE_JSON): text_sensor.text_sensor_schema(
                icon="mdi:code-json",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(ble_client.BLE_CLIENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)

    cg.add(var.set_config_poll_interval_ms(config[CONF_CONFIG_POLL_INTERVAL]))
    cg.add(var.set_debug_protocol(config[CONF_DEBUG_PROTOCOL]))
    if CONF_PIN in config:
        cg.add(var.set_default_pin(config[CONF_PIN]))
    cg.add(var.set_auto_authenticate(config[CONF_AUTO_AUTHENTICATE]))

    # Auth controls
    if pin_conf := config.get(CONF_SESSION_PIN):
        num = await number.new_number(pin_conf, min_value=0, max_value=9999, step=1)
        await cg.register_parented(num, config[CONF_ID])
        cg.add(var.set_session_pin_number(num))

    if auth_conf := config.get(CONF_AUTHENTICATE):
        btn = await button.new_button(auth_conf)
        await cg.register_parented(btn, config[CONF_ID])
        cg.add(var.set_authenticate_button(btn))

    if req_pin_conf := config.get(CONF_REQUEST_PIN):
        btn = await button.new_button(req_pin_conf)
        await cg.register_parented(btn, config[CONF_ID])
        cg.add(var.set_request_pin_button(btn))

    # Pump controls
    switches = [
        (CONF_PUMP_RUN, "set_pump_run_switch"),
        (CONF_NIGHT_SETBACK, "set_night_setback_switch"),
        (CONF_PRESSURE_INDEPENDENT_VALVE, "set_pressure_independent_valve_switch"),
        (CONF_AIR_VENTING, "set_air_venting_switch"),
    ]
    for key, setter in switches:
        if conf := config.get(key):
            sw = await switch.new_switch(conf)
            await cg.register_parented(sw, config[CONF_ID])
            cg.add(getattr(var, setter)(sw))

    if sp_conf := config.get(CONF_SETPOINT_CONTROL):
        num = await number.new_number(sp_conf, min_value=0.5, max_value=6.0, step=0.1)
        await cg.register_parented(num, config[CONF_ID])
        cg.add(var.set_setpoint_control_number(num))

    if spp_conf := config.get(CONF_SETPOINT_PERCENT_CONTROL):
        num = await number.new_number(spp_conf, min_value=0.0, max_value=100.0, step=1.0)
        await cg.register_parented(num, config[CONF_ID])
        cg.add(var.set_setpoint_percent_control_number(num))

    if mode_conf := config.get(CONF_CONTROL_MODE_SELECT):
        sel = await select.new_select(mode_conf, options=STRATOS_PICO_CONTROL_MODES)
        await cg.register_parented(sel, config[CONF_ID])
        cg.add(var.set_control_mode_select(sel))

    if restart_conf := config.get(CONF_MANUAL_RESTART):
        btn = await button.new_button(restart_conf)
        await cg.register_parented(btn, config[CONF_ID])
        cg.add(var.set_manual_restart_button(btn))

    if reset_conf := config.get(CONF_RESET_ENERGY):
        btn = await button.new_button(reset_conf)
        await cg.register_parented(btn, config[CONF_ID])
        cg.add(var.set_reset_energy_button(btn))

    # Binary sensors
    binary_sensors = [
        (CONF_CONNECTED, "set_connected_binary_sensor"),
        (CONF_AUTHENTICATED, "set_authenticated_binary_sensor"),
        (CONF_WAITING_FOR_PIN, "set_waiting_for_pin_binary_sensor"),
        (CONF_ACTIVE_FAULT, "set_active_fault_binary_sensor"),
        (CONF_ACTIVE_WARNING, "set_active_warning_binary_sensor"),
        (CONF_AIR_VENTING_ACTIVE, "set_air_venting_active_binary_sensor"),
    ]
    for key, setter in binary_sensors:
        if conf := config.get(key):
            sens = await binary_sensor.new_binary_sensor(conf)
            cg.add(getattr(var, setter)(sens))

    # Numeric sensors
    numeric_sensors = [
        (CONF_FLOW_RATE, "set_flow_rate_sensor"),
        (CONF_DIFFERENTIAL_PRESSURE, "set_differential_pressure_sensor"),
        (CONF_MEDIUM_TEMPERATURE, "set_medium_temperature_sensor"),
        (CONF_POWER, "set_power_sensor"),
        (CONF_ENERGY, "set_energy_sensor"),
        (CONF_SPEED, "set_speed_sensor"),
        (CONF_OPERATING_HOURS, "set_operating_hours_sensor"),
        (CONF_SETPOINT, "set_setpoint_sensor"),
        (CONF_SETPOINT_PERCENT, "set_setpoint_percent_sensor"),
        (CONF_MAX_HEAD, "set_max_head_sensor"),
        (CONF_MAX_FLOW, "set_max_flow_sensor"),
        (CONF_AIR_VENTING_REMAINING, "set_air_venting_remaining_sensor"),
        (CONF_PUMP_STATUS_CODE, "set_pump_status_code_sensor"),
        (CONF_CONTROL_MODE_CODE, "set_control_mode_code_sensor"),
    ]
    for key, setter in numeric_sensors:
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(getattr(var, setter)(sens))

    # Text sensors
    text_sensors = [
        (CONF_DEVICE_NAME, "set_device_name_text_sensor"),
        (CONF_PUMP_TYPE, "set_pump_type_text_sensor"),
        (CONF_SERIAL_NUMBER, "set_serial_number_text_sensor"),
        (CONF_SOFTWARE_VERSION, "set_software_version_text_sensor"),
        (CONF_CONTROL_MODE, "set_control_mode_text_sensor"),
        (CONF_ACTIVE_ALARMS, "set_active_alarms_text_sensor"),
        (CONF_PROTOCOL_STATE, "set_protocol_state_text_sensor"),
        (CONF_RAW_STATIC_JSON, "set_raw_static_json_text_sensor"),
        (CONF_RAW_LIVE_JSON, "set_raw_live_json_text_sensor"),
    ]
    for key, setter in text_sensors:
        if conf := config.get(key):
            sens = await text_sensor.new_text_sensor(conf)
            cg.add(getattr(var, setter)(sens))
