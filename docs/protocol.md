# Masterbuilt smoker BLE protocol

Notes from reverse-engineering the Bluetooth protocol of a Masterbuilt electric smoker (Bough Tech
"BG536" module), worked out from a Bluetooth HCI snoop capture of the official app plus a read of
the app's own decoding logic. Everything here is implemented in
[`../components/masterbuilt_smoker/masterbuilt_smoker.h`](../components/masterbuilt_smoker/masterbuilt_smoker.h).

## GATT layout

Service `426f7567-6854-6563-2d57-65694c69fff0`:

| Char | UUID suffix | Handle | Role |
| --- | --- | --- | --- |
| fff1 | `…fff1` | `0x0025` | write — commands |
| fff2 | `…fff2` | `0x0027` | read — challenge / ack responses |
| fff3 | `…fff3` | `0x0029` | write — set-point commands |
| fff4 | `…fff4` | `0x002b` | notify — telemetry (CCCD `0x002c`) |

The current ESPHome component uses these fixed handles. That matches the verified BG536 module, but
a future variant with the same UUIDs and different handles would need UUID-based discovery.

The link is **plain and unencrypted** — there is no BLE bonding/pairing (SMP) at all. This matters:
a Bluetooth *proxy* can't be used, because the "pairing" is entirely application-level. You need a
BLE *client* that can write to fff1 on an unbonded connection.

## The keyed handshake

All handshake writes to fff1 have the form `0e 08` followed by 8 bytes, where the 8 bytes are an
XOR of a per-message secret against a fixed key:

```
MAC_KEY = 42 48 11 22 33 44 55 66      # company id 0x4842 (LE) + a fixed placeholder
```

### Pairing mode and the rotating code

While the smoker is in **pairing mode** (pairing button on the panel), it broadcasts manufacturer
data (company id `0x4842`) whose last 8 bytes are a **pairing code that rotates every session**.
Outside pairing mode those bytes are a constant placeholder and are useless.

### Round 1 — first-time pairing

```
W fff1   0e08 + XOR(MAC_KEY, pair_code)
R fff2   0a08 + grill_half          # the smoker's fixed 8-byte identity for this unit
W fff1   0e08 + grill_half          # echo it straight back
R fff2   0b ..                      # ack
W fff4   CCCD = 0100                # subscribe to telemetry
W fff1   0f 00 00                   # "start"
```

Immediately after the `0f0000` write the smoker **deliberately drops the connection**
(`HCI Disconnect`, reason `0x13` / remote-terminated). This is normal — the official app gets the
same disconnect and simply reconnects.

### Round 2 — reconnect (and every session after the first)

On the *next* connection the smoker no longer offers the rotating code; instead the client proves
itself with the `grill_half` it learned in round 1:

```
W fff1   0e08 + XOR(MAC_KEY, grill_half)
R fff2   0a08 + grill_half
W fff4   CCCD = 0100                # telemetry starts streaming
```

Because the `grill_half` is fixed per unit, a client that has paired once can store it and reconnect
forever without the pairing button — which is exactly what this component does (it saves the
`grill_half` to flash on first pairing).

### A timing gotcha: fff2 is filled asynchronously

After a write to fff1, the smoker computes its `fff2` response **asynchronously** — read it back too
quickly and you get 20 bytes of zeros instead of the real `0a08`/`0b` value. The component handles
this by re-reading fff2 a few times (every 250 ms, up to ~12 attempts) until a valid response
appears. Without that retry, first-time pairing fails intermittently.

### Advertisement / pairing-code note

The smoker advertises BLE manufacturer data with company id `0x4842`, both idle and in pairing
mode. The component uses that advertisement to discover the smoker's Bluetooth address at runtime.

The pairing code also lives in that manufacturer advertisement while the smoker is in pairing mode.
When ESPHome parses the advertisement it strips the 2-byte company id, so the manufacturer payload
it hands you is **6 bytes when idle** and **14 bytes in pairing mode** (a 6-byte tail + the 8-byte
rotating code). The code is the last 8 bytes; XOR it with `MAC_KEY` to form the round-1 write.

## Telemetry frames (fff4 notifications)

Temperatures are IEEE-11073 16-bit **SFLOAT**, but for the normal temperature range the exponent is
0, so the value is just the low 12 bits of the little-endian 16-bit field.

### `0xB2` — device state

```
B2 .. flags .. [chamber:LE16@4] [probe:LE16@6] [remaining:LE16@8] .. [set_min:LE16@11] [set_temp:LE16@13] ..
```

- `chamber` (offset 4) — current cabinet temperature
- `remaining` (offset 8) — minutes left on the timer
- `set_min` (offset 11) — set cook time
- `set_temp` (offset 13) — set/target temperature

### `0xB3` — meat probes

```
B3 [flags] [probe1:LE16@2] [probe2:LE16@4] [probe3:LE16@6] [probe4:LE16@8] [targets @10/12/14/16]
```

`flags` (byte 1) is a bitmask: bit *i* set means probe *i+1* is present. Probe temperatures are at
offsets 2/4/6/8, their per-probe target temperatures at 10/12/14/16. The component currently
publishes probe temperatures only.

## Control commands (fff3)

Control is written to `fff3` (`UUID_SMOKER_SETTINGS`). The settings command is full-state — one write
carries power, temperature, time and broil together:

```
A1 [flags1] [flags2] [cookTime:LE16] [setTemp:LE16] [broilLevel]
flags1: bit0=Fahrenheit  bit1&2=smoke/heat element on  bit3=broil
flags2: bit0=power  bit2=cook active  bit3=broil
broilLevel: 0=off, 1=Low, 2=Medium, 3=High
```

The smoke (bottom) and broil (top) elements are mutually exclusive: a broil command sets the broil
bits and clears the cook bit, so turning broil on turns the smoke element off. Power on plus the smoke
target heats the bottom element; the broiler runs by level only — the smoker does not accept a broil
temperature over Bluetooth. Example: power on, Fahrenheit, heat to 225°F is `A1 07 05 00 00 E1 00 00`.

Meat-probe alarm targets are a separate command, `A3 [enableBits] [p1:LE16] [p2:LE16] ...`, where each
enable bit marks a probe whose target follows. The status frame does **not** report a usable broil
level back (byte 15 is stale and the broil-control bit is not set while broiling), so a controller
should track the commanded broil level itself.
