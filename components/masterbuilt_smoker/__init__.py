import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import (
    ble_client,
    binary_sensor,
    button,
    climate,
    esp32_ble_tracker,
    number,
    select,
    sensor,
    switch,
)
from esphome.const import (
    CONF_ID,
    CONF_UNIT_OF_MEASUREMENT,
    DEVICE_CLASS_DOOR,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_MINUTE,
)

CODEOWNERS = ["@webb64b"]
DEPENDENCIES = ["ble_client"]
AUTO_LOAD = ["binary_sensor", "button", "climate", "number", "select", "sensor", "switch"]

masterbuilt_smoker_ns = cg.esphome_ns.namespace("masterbuilt_smoker")
MasterbuiltSmoker = masterbuilt_smoker_ns.class_(
    "MasterbuiltSmoker",
    cg.Component,
    ble_client.BLEClientNode,
    esp32_ble_tracker.ESPBTDeviceListener,
)
ForgetPairingButton = masterbuilt_smoker_ns.class_("ForgetPairingButton", button.Button)
SmokerClimate = masterbuilt_smoker_ns.class_("SmokerClimate", climate.Climate)
SmokerBroilSelect = masterbuilt_smoker_ns.class_("SmokerBroilSelect", select.Select)
SmokerCookTimeNumber = masterbuilt_smoker_ns.class_("SmokerCookTimeNumber", number.Number)
SmokerProbeTargetNumber = masterbuilt_smoker_ns.class_("SmokerProbeTargetNumber", number.Number)
SmokerPowerSwitch = masterbuilt_smoker_ns.class_("SmokerPowerSwitch", switch.Switch)
SmokerLightSwitch = masterbuilt_smoker_ns.class_("SmokerLightSwitch", switch.Switch)
BROIL_OPTIONS = ["Off", "Low", "Medium", "High"]

CONF_CHAMBER_TEMPERATURE = "chamber_temperature"
CONF_TARGET_TEMPERATURE = "target_temperature"
CONF_COOK_TIME = "cook_time"
CONF_TIME_REMAINING = "time_remaining"
CONF_FORGET_PAIRING = "forget_pairing"
CONF_SMOKER_MAC = "smoker_mac"
CONF_TEMPERATURE_UNIT = "temperature_unit"
CONF_PROBES = ["probe_1", "probe_2", "probe_3", "probe_4"]
CONF_PROBE_TARGETS = ["probe_target", "probe_2_target", "probe_3_target", "probe_4_target"]
CONF_DOOR = "door"
CONF_TEMPERATURE_ERROR = "temperature_error"
CONF_MEAT_PROBE_ERROR = "meat_probe_error"
CONF_BROILER_AVAILABLE = "broiler_available"
CONF_SMOKE_ON_DEMAND_AVAILABLE = "smoke_on_demand_available"
CONF_CLIMATE = "climate"
CONF_POWER = "power"
CONF_LIGHT = "light"
CONF_BROIL = "broil"
CONF_COOK_TIMER = "cook_timer"


def _temperature_schema():
    # The unit label and ranges are set in to_code from temperature_unit; the schema default is just a
    # placeholder so a Fahrenheit-labelled sensor exists before the override.
    return sensor.sensor_schema(
        unit_of_measurement="°F",
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
    )


def _minutes_schema():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_MINUTE,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
    )


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MasterbuiltSmoker),
            cv.Optional(CONF_TEMPERATURE_UNIT, default="fahrenheit"): cv.one_of(
                "fahrenheit", "celsius", lower=True
            ),
            cv.Optional(CONF_CHAMBER_TEMPERATURE): _temperature_schema(),
            cv.Optional(CONF_TARGET_TEMPERATURE): _temperature_schema(),
            cv.Optional(CONF_COOK_TIME): _minutes_schema(),
            cv.Optional(CONF_TIME_REMAINING): _minutes_schema(),
            cv.Optional(CONF_SMOKER_MAC): cv.mac_address,
            cv.Optional(CONF_FORGET_PAIRING): button.button_schema(
                ForgetPairingButton,
                icon="mdi:bluetooth-off",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_DOOR): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_DOOR,
                icon="mdi:door",
            ),
            cv.Optional(CONF_TEMPERATURE_ERROR): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_PROBLEM,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_MEAT_PROBE_ERROR): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_PROBLEM,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_BROILER_AVAILABLE): binary_sensor.binary_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_SMOKE_ON_DEMAND_AVAILABLE): binary_sensor.binary_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_POWER): switch.switch_schema(
                SmokerPowerSwitch,
                icon="mdi:power",
                default_restore_mode="DISABLED",
            ),
            cv.Optional(CONF_LIGHT): switch.switch_schema(
                SmokerLightSwitch,
                icon="mdi:lightbulb",
                default_restore_mode="DISABLED",
            ),
            cv.Optional(CONF_CLIMATE): climate.climate_schema(SmokerClimate),
            cv.Optional(CONF_BROIL): select.select_schema(
                SmokerBroilSelect,
                icon="mdi:fire",
            ),
            cv.Optional(CONF_COOK_TIMER): number.number_schema(
                SmokerCookTimeNumber,
                unit_of_measurement=UNIT_MINUTE,
                icon="mdi:timer-outline",
            ),
            **{
                cv.Optional(key): number.number_schema(
                    SmokerProbeTargetNumber,
                    unit_of_measurement="°F",
                    device_class=DEVICE_CLASS_TEMPERATURE,
                )
                for key in CONF_PROBE_TARGETS
            },
            **{cv.Optional(p): _temperature_schema() for p in CONF_PROBES},
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    await esp32_ble_tracker.register_ble_device(var, config)

    celsius = config[CONF_TEMPERATURE_UNIT] == "celsius"
    unit = "°C" if celsius else "°F"

    async def temp_sensor(key):
        config[key][CONF_UNIT_OF_MEASUREMENT] = unit
        return await sensor.new_sensor(config[key])

    if CONF_SMOKER_MAC in config:
        cg.add(var.set_smoker_address(config[CONF_SMOKER_MAC].as_hex))
    if CONF_CHAMBER_TEMPERATURE in config:
        cg.add(var.set_chamber_temperature(await temp_sensor(CONF_CHAMBER_TEMPERATURE)))
    if CONF_TARGET_TEMPERATURE in config:
        cg.add(var.set_target_temperature(await temp_sensor(CONF_TARGET_TEMPERATURE)))
    if CONF_COOK_TIME in config:
        cg.add(var.set_cook_time(await sensor.new_sensor(config[CONF_COOK_TIME])))
    if CONF_TIME_REMAINING in config:
        cg.add(var.set_time_remaining(await sensor.new_sensor(config[CONF_TIME_REMAINING])))
    if CONF_FORGET_PAIRING in config:
        forget_pairing = await button.new_button(config[CONF_FORGET_PAIRING])
        cg.add(forget_pairing.set_parent(var))
    if CONF_DOOR in config:
        cg.add(var.set_door_sensor(await binary_sensor.new_binary_sensor(config[CONF_DOOR])))
    if CONF_TEMPERATURE_ERROR in config:
        cg.add(var.set_temp_error_sensor(await binary_sensor.new_binary_sensor(config[CONF_TEMPERATURE_ERROR])))
    if CONF_MEAT_PROBE_ERROR in config:
        cg.add(var.set_meat_probe_error_sensor(await binary_sensor.new_binary_sensor(config[CONF_MEAT_PROBE_ERROR])))
    if CONF_BROILER_AVAILABLE in config:
        cg.add(var.set_broiler_available_sensor(await binary_sensor.new_binary_sensor(config[CONF_BROILER_AVAILABLE])))
    if CONF_SMOKE_ON_DEMAND_AVAILABLE in config:
        cg.add(var.set_sod_available_sensor(await binary_sensor.new_binary_sensor(config[CONF_SMOKE_ON_DEMAND_AVAILABLE])))
    if CONF_POWER in config:
        power = await switch.new_switch(config[CONF_POWER])
        cg.add(power.set_parent(var))
        cg.add(var.set_power_switch(power))
    if CONF_LIGHT in config:
        light = await switch.new_switch(config[CONF_LIGHT])
        cg.add(light.set_parent(var))
        cg.add(var.set_light_switch(light))
    if CONF_CLIMATE in config:
        climate_conf = config[CONF_CLIMATE]
        climate_var = cg.new_Pvariable(climate_conf[CONF_ID])
        await climate.register_climate(climate_var, climate_conf)
        cg.add(climate_var.set_parent(var))
        # Visual range in Celsius (ESPHome climate is always Celsius internally). Defaults are a sane
        # smoker span in the configured unit; HA shows them back in the user's display unit.
        if celsius:
            cg.add(climate_var.set_visual_range_c(40.0, 160.0, 5.0))
        else:
            cg.add(climate_var.set_visual_range_c((100.0 - 32.0) * 5.0 / 9.0, (320.0 - 32.0) * 5.0 / 9.0, 5.0 * 5.0 / 9.0))
        cg.add(var.set_climate(climate_var))
    if CONF_BROIL in config:
        broil = await select.new_select(config[CONF_BROIL], options=BROIL_OPTIONS)
        cg.add(broil.set_parent(var))
        cg.add(var.set_broil_select(broil))
    if CONF_COOK_TIMER in config:
        cook_timer = await number.new_number(
            config[CONF_COOK_TIMER], min_value=0, max_value=1440, step=5
        )
        cg.add(cook_timer.set_parent(var))
        cg.add(var.set_cook_time_number(cook_timer))
    probe_target_max = 160 if celsius else 300
    for i, key in enumerate(CONF_PROBE_TARGETS):
        if key in config:
            config[key][CONF_UNIT_OF_MEASUREMENT] = unit
            n = await number.new_number(config[key], min_value=0, max_value=probe_target_max, step=5)
            cg.add(n.set_parent(var))
            cg.add(n.set_position(i + 1))
            cg.add(var.set_probe_target_number(i, n))
    for i, p in enumerate(CONF_PROBES):
        if p in config:
            cg.add(var.set_probe(i, await temp_sensor(p)))
