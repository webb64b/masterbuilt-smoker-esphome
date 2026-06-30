# Masterbuilt legacy smoker: command and status reference

A byte-level reference for the legacy Masterbuilt smoker protocol, worked out from the decompiled
official app (the `PrepareLegacyDeviceBleSettings`, `PrepareDeviceBleSettingsUseCase`,
`BaseDeviceStateNotificationParsingStrategy`, `AppConstants`, `BroilerLevel` and `SmokeOnDemandLevel`
classes) alongside the HCI snoop capture.

This is the companion to [`protocol.md`](protocol.md), which covers the GATT layout and the keyed
handshake. This file covers every command and the full status frame, including the broiler and
smoke-on-demand controls that the ESPHome component does not drive yet, so the byte layout is on hand
if anyone wants to add them.

## Settings command (0xA1)

Written to `fff3`. Full-state: a single write carries power, temperature, time, broil and
smoke-on-demand together. The buffer length depends on what the device reports it has:

- Base 3 bytes minimum
- Smoke-on-demand capable: add 6 (9 total)
- Broiler capable: add 5 (8 total)
- Door capable: add 4 (7 total)
- A power-only or temperature-unit command is always 3 bytes (capabilities ignored)

Byte layout, little-endian:

| Byte | Field | Notes |
| --- | --- | --- |
| 0 | command type | `0xA1` |
| 1 | flags1 | bit0 Fahrenheit, bits1-2 smoke element on, bit3 broiler on, bit4 smoke-on-demand on |
| 2 | flags2 | bit0 power, bit1 light, bit2 heat/cook active, bit3 broiler, bit4 smoke-on-demand |
| 3-4 | set time | int16 LE, minutes |
| 5-6 | set temp | int16 LE, rounded to whole degrees |
| 7 | broiler level | for a broiler command |
| 8 | smoke-on-demand level | for a smoke-on-demand command |

Example, power on / Fahrenheit / 225°F on a broiler-capable unit (8 bytes):
`A1 01 01 00 00 E1 00 00`.

The smoke (bottom) and broiler (top) elements are mutually exclusive in the app: a broiler command
sets the broiler bits and clears the cook bit.

## Broiler level (byte 7)

`0` Off, `1` Low, `2` Medium, `3` High. A broiler-on command sets flags1 bit3 and flags2 bit3 and
writes the level to byte 7; broiler-off writes `0`.

## Smoke-on-demand level (byte 8)

`0` Off, `1` through `5`. Set by the smoke-on-demand command (flags1 bit4, flags2 bit4). The ESPHome
component does not expose this, but the byte above is all that is needed to add it.

## Probe targets (0xA3)

Written to `fff3`. Layout, little-endian:

| Byte | Field | Notes |
| --- | --- | --- |
| 0 | command type | `0xA3` |
| 1 | enable bitset | bit N-1 set means probe N has a target following |
| 2+ | targets | int16 LE per probe, at `(position-1)*2 + 2` |

Buffer size is `probes * 2 + 2` (4 probes = 10 bytes). Targets are in the current unit (F or C).

Example, probe 1 to 165°F and probe 2 to 180°F: `A3 03 A5 00 B4 00 00 00 00 00`.

## Recall settings (0xA2)

Three bytes, `A2 00 00`. Tells the smoker to load its previously stored settings from device memory.
Not used by this component.

## Status frame (0xB2 notification)

Arrives on `fff4`. A variable-length frame whose three leading flag bytes say which fields are present.
Temperatures are Android GATT SFLOAT; for the normal range the value is the low 12 bits of the
little-endian field.

Flag byte 1 (offset 1) presence bits:

| Bit | Meaning |
| --- | --- |
| 0 | temperature unit (1 Fahrenheit, 0 Celsius) |
| 1 | chamber temperature present (offset 4) |
| 2 | meat probe temperature present (offset 6) |
| 3 | remaining time present (offset 8) |
| 4 | set time present (offset 11) |
| 5 | set temperature present (offset 13) |
| 6 | broiler level present (offset 15) |
| 7 | smoke-on-demand level present (offset 16) |

Flag byte 2 (offset 2) control bits:

| Bit | Meaning |
| --- | --- |
| 0 | power on |
| 1 | light on |
| 2 | heat/cook active |
| 3 | door open (1 open, 0 closed) |
| 4 | bluetooth connected |
| 5 | broiler on |
| 6 | smoke-on-demand on |

Flag byte 3 (offset 3) capability and error bits:

| Bit | Meaning |
| --- | --- |
| 0 | temperature error |
| 1 | meat probe error |
| 2 | broiler available (device has a broiler) |
| 3 | smoke-on-demand available (device has SOD) |

Data fields, by offset:

| Offset | Field | Format |
| --- | --- | --- |
| 4 | chamber temperature | SFLOAT |
| 6 | meat probe temperature | SFLOAT |
| 8 | remaining time | uint16 LE, minutes |
| 11 | set time | uint16 LE, minutes |
| 13 | set temperature | SFLOAT |
| 15 | broiler level | uint8 |
| 16 | smoke-on-demand level | uint8 |

Minimum frame is 4 bytes (flags only); maximum is 17; typical is 14 to 16.

## Door interlock

The door check is app-side only. `isDoorOpen()` is true when the device has a door capability whose
control is On (so Control.On means the door is open). When open, the app refuses to send control
commands and returns a door-open error rather than relying on the firmware to reject them. Only legacy
devices enforce this; newer config and Wi-Fi devices skip the door check.
