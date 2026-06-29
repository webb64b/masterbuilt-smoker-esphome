import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import (
    ble_client,
    binary_sensor,
    button,
    climate,
    esp32_ble_tracker,
    sensor,
)
from esphome.const import (
    CONF_ID,
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
AUTO_LOAD = ["binary_sensor", "button", "climate", "sensor"]

masterbuilt_smoker_ns = cg.esphome_ns.namespace("masterbuilt_smoker")
MasterbuiltSmoker = masterbuilt_smoker_ns.class_(
    "MasterbuiltSmoker",
    cg.Component,
    ble_client.BLEClientNode,
    esp32_ble_tracker.ESPBTDeviceListener,
)
ForgetPairingButton = masterbuilt_smoker_ns.class_("ForgetPairingButton", button.Button)
SmokerClimate = masterbuilt_smoker_ns.class_("SmokerClimate", climate.Climate)

CONF_CHAMBER_TEMPERATURE = "chamber_temperature"
CONF_TARGET_TEMPERATURE = "target_temperature"
CONF_COOK_TIME = "cook_time"
CONF_TIME_REMAINING = "time_remaining"
CONF_FORGET_PAIRING = "forget_pairing"
CONF_SMOKER_MAC = "smoker_mac"
CONF_PROBES = ["probe_1", "probe_2", "probe_3", "probe_4"]
CONF_DOOR = "door"
CONF_TEMPERATURE_ERROR = "temperature_error"
CONF_CLIMATE = "climate"


def _temperature_schema():
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
            cv.Optional(CONF_CLIMATE): climate.climate_schema(SmokerClimate),
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

    if CONF_SMOKER_MAC in config:
        cg.add(var.set_smoker_address(config[CONF_SMOKER_MAC].as_hex))
    if CONF_CHAMBER_TEMPERATURE in config:
        cg.add(var.set_chamber_temperature(await sensor.new_sensor(config[CONF_CHAMBER_TEMPERATURE])))
    if CONF_TARGET_TEMPERATURE in config:
        cg.add(var.set_target_temperature(await sensor.new_sensor(config[CONF_TARGET_TEMPERATURE])))
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
    if CONF_CLIMATE in config:
        climate_conf = config[CONF_CLIMATE]
        climate_var = cg.new_Pvariable(climate_conf[CONF_ID])
        await climate.register_climate(climate_var, climate_conf)
        cg.add(climate_var.set_parent(var))
        cg.add(var.set_climate(climate_var))
    for i, p in enumerate(CONF_PROBES):
        if p in config:
            cg.add(var.set_probe(i, await sensor.new_sensor(config[p])))
