# LoRaWAN Water Node – Spring & Reservoir Monitor (Heltec WiFi LoRa V3)

Firmware for a custom-built PCB based on the **Heltec WiFi LoRa 32 V3** (ESP32-S3).  
The node measures flow rate, water level, and process temperatures at a natural spring or water reservoir, and transmits all data via **LoRaWAN (OTAA, region configurable; default: EU868)** to a ChirpStack server. The BME280 values are used mainly for enclosure health monitoring (humidity ingress + electronics thermal state), not as a primary water-process sensor.

---

## Project Photos

<p align="center">
     <img src="pictures/overview.jpeg" alt="Water node project overview" width="32%" />
     <img src="pictures/pcb-protype.jpeg" alt="PCB prototype" width="32%" />
     <img src="pictures/grafana.jpeg" alt="Grafana dashboard" width="32%" />
</p>

<p align="center">
     <sub><strong>Overview:</strong> Complete field setup</sub><br/>
     <sub><strong>PCB Prototype:</strong> Board and wiring stage</sub><br/>
     <sub><strong>Grafana:</strong> Live telemetry and trends</sub>
</p>

---

## Quick Navigation

- [Use Case](#use-case)
- [Hardware](#hardware)
- [GPIO Pinout](#gpio-pinout)
- [CayenneLPP Payload](#cayennelpp-payload-channels)
- [Building & Flashing](#building--flashing)
- [Initial Configuration](#initial-configuration-wifi-portal)
- [Downlink Commands](#downlink-commands-port-1)
- [Power Saving & Consumption](#power-saving-measures)
- [ChirpStack Decoder](#chirpstack-decoder)

---

## 📄 License & Open Source

This project is fully open-source. To support both software developers and hardware makers, it is dual-licensed:

* **Software / Firmware:** Licensed under the **[MIT License](LICENSE-SOFTWARE)**. You can freely use, modify, and distribute the code.
* **Hardware (Schematics, PCB & 3D Files):** Licensed under the **[CERN-OHL-P v2](LICENSE-HARDWARE)** (CERN Open Hardware Licence – Permissive). You are free to manufacture, modify, and distribute the hardware designs.

*Developed with 🚜 and 💻 by Gerhard Massenbichler at Franzenhof.*


## Use Case

```
[LiFePO4 4s] ──► [Victron MPPT] ──► charge battery
                                         │
                              [DALY Smart BMS] (protection)
                                         │
                              [DC-DC 12V → 5V] ──► Board (+5V)
                                         │
                              [Heltec WiFi LoRa V3]
                                    │        │
               [Flow sensor, e.g. ZJ-B5]   [DS18B20 x3]
                          [4-20mA Level]   [BME280 enclosure monitor]
                          [Victron VE.Direct]
                          [DALY BMS UART]
                                    │
                               [LoRaWAN]
                                    │
                            [ChirpStack Server]
```

Every 5 minutes the node wakes from deep sleep, samples for approximately 15 seconds (averaging), and sends a LoRaWAN uplink. It then returns to deep sleep. The DALY BMS is read less frequently (configurable interval, default every 6th cycle = 30 minutes).  
If `sleepSec=0`, deep sleep is disabled: the node stays awake, samples continuously over the configured continuous uplink interval, forms window averages, and sends regularly at that interval. In this mode, flow, 4–20 mA level, DS18B20, BME280, and Victron V/P/I are averaged across the whole measurement window. Flow pulses are captured without gaps in continuous mode (including during uplink/processing phases), so no liters are lost between cycles.  
> **Note:** In continuous mode (`sleepSec=0`), `samplingMs` (Max sampling time) is ignored — the measurement window is governed solely by `continuousSendSec` (Continuous uplink interval).

**Display power management (automatic, no configuration needed in deep-sleep mode):**
- **Deep sleep (`sleepSec>0`):** the display is **off** on a normal timer wakeup (silent, unattended cycle), and automatically turned **on** for a fresh power-on boot or an EXT0 button wakeup — staying on until the measurement is done and the uplink has been sent, then switching off again before the next deep sleep. This is fixed behavior and not user-configurable, since keeping the display on during unattended timer cycles would only waste battery power for no benefit.
- **Continuous mode (`sleepSec=0`):** since the node never deep-sleeps, there is no wakeup event to hook into. Instead, the display stays on for `displayOnCycles` measurement cycles (configurable, default 5; **`0` = always on, no auto-off**) after boot or after a **short press** of the user button (GPIO 0), then turns off automatically until the button is pressed again. A short press is recognized even while a (potentially long) measurement window is running, so the display reacts immediately rather than waiting for the next full cycle.

If the display is on, it doesn't just show a static "Measuring..." message: it updates once per second during the whole measurement window with a countdown, the running flow rate, the running 4–20 mA level average, Victron/DALY BMS reception status (`...` while waiting, `OK` once received), and — in continuous mode — the live DS18B20 temperatures. To keep the ~750 ms 12-bit conversion from stalling the ADC/serial sampling, the DS18B20 are read **asynchronously** in continuous mode (a fresh conversion is started right after each read). At uplink time, the continuous-mode payload uses the window averages (instead of last-sample values) for flow, level, DS18B20, BME280, and Victron V/P/I. In deep-sleep mode the sensors are read once (blocking) after the short measurement window to save power. So the user always sees live feedback instead of a frozen screen.

All measurement screens (live "Measuring…", the final results screen, and the "Sending uplink…" screen) list values in a fixed **priority order (most important first): Flow → DS18B20 temperatures → Level → BME280 → Victron → DALY → rest (LiPo)**. Lines are drawn compactly (only present sensors); on the 64 px panel the lowest-priority values drop off the bottom first if everything is enabled at once. The "Sending uplink…" screen keeps the measured values visible under the header instead of blanking the screen. If known, RF quality values (last downlink RSSI/SNR and LinkCheck results) are appended as lowest-priority commissioning info.  
The LoRaWAN session is stored in RTC RAM, so after a timer or EXT0 wakeup **no OTAA rejoin** is normally required.

---

## Hardware

| Component | Function |
|-----------|----------|
| Heltec WiFi LoRa 32 V3 | Microcontroller (ESP32-S3), LoRa SX1262 |
| Flow sensor (open collector) | any flow sensor with open-collector output (e.g. ZJ-B5) |
| DS18B20 × 3 | 1-Wire temperature sensors (spring, tank floor, air) |
| BME280 | Enclosure health monitoring: humidity ingress indicator, enclosure/electronics temperature trend, plus barometric pressure as an additional byproduct value (I²C) |
| Victron MPPT | Solar charge controller, read via VE.Direct (serial) |
| DALY Smart BMS | Battery protection, read via UART |
| ADuM121N1 × 2 | Digital isolators (galvanic isolation for Victron + DALY) |
| HCPL-260L | Optocoupler for flow-sensor pulses (open collector, e.g. ZJ-B5) (galvanic isolation) |
| PC817 | Optocoupler for DALY S1 wakeup |
| FQP27P06 | P-channel MOSFET high-side switch (3.5–40 V) |
| LP0701N3 × 2 | P-channel MOSFETs, switch VCC3.3 for sensors (GPIO38) |
| TN2106N3-G × 4 | N-channel MOSFETs, gate drivers for P-channel MOSFETs |
| 78L05 / 78L33 | Optional voltage regulators for DALY interface (jumper-selectable) |
| DC-DC 12V → 24V | Supply for 4–20 mA level sensor, switchable via high-side switch |

> **PCB component selection note:** For the flow-sensor optocoupler, the Lite-On 6N137-L is a pin-compatible alternative to the HCPL-260L and also works reliably with 3.3 V VCC (see datasheet). It is often available at a fraction of the cost and is more than sufficient for flow-sensor pulse isolation.

> **PCB component selection note:** For the digital isolators, the Texas Instruments ISO7721 can be used as a lower-cost pin-compatible alternative to the ADuM121N1. Use the standard ISO7721, not the F-variant, so the fail-safe state remains HIGH. It uses capacitive isolation instead of iCoupler magnetic isolation, but is suitable for the same job. As an absolute low-cost option, the Chipanalog CA-IS3721HS is also worth considering; please verify the Chipanalog datasheet and pinout before committing to the PCB.

---

## GPIO Pinout

| GPIO | Function | Direction | Notes |
|------|----------|-----------|-------|
| **0** | User button | IN (pull-up) | After boot: hold 3 s → config portal; during sleep: EXT0 wakeup |
| **1** | DALY BMS S1 wakeup | OUT | Via PC817 (inverted: HIGH = S1 active / LOW) |
| **2** | 4–20 mA level sensor ADC | IN (ADC) | Shunt R4 = 120 Ω → 4 mA = 0.48 V / 20 mA = 2.4 V |
| **3** | 1-Wire bus | IN/OUT | DS18B20 × 3, pull-up 2.2 kΩ (10 m cable) |
| **4** | Victron VE.Direct RX | IN (UART1) | Via ADuM121N1, 19200 baud |
| **5** | Victron VE.Direct TX | OUT (UART1) | Via ADuM121N1 |
| **6** | DALY BMS UART RX | IN (UART2) | Via ADuM121N1, 9600 baud |
| **7** | DALY BMS UART TX | OUT (UART2) | Via ADuM121N1 |
| **38** | VCC3.3 switched | OUT | LOW = on; powers HCPL-260L + 1-Wire supply. Output (VCC3.3_SW) drives TN2106N3-G → LP0701N3(2) for the flow-sensor 5V rail (e.g. ZJ-B5) |
| **41** | I²C SDA | IN/OUT | BME280 (address 0x76) |
| **42** | I²C SCL | OUT | BME280 |
| **47** | High-side switch | OUT | HIGH = on; connects 12 V to DC-DC 12→24 V (TN2106N3-G → FQP27P06) |
| **48** | Flow sensor pulses | IN (PCNT) | flow sensor (open collector, e.g. ZJ-B5) via HCPL-260L, hardware pulse counter |

---

## CayenneLPP Payload Channels

| Channel | Type | Content | Unit | Active when |
|---------|------|---------|------|------------|
| 1 | Analog Input | 4–20 mA current value | mA | `levelEn = 1` |
| 2 | Analog Input | Flow rate | L/min | always |
| 3 | Temperature | DS18B20 #1 (spring) | °C | sensor present |
| 4 | Temperature | DS18B20 #2 (tank floor) | °C | sensor present |
| 5 | Temperature | DS18B20 #3 (air above tank) | °C | sensor present |
| 6 | Temperature | BME280 enclosure temperature | °C | BME280 present |
| 7 | Humidity | BME280 relative humidity | % | BME280 present |
| 8 | Barometric Pressure | BME280 pressure (QNH if altitude configured, otherwise absolute) | hPa | BME280 present |
| 9 | Analog Input | LiPo voltage (`bmsMode = 2`) **or** DALY status 0/1 (`bmsMode = 1`) | V / flag | `bmsMode = 1` or `2` |
| 10 | Analog Input | Victron battery voltage | V | `victronEn = 1` + Victron interval cycle |
| 11 | Analog Input | Victron PV power | W | `victronEn = 1` + Victron interval cycle |
| 12 | Analog Input | Victron charge current | A | `victronEn = 1` + Victron interval cycle |
| 13 | Analog Input | Victron charge state (CS): 3=Bulk, 4=Absorb, 5=Float | — | `victronEn = 1` + Victron interval cycle |
| 14 | Analog Input | Victron yield today | kWh | `victronEn = 1` + Victron interval cycle |
| 15 | Analog Input | DALY pack voltage | V | `bmsMode = 1` + BMS responds |
| 16 | Analog Input | DALY state of charge | % | `bmsMode = 1` + BMS responds |
| 17 | Analog Input | DALY charge/discharge current (+/−) | A | `bmsMode = 1` + BMS responds |
| 18 | Temperature | DALY battery temperature (avg) | °C | `bmsMode = 1` + BMS responds |
| 19 | Analog Input | DALY cell voltage spread (max−min) | mV | `bmsMode = 1` + BMS responds |
| 20 | Digital Input | Diagnostic bitmask (Bit0=Victron missing, Bit1=DALY missing, Bit2=DS18B20 error, Bit3=ADC out of 4–20 mA range, Bit4=last reset was WDT) | Bitmask | always |
| 200 | Digital Input | Debug cycle counter (0..255) | count | `debugMode = 1` |
| 202 | Digital Input | Debug send fail counter (0..255) | count | `debugMode = 1` |
| 203 | Analog Input | Debug free heap | KiB | `debugMode = 1` |
| 204 | Analog Input | Debug uptime | h | `debugMode = 1` |
| 205 | Analog Input | Debug downlink RSSI | dBm | `debugMode = 1` + downlink received |
| 206 | Analog Input | Debug downlink SNR | dB | `debugMode = 1` + downlink received |
| 207 | Analog Input | Debug LinkCheck margin | dB | `debugMode = 1` + LinkCheck answer received |
| 208 | Digital Input | Debug LinkCheck gateway count | count | `debugMode = 1` + LinkCheck answer received |

> **Payload size:** 51 bytes (base configuration without Victron/DALY) to 75 bytes (full configuration).  
> From 52 bytes onwards **LoRaWAN DR3 (SF9)** is required — no issue with a private ChirpStack at normal gateway distance.
>
> **Reducing average payload size:** slow-changing battery/PV/BMS telemetry can be sent less often than the fast water values. The **DALY** channels (15–19) already follow `DALY interval`, and the **Victron** channels (10–14) follow the separate `Victron interval` (cycles). Setting e.g. `Victron interval = 6` appends the ~20 bytes of Victron data only every 6th uplink; the other uplinks stay small (level, flow, temperatures) and typically drop below the 52-byte DR3 threshold. A button (EXT0) wakeup always forces a full send. Victron is still *read* every cycle for the diagnostic status flag (Ch20 Bit0) — only the payload append is throttled.

### Payload format: CayenneLPP or compact binary

The `Payload format` portal parameter selects how uplinks are encoded:

| Value | Format | FPort | Typical size (full config) |
|-------|--------|-------|----------------------------|
| `0` (default) | CayenneLPP (self-describing, standard tooling) | 1 | ~78 B |
| `1` | Compact custom binary | 3 | ~39 B (≈50% smaller) |

The binary format carries **exactly the same values** at the same resolution, but drops the 2-byte-per-field CayenneLPP overhead. Layout on **FPort 3**:

```
Byte 0      Protocol version (0x02)
Byte 1..4   32-bit field-presence bitmask (little-endian)
Byte 5..N   Data for present fields, in ascending bit order
```

The daily uplink (FPort 2) always stays CayenneLPP. The matching decoder in [`chirpstack/decoder.js`](chirpstack/decoder.js) handles **all three ports** (1 = CayenneLPP telemetry, 2 = daily, 3 = binary), so the same decoder works regardless of the selected format. A **version-matched copy of this decoder can be downloaded directly from the WiFi portal** (menu item *“Payload decoder (JS)”*) — it is generated from the exact firmware build, which guarantees the binary layout matches.

### Daily Uplink (Port 2)

Every **288 cycles** (≈ 24 h at 5-minute sleep interval) the node sends an additional uplink on **Port 2**:

| Channel | Type | Content | Unit |
|---------|------|---------|------|
| 1 | Analog Input | Total volume today | L |
| 2 | Analog Input | Maximum flow rate today | L/min |
| 3 | Temperature | Minimum DS18B20 #1 temperature today | °C |
| 4 | Temperature | Maximum DS18B20 #1 temperature today | °C |

---

## Building & Flashing

A single PlatformIO environment (`heltec_v3`) covers all hardware variants — Victron, DALY BMS, HighSide/24V switch, 4–20 mA level sensor, and flow sensor are all enabled by default and can be individually turned on or off **at runtime** via the WiFi configuration portal. Separate build-time variants are unnecessary, since the very first boot (empty NVS) always opens the config portal anyway, where every parameter — including which sensors/features are present on your hardware — is set interactively.

```bash
pio run -e heltec_v3      # build
pio run -e heltec_v3 -t upload  # build + flash
```

> Upgrading from an older version with separate `full` / `lipo` / `lipo_level` environments? Just flash the new `heltec_v3` build — your existing configuration in NVS is untouched, and any feature toggles (Victron, BMS, HighSide, level, flow) can still be changed afterwards via the WiFi portal.

### Firmware Versioning

Each build is automatically stamped with a version derived from Git. The pre-build script [`tools/version.py`](tools/version.py) runs `git describe --tags --dirty --always` and generates `include/generated_version.h` with a `FIRMWARE_VERSION` define. This version is shown in the WiFi configuration portal.

After a successful build the compiled binary is copied to the `firmware/` folder as `firmware/lorawan-water-node-<version>.bin`, so every build is archived with its exact version.

* Version comes from the latest Git tag (e.g. `v1.0.0`). Create a new release version with `git tag v1.1.0`.
* A `-dirty` suffix (e.g. `v1.0.0-dirty`) indicates uncommitted changes at build time. These `*-dirty.bin` files are git-ignored, so only clean, tagged release builds are checked into the `firmware/` folder. For a clean release build, commit all changes first.
* You can override the version explicitly via the `FW_VERSION` environment variable, e.g. `FW_VERSION=1.2.3 pio run`.

---

## Initial Configuration (WiFi Portal)

1. **Power on the device** – a hint screen appears for 4 seconds: hold the user button within this window for 3 seconds to open the portal (or automatically on first boot when no DevEUI is stored yet)
2. Connect to the **"WaterNode"** WiFi network
3. Open `http://192.168.4.1` in a browser (the display shows the exact IP address once the portal is active)
4. Enter LoRaWAN keys and operating parameters (**portal timeout: 3 minutes**, then restart)

| Parameter | Description                                                                                                                                                                                                                                                                              | Default |
|-----------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------|
| DevEUI | 16 hex characters, from ChirpStack                                                                                                                                                                                                                                                       | – |
| JoinEUI | 16 hex characters, from ChirpStack                                                                                                                                                                                                                                                       | – |
| AppKey | 32 hex characters, from ChirpStack                                                                                                                                                                                                                                                       | – |
| LoRaWAN region | `EU868`, `US915`, `AU915`, `AS923`, `IN865`, `KR920`, `CN470`                                                                                                                                                                                                                            | EU868 |
| Deep Sleep time (s) | Deep sleep duration between measurement cycles. **0 = continuous mode** (no deep sleep)                                                                                                                                                                                                  | 300 |
| Max sampling time (ms) | Maximum measurement window per deep sleep cycle. **Only used when `Deep Sleep time > 0`; ignored in continuous mode** (see `Continuous uplink interval` instead)                                                                                                                         | 15000 |
| Continuous uplink interval (s) | Only used when `Deep Sleep time = 0`: the node samples continuously for this interval and then sends one uplink                                                                                                                                                                          | 300 |
| K-factor (pulses/L) | Calibration value for your flow sensor (open collector, e.g. ZJ-B5)                                                                                                                                                                                                                             | 450 |
| BMS (0=off, 1=DALY, 2=LiPo) | 0=no BMS, 1=DALY Smart BMS, 2=LiPo voltage via `heltec_vbat()`                                                                                                                                                                                                                           | 1 |
| Victron (1=Yes) | Victron VE.Direct enabled                                                                                                                                                                                                                                                                | 1 |
| HighSide/24V (1=Yes) | High-side switch e.g. for DC-DC step up converter for 24v level sensor                                                                                                                                                                                                                   | 1 |
| 4-20mA Sensor (1=Yes) | Level sensor measurement enabled                                                                                                                                                                                                                                                         | 1 |
| Flow sensor (open collector, 1=Yes) | Flow pulse counting enabled                                                                                                                                                                                                                                                       | 1 |
| Display-on cycles | **Only used in continuous mode (`Deep Sleep time = 0`)**: number of measurement cycles the display stays on after boot or after a short user-button press, before turning off automatically. **`0` = always on** (no auto-off). In deep-sleep mode the display behavior is automatic (see note above) and not configurable. | 5 |
| Altitude (m) | Site altitude in meters for QNH calculation (0 = absolute pressure)                                                                                                                                                                                                                      | 0 |
| DALY interval | Read BMS every N cycles                                                                                                                                                                                                                                                                  | 6 |
| Victron interval | Append Victron telemetry (Ch10/11/17/18/19) to the uplink only every N cycles (`1`=every cycle). Increase to reduce average payload size / airtime.                                                                                                                                       | 1 |
| Payload format (0=CayenneLPP, 1=binary) | `0`=CayenneLPP on FPort 1 (standard tooling). `1`=compact custom binary on FPort 3 (~50% smaller, same values). The matching decoder is downloadable from the portal.                                                                                          | 0 |
| Debug mode | Include additional diagnostics (Ch200/202/203/204 and RF quality Ch205/206/207/208 when available) in each uplink                                                                                                                                                                           | 0 |
| CPU (80/240 MHz) | CPU frequency (80 MHz saves power)                                                                                                                                                                                                                                                       | 80 |

> **Auto-detected sensors (no config needed):**  
> **DS18B20** — the 1-Wire bus is always scanned on startup; 0–3 sensors are detected and used automatically.  
> **BME280** — always probed at I²C address 0x76; used automatically if present, silently skipped if absent. Primary purpose here is enclosure monitoring: humidity helps detect moisture ingress into the control box, and temperature indicates the thermal health of the electronics. Recommended practice is to place silica gel inside the waterproof enclosure; if humidity trends upward over time, replace/regenerate the silica gel and inspect the enclosure sealing.

---

## User Button (GPIO 0)

> **Note:** GPIO 0 LOW during power-on triggers the ESP32 bootloader. The button therefore **cannot** be held while powering on to open the config portal. Instead, a hint screen is shown after boot and the button is polled actively.

| State | Action                                                                                                        |
|-------|---------------------------------------------------------------------------------------------------------------|
| **First boot** (no DevEUI stored) | Config portal opens automatically                                                                             |
| **After boot** (4 s hint screen) | Hold button for 3 s → config portal opens                                                                     |
| **Normal operation** (timer wakeup, deep-sleep mode) | –                                                                                                             |
| **Button press during deep sleep** | EXT0 wakeup → display enabled → full measurement cycle → deep sleep again                                     |
| **Short button press (continuous mode, `sleepSec=0`)** | Display turns on (or stays on) for `displayOnCycles` further measurement cycles, then turns off automatically |

> **How presses are captured:** in deep-sleep mode the button is a hardware **EXT0** wake source. While the node is awake (continuous mode, or the active phase of a deep-sleep cycle) presses are latched by a **GPIO interrupt** instead of polling, so a tap is never missed — even during the LoRaWAN transmission or an internal delay. The interrupt is detached again right before entering deep sleep, where EXT0 takes over.

> Before entering deep sleep the firmware waits until the button is released (+ 50 ms debounce) to prevent an immediate re-wakeup loop (EXT0 is level-triggered on LOW).

---

## Firmware Update via Wi-Fi Portal (OTA)

Firmware can be updated wirelessly in the field while the config portal is open — no USB connection required. The compiled `.bin` (see [`firmware/`](firmware/), e.g. `lorawan-water-node-v1.0.0.bin`) is uploaded straight from a browser.

1. Open the config portal (first boot, 3 s button hold after the hint screen, or LoRaWAN downlink `0x03`)
2. Connect to the board Wi-Fi **"WaterNode"**
3. Open **http://192.168.4.1** in a normal browser
4. On the portal start page, click **Firmwareupdate**
5. Select the new `.bin` file, upload it, and wait until the board reboots

> If the captive portal page does not offer a file-upload field, open **http://192.168.4.1/update** directly in the browser.

While the portal is active, an `ArduinoOTA` service also runs in parallel under the hostname **`water-node`**, so the firmware can alternatively be flashed over the network from PlatformIO / the Arduino IDE (espota) instead of over USB.

---


## TTN (The Things Stack) Instead of Private ChirpStack

The node can be used with TTN/TTS as well. No payload format change is required, because uplink payloads remain standard CayenneLPP and the same decoder logic can be reused.

### What to configure

1. In the WiFi portal, set **LoRaWAN region** to the matching regional plan of your TTN deployment (for Europe typically `EU868`).
2. In TTN, create an end device using **OTAA** (LoRaWAN 1.0.x profile) and copy **DevEUI / JoinEUI / AppKey** into the node portal.
3. Add the payload formatter in TTN by reusing the logic from `chirpstack/decoder.js` (field names can remain unchanged). The same decoder also handles the optional binary format (FPort 3) — if you enable `Payload format = 1`, no separate formatter is needed, and TTN just needs the decoder applied to all ports.

### Fair-use and practical intervals (EU868)

TTN strongly favors low airtime and low uplink rates. Practical guidance for this water telemetry node:

1. Prefer **60 s to 300 s** uplink intervals.
2. Avoid high-spreading-factor operation with very short intervals.
3. Keep payload compact by disabling unused sensors in the portal.
4. Reduce average payload by raising `DALY interval` and `Victron interval` so the bulky battery/PV/BMS telemetry is sent only every N cycles — this keeps most uplinks under the 52-byte DR3 threshold and lowers airtime, which is especially valuable under TTN fair-use.

For TTN community networks, very short uplink intervals are generally not appropriate and may violate fair-use expectations depending on data rate and gateway conditions.

For TTN operation, keep `debugMode` disabled except for short troubleshooting windows.

### Do you need further firmware changes for TTN?

In most cases: **no functional code changes** are required. The key points are correct region and OTAA credentials.

---

## Downlink Commands (Port 1)

| Byte | Function | Effect |
|------|----------|--------|
| `0x01` | Immediate measurement | Sleep is skipped; a new measurement cycle starts immediately |
| `0x02` | Open config portal (alias for `0x03`) | Kept for backwards compatibility |
| `0x03` | Open config portal immediately | After the current send cycle completes, the portal opens and the device restarts afterwards |
| `0x04` | Enable debug mode | Persists `debugMode = 1` in NVS |
| `0x05` | Disable debug mode | Persists `debugMode = 0` in NVS |
| `0x10 ...` | Parameter update | Binary format: `0x10` + N tuples `[paramId][valueHi][valueLo]` (signed int16, big-endian) |
| `0xFF` | Factory reset | Clears NVS configuration and restarts |

### Parameter Update via Downlink `0x10`

The payload starts with `0x10` and then one or more triplets:

`[paramId][valueHi][valueLo]`

- `paramId=1`: `sleepSec` (`0`=continuous, otherwise min `10`)
- `paramId=2`: `samplingMs` (min `1000`)
- `paramId=8`: `continuousSendSec` (min `10`, used only when `sleepSec=0`)
- `paramId=3`: `kFactor` (min `1`)
- `paramId=4`: `altitudeM` (`0..9999`)
- `paramId=5`: `bmsInterval` (min `1`)
- `paramId=10`: `victronInterval` (min `1`; `1`=send Victron telemetry every cycle, higher=less often)
- `paramId=11`: `payloadFormat` (`0`=CayenneLPP/FPort 1, `1`=binary/FPort 3)
- `paramId=6`: `debugMode` (`0` or `1`)
- `paramId=7`: `region` (`0=EU868`, `1=US915`, `2=AU915`, `3=AS923`, `4=IN865`, `5=KR920`, `6=CN470`)
- `paramId=9`: `displayOnCycles` (min `0`, max `1000`; `0`=always on; continuous mode only, see [Display power management](#initial-configuration-wifi-portal))

If `paramId=7` changes region, the node restarts automatically after saving.

---

## 4–20 mA Current Measurement

The level sensor (24 V two-wire loop) is powered via the high-side switch at 24 V.  
Shunt resistor R4 = 120 Ω converts the loop current to a voltage:

```
4 mA  → 0.48 V  (sensor: lower range / dry)
20 mA → 2.40 V  (sensor: upper range / full)
```

The ADC value is converted directly to mA in firmware and transmitted on CayenneLPP channel 1.

---

## Power Saving Measures

- **Deep sleep** between measurement cycles (default: 5 minutes)
- **CPU frequency** reduced to 80 MHz
- **Display** automatically off during unattended timer-wakeup cycles (deep-sleep mode); on for button wakeup/fresh boot, or for `displayOnCycles` cycles after a button press in continuous mode
- **VCC3.3 switched (GPIO38)** cuts power to DS18B20, HCPL-260L, flow sensor (open collector, e.g. ZJ-B5) (5 V via LP0701N3 cascade), and ADuM121N1
- **High-side switch (GPIO47)** cuts power to 24 V DC-DC converter + 4–20 mA sensor
- **GPIO hold** keeps both outputs safely LOW during deep sleep
- **DALY BMS** is read only every N cycles (infrequent wakeup pulse on S1)

---

## Power Consumption

> All figures are approximate (typical values from datasheets + practical experience with Heltec V3).  
> Voltage rail: **5 V rail** (supplied by DC-DC 12 V→5 V, η ≈ 85 %).  
> The 12 V→24 V DC path for the 4–20 mA sensor is shown **separately**.

### Deep Sleep (~275 s per 5-minute cycle)

| Component | Current (5 V rail) | Notes |
|---|---|---|
| ESP32-S3 deep sleep | ~8 µA | Datasheet |
| SX1262 sleep mode | ~1 µA | `heltec_deep_sleep()` |
| Heltec V3 onboard (LDO, CP2102N, dividers) | ~500 µA | Main consumer during sleep |
| BME280 standby | < 1 µA | |
| GPIO38 rail (DS18B20, flow sensor (open collector, e.g. ZJ-B5), HCPL-260L) | **0 µA** | switched off |
| GPIO47 rail (24 V DC-DC, sensor) | **0 µA** | switched off |
| **Total 5 V rail** | **≈ 0.5–1 mA** | measured in practice: 0.8–2 mA¹ |

> ¹ The Heltec V3 sleep current varies by batch and library version;
> the `heltec_unofficial` library minimizes this value.

### Active Measurement Phase (~25 s per cycle)

| Component | Current (5 V rail) | Notes |
|---|---|---|
| ESP32-S3 @ 80 MHz | ~45 mA | CPU + SRAM + peripherals |
| SX1262 receive/idle | ~5 mA | during measurement window |
| SX1262 transmit (+20 dBm) | ~120 mA (peak) | ~1–2 s; SF9/DR3 EU868 |
| flow sensor (open collector, e.g. ZJ-B5) | ~10 mA | Hall effect, 5 V |
| HCPL-260L (ESP32 side, 3.3 V) | ~2 mA | |
| DS18B20 × 3 | ~5 mA | active conversion |
| BME280 | ~1 mA | |
| ADuM121N1 VCC2 side × 2 | ~2 mA | Victron + DALY, ESP32 side |
| **Subtotal without LoRa TX** | **≈ 70 mA** | |
| **Peak during LoRa TX** | **≈ 180–200 mA** | ~1–2 s; capacitors buffer the peak |

**Separate 12 V path** (high-side switch, active only during measurement):

| Component | Current (12 V rail) | Notes |
|---|---|---|
| 12 V→24 V DC-DC converter | ~5 mA no-load | converter self-consumption |
| 4–20 mA sensor @ 24 V | 4–20 mA | depending on water level |
| **Total 12 V path** | **≈ 9–25 mA** | only during measurement (15 s) |

### Average over One 5-Minute Cycle (300 s)

| Phase | Duration | Current 5 V rail | Share |
|---|---|---|---|
| Active measurement + LoRaWAN | 25 s | ~80 mA (avg including TX phases) | ~8 % |
| Deep sleep | 275 s | ~1 mA | ~92 % |
| **Average current (5 V rail)** | — | **(80×25 + 1×275) / 300 ≈ 7.6 mA** | |

**At the LiFePO4 battery (12 V, after DC-DC η = 85 %):**

```
P_5V = 5 V × 7.6 mA = 38 mW
P_12V_in = 38 mW / 0.85 ≈ 45 mW → 45 mW / 12.8 V ≈ 3.5 mA (ESP32 path)
P_12V_sensor ≈ 1.5 mA (25 s measurement at avg 15 mA, scaled to 300 s)
────────────────────────────────────────────────────────────
Total avg from 12 V battery:  ≈ 5 mA
Daily consumption:             5 mA × 24 h ≈ 120 mAh/day
```

**Example battery runtime without solar** (4s LiFePO4, 10 Ah):
> 10,000 mAh / 120 mAh/day ≈ **~83 days** — shorter in practice due to aging, temperature, and BMS self-consumption.  
> With the Victron solar charge controller and a small panel (~25 W), **continuous operation** is easily achievable.

---

## Libraries

| Library | Version | Purpose |
|---------|---------|---------|
| RadioLib | ^7.6.0 | LoRaWAN stack (OTAA, region configurable) |
| Heltec_Esp32_LoRa_v3 | ^0.9.2 | Board support (display, heltec_vbat) |
| WiFiManager | ^2.0.16-rc.2 | WiFi configuration portal |
| CayenneLPP | ^1.6.1 | Payload encoding |
| Adafruit BME280 | ^2.2.4 | BME280 driver |
| OneWire | ^2.3.8 | 1-Wire protocol |
| DallasTemperature | ^4.0.6 | DS18B20 driver |
| daly-bms-uart | GitHub | DALY Smart BMS parser |
| VeDirectFrameHandler | GitHub | Victron VE.Direct parser |

---

## ChirpStack Decoder

The JavaScript decoder is located in `chirpstack/decoder.js` and is compatible with **ChirpStack v3 and v4**.

### Installation in ChirpStack
1. Open the **Device Profile** in ChirpStack
2. Go to the **Codec** tab → Codec: **Custom JavaScript codec functions**
3. Paste the contents of `chirpstack/decoder.js` into the **Decoder** field
4. Save

### Decoded Fields (Port 1 – Normal Measurement Cycle)

| Field | Unit | Description |
|-------|------|-------------|
| `level_sensor_ma` | mA | 4–20 mA level sensor current |
| `flow_l_min` | L/min | flow rate (from configured open-collector flow sensor, e.g. ZJ-B5) |
| `temp_source_c` | °C | DS18B20 #1 spring water temperature |
| `temp_tank_floor_c` | °C | DS18B20 #2 tank floor temperature |
| `temp_air_c` | °C | DS18B20 #3 air temperature above tank |
| `bme280_temp_c` | °C | BME280 enclosure/electronics temperature (thermal health indicator) |
| `bme280_humidity_pct` | % | BME280 relative humidity inside enclosure (moisture ingress indicator) |
| `bme280_pressure_hpa` | hPa | BME280 pressure (QNH), additional byproduct value |
| `battery_status` | V or 0/1 | LiPo voltage (bmsMode=2) or DALY status flag (bmsMode=1) |
| `victron_battery_v` | V | Victron battery voltage |
| `victron_pv_w` | W | Victron PV power |
| `victron_cs` | 0–7 | Victron charge state (numeric) |
| `victron_cs_text` | – | Victron charge state (text: Bulk/Absorption/Float/…) |
| `victron_charge_a` | A | Victron charge current |
| `victron_yield_today_kwh` | kWh | Victron yield today |
| `daly_pack_v` | V | DALY pack voltage |
| `daly_soc_pct` | % | DALY state of charge |
| `daly_current_a` | A | DALY current (+A=charging, −A=discharging) |
| `daly_bat_temp_c` | °C | DALY battery temperature |
| `daly_cell_diff_mv` | mV | DALY cell voltage spread (battery health) |
| `diag.victron_no_frame` | bool | Victron: no frame received |
| `diag.daly_no_response` | bool | DALY: no response |
| `diag.ds18b20_read_error` | bool | DS18B20: read error |
| `diag.level_out_of_range` | bool | 4–20 mA outside 3.5–20.5 mA range |
| `diag.last_reset_was_wdt` | bool | Last reset was a watchdog reset |
| `debug_cycle_counter` | 0–255 | Cycle counter (only when `debugMode = 1`) |
| `debug_send_fail_counter` | 0–255 | OTAA/send fail counter (only when `debugMode = 1`) |
| `debug_free_heap_kib` | KiB | Free heap (only when `debugMode = 1`) |
| `debug_uptime_h` | h | Uptime in hours (only when `debugMode = 1`) |
| `debug_dl_rssi_dbm` | dBm | Last downlink RSSI at node receiver (only when available) |
| `debug_dl_snr_db` | dB | Last downlink SNR at node receiver (only when available) |
| `debug_link_margin_db` | dB | LoRaWAN LinkCheck margin reported by network (only when available) |
| `debug_link_gw_count` | count | Number of gateways reported by LinkCheck (only when available) |

### Decoded Fields (Port 2 – Daily Uplink, every 24 h)

| Field | Unit | Description |
|-------|------|-------------|
| `total_liters_today` | L | Total volume today |
| `max_flow_l_min` | L/min | Maximum flow rate today |
| `min_temp_source_c` | °C | Minimum spring water temperature today |
| `max_temp_source_c` | °C | Maximum spring water temperature today |

---

## Project Structure

```
├── src/
│   └── main.cpp          # Complete firmware (setup, measurement, LoRaWAN)
├── chirpstack/
│   └── decoder.js        # ChirpStack payload decoder (v3 + v4 compatible)
├── pcb/
│   ├── *.fzz             # Fritzing project file
│   ├── *_Schaltplan.png  # Schematic
│   ├── *_Leiterplatte.png# PCB view
│   └── *_bom.html        # Bill of materials
├── platformio.ini        # Build configuration & variants
└── README.md
```
