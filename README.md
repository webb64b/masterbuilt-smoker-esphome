# Masterbuilt Smoker → Home Assistant (ESPHome)

Bring your Bluetooth **Masterbuilt electric smoker** into Home Assistant with a cheap ESP32 and
[ESPHome](https://esphome.io) — live chamber temperature, set/target temperature, cook time,
time remaining, and up to four meat probes, as native Home Assistant sensors.

No cloud, no phone app left running on the counter, no account. The ESP32 talks to the smoker
directly over Bluetooth Low Energy and publishes everything to Home Assistant.

<p align="center">
  <img src="docs/images/smoker.jpeg" width="320" alt="Masterbuilt 30-inch digital electric vertical smoker">
</p>

## Does this work with my smoker?

This targets Masterbuilt electric smokers that pair over Bluetooth with the classic
**"Masterbuilt"** phone app. The real test is simple:

> If the **Masterbuilt app connects to your smoker over Bluetooth**, and the smoker shows up in a
> BLE scanner as **"Masterbuilt Smoker"**, this should work.

Under the hood these units use a **Bough Tech "BG536"** BLE module and the two-round handshake
documented in [`docs/protocol.md`](docs/protocol.md).

- **Confirmed:** 30" Digital Electric Vertical Smoker (the black cabinet with the front window and
  side wood-chip loader — Masterbuilt's `MB200704xx` / `MB20073519` family of Bluetooth verticals).
- **Probably works:** other Masterbuilt smokers that use the same Bluetooth app. Try it and open an
  issue with your model if it does (or doesn't).
- **Not this project:** Gravity Series / Wi-Fi / 940-series models that use a *different* app.

## What you need

- An **ESP32** dev board (any common `esp32dev` board works) — kept within Bluetooth range of the
  smoker and powered by USB.
- [ESPHome](https://esphome.io/guides/installing_esphome.html) (the Home Assistant add-on is easiest).

## Setup

### 1. Create your config

Copy [`smoker.example.yaml`](smoker.example.yaml) and add a `secrets.yaml` with your Wi-Fi
credentials. The config pulls this component straight from GitHub:

```yaml
external_components:
  - source: github://webb64b/masterbuilt-smoker-esphome
    components: [masterbuilt_smoker]
```

### 2. Flash and pair (one time)

Flash the ESP32 over USB. Then put the smoker in **pairing mode** once while the ESP32 is running.

Use whatever pairing step your smoker's manual describes — it's the same one you'd use to connect the
phone app. On the 30" digital electric vertical, that's **pressing and holding the Bluetooth/pairing
button on the control panel until the display reads "pair."** While the smoker is in pairing mode the
ESP32 discovers it, captures the pairing code, completes the handshake, and **saves the smoker's
identity to flash**.

From then on the ESP32 reconnects on its own — after a reboot, a power cycle, anything — with **no
pairing button**, exactly like the phone app's "connect." You only pair again if you reset the
device or move it to a different smoker.

> **Heads up:** the smoker only allows **one** Bluetooth connection at a time. While the ESP32 is
> connected, the phone app won't be able to connect, and vice-versa.

> **Safety:** out of the box this is read-only — it just reads telemetry. It can optionally control
> the smoker too (see [Controlling the smoker](#controlling-the-smoker-optional)); if you enable that,
> read the safety note there. The Bluetooth link is plain/unencrypted, so keep the ESP32 within your
> own trusted environment, and never treat Home Assistant as a safety interlock or a substitute for
> supervising the smoker.

### Re-pairing or moving to another smoker

The smoker identity is saved in ESPHome preferences after first pairing. If you move the ESP32 to a
different smoker, press the **Forget Saved Smoker Pairing** button in Home Assistant, then put the new
smoker in pairing mode while the ESP32 is running.

### Optional: pin a specific smoker

By default the ESP32 locks onto the first compatible smoker advertisement it sees. If more than one
compatible smoker is in Bluetooth range, set `smoker_mac` under `masterbuilt_smoker`:

```yaml
masterbuilt_smoker:
  id: smoker
  ble_client_id: smoker_client
  smoker_mac: BC:33:AC:E0:E1:27
```

## Sensors exposed

| Sensor | Notes |
| --- | --- |
| `Chamber Temperature` | Current cabinet temperature (°F) |
| `Target Temperature` | Set temperature (0 when no cook is set) |
| `Cook Time` | Set cook time (minutes) |
| `Time Remaining` | Remaining cook time (minutes) |
| `Meat Probe 1`–`4` | Probe temperatures; `unknown` when a probe isn't plugged in |

## Controlling the smoker (optional)

Beyond reading temperatures, the component can drive the smoker the way the phone app does. These
controls are optional — leave them out of your config and the integration stays read-only.

| Control | What it does |
| --- | --- |
| `Smoker` (climate) | Off / Heat with an adjustable target temperature — the bottom (smoke) element |
| `Broil` (select) | Off / Low / Medium / High — the top (broiler) element |
| `Cook Timer` (number) | Cook time in minutes |
| `Probe 1 Target` (number) | Meat-probe alarm temperature |
| `Door` (binary sensor) | Open / closed |
| `Temperature Error` (binary sensor) | Sensor fault flag |

The smoke (bottom) and broil (top) elements are **mutually exclusive**, just like the smoker's own
panel: turning broil on turns the smoke off (the climate reads Off), and turning the climate on turns
broil off. The broiler is a Low/Medium/High level, not a free temperature — that's all the smoker
accepts over Bluetooth.

> **Safety.** These controls really do power the smoker on, set its temperature, and run the broiler.
> The smoker enforces its own door interlock — it will not start heating with the door open — and this
> component relies on that rather than second-guessing it. Do not treat Home Assistant as a safety
> interlock or a substitute for supervising the smoker. The Bluetooth link is plain and unencrypted,
> so keep the ESP32 in your own trusted environment.

## How it works

The smoker speaks an undocumented two-round challenge/response over a plain (unencrypted) BLE GATT
link. The full protocol — the keyed handshake, the deliberate mid-handshake disconnect, the
telemetry frame format — is written up in [`docs/protocol.md`](docs/protocol.md).

The implementation assumes the Bough Tech BG536 handle layout documented there. If a different
Masterbuilt variant advertises as "Masterbuilt Smoker" but uses different GATT handles, open an
issue with the model number and an ESPHome debug log.

## License

[MIT](LICENSE).
