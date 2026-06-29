# Changelog

Notable changes to the Masterbuilt smoker ESPHome component and dashboard card.

## Unreleased

### Added

- Dashboard card (`card/masterbuilt-smoker-card.js`). A single Lovelace panel for the smoker: a power
  button when it's off, and the full controls (temperature, smoke and broil, probes, cook timer, door)
  when it's on. Every entity except the climate is optional, so it fits any smoker, and it shows a
  spinner while a command is in flight so taps don't pile up.
- Master power switch. Turning it on brings the smoker up to idle/ready; turning it off shuts it down.
  Starting smoke or broil powers it on automatically.
- Control entities: a climate for the smoke element (off/heat with a target temperature), a broil
  select (Off/Low/Medium/High), a cook-timer number, a probe-target number, and door and
  temperature-error binary sensors.
- Support for more smoker models, all optional: a cabinet `light` switch, a `meat_probe_error` sensor,
  `broiler_available` and `smoke_on_demand_available` diagnostic sensors, and probe targets for all four
  probes. The light and capability features are built from the protocol and not yet tested on hardware.
- Celsius support via `temperature_unit: celsius`, which adapts the sensor labels and target ranges; the
  temperatures themselves already follow whatever unit the smoker reports.

### Notes

- The smoker has no "stop the heat but stay powered" command over Bluetooth, so stopping a cook powers
  the smoker off, the same thing the phone app does. Power it back on to return to idle.
- Temperatures only report while the smoker is powered. An off smoker shows no reading instead of a
  stale leftover number.

## Earlier

- Automatic discovery: the component finds the smoker from its Bluetooth advertisement, so the config
  needs no MAC address.
- Zero-button reconnect: after the first pairing the ESP32 reconnects on its own, since the smoker
  identity is saved to flash.
- Read-only telemetry: chamber temperature, target temperature, cook time, time remaining, and up to
  four meat probes as Home Assistant sensors.
