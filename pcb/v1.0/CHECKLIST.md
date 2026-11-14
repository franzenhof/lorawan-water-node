# PCB v1.0 – Design Review, Assembly and Bring-up Checklist

Nothing on this board has been tested on real hardware yet. Use this list for the JLCPCB
order and for the first prototypes (order 5, test, then more).

**State after the design review of 2026-11-08** (part by part and block by block):
final DRC **0 errors, 0 unconnected items, 0 schematic-parity issues** (silkscreen warnings
only); ERC 0; BOM 61 SMD lines without duplicate LCSC numbers, CPL 166 top-side placements,
13 THT lines in `*-tht-lcsc.csv`.

## 1. Changes from the review

| Item | Change | Reason |
|---|---|---|
| D26/D27/D29 | BAT54S clamp to +5V → **BZX84C5V6** zener to GND | a 12/24 V input injected 0.4–0.6 mA per input into +5V; the PFM buck cannot sink it, so in deep sleep the 5 V rail rose to 6–6.5 V (above XC6206 6 V and 74AHCT 5.5 V) |
| D12 | SMBJ33A → **SMBJ33CA** (bidirectional) | reversed battery drove the TVS forward: 100–250 A against F1's 40 A rating. D11 and Q12 already block reverse polarity |
| D11, D13 | SS34 → **SS210** (100 V) | D11 sees the full reversed VIN; D13 had 3 % margin at boost overvoltage |
| U1 | 78L05 → **78L33** (same SOT-89 pinout) | JP3 at 2-3 ran the DALY side of the isolator at 5 V against a 3.3 V BMS UART |
| Q12 load supply | **eFuse TPS16630** (U16) behind Q12, ILIM 3.3k = 5.4 A (2026-11-09, was 4.7k = 3.8 A: start currents of pumps), MODE = auto-retry; SHDN = BAV70 diode OR of the output GPIOs + 47k | +VIN_LOAD had no fuse; a hard short at an output exceeded PTC/FET ratings. A protected 60 V low-side switch for 3 A does not exist at JLC (BTF3050TE: short-circuit protection only up to 28 V) |
| F7/F8/F10 | 3 A / 36 V PTCs removed | covered by U16 (one current limit for all three outputs and the +VIN terminal) |
| Outputs | **OUT1 = GPIO19 (Q11), OUT3 = GPIO1 (Q6)** | the FETs sit in reverse order under J17; the swap gives straight 1.5 mm paths. Terminal labels unchanged, firmware pin constants swapped |
| R63 | 100k → **1 MΩ** | Q12 gate clamp current in deep sleep 145–210 µA → 12–21 µA |
| R24 | 390k → **360k** | boost 24.0 → 22.2 V (TVS D8 SMBJ24A standoff 24 V) |
| D24/D25 (D35) | BAT54S → **BAS70-06** (70 V Schottky, common anode; 2 parts for 3 inputs, D35 removed) | BAT54S (30 V) saw 29.7 V at 33 V input; BAV99 rejected (0.7 V drop eats the input high margin) |
| D7 | SMBJ5.0A → **SMBJ6.0A** | 5.0A leaks at 5 V (same as D6) |
| D22/D23/D28/D31/D32 | SMAJ33A → **SMAJ33CA** (bidirectional, same SMA footprint) | a reversed 24 V source at IN1–3 / AI1–2 drove the TVS forward without any fuse; now −33…+33 V, current limited by 47k / 100k |
| R8 | 10k → **1k** | clamp leakage / ADS1115 input current error < 0.05 % |
| J4 OC1/OC2 | **0–33 V**: D14 PESD5V0S2BT → PESD36VS2UT, R6/R30 220 Ω → 10k, pull-ups R14/R31 to the terminal side via D40 BAS70-06, D3/D19 BAT54S → BZX84C5V6 to GND, C7/C24 100 → 47 nF; silkscreen "OC 0-33V" | 12/24 V pulse sources and miswiring; filter τ kept (1.2 / 0.7 ms) |
| JP13–JP16 | **pull-up / pull-down jumpers**: JP13–JP15 (IN1–3, 1 × 3) and JP16 (OC1 \| OC2, 2 × 3), top pair = pull-up via diode, bottom pair = pull-down to GND, open = none; R14/R31 0805 → 1206 (pull-down at 33 V: 109 mW) | PNP sensors / high-side switches without an external resistor at the terminal |
| R64 | 4.7k → **3.3k** (C26010, basic) = 5.4 A eFuse limit | pump / motor start currents of 2–5 × the running current; at 3.8 A the eFuse held the current, overheated and retried every 0.65 s, so such a load never started. Continuous rating stays 3 A in total |
| J16 | new pin order **VIN, GND, 5V, GND, 3V3S, SDA, SCL, GPIO46** (was GND, 3V3S, 5V, VIN, SDA, …) | VIN sat between 5V and SDA: a slipped probe put up to 33 V onto I²C. Now VIN is at the outer end next to GND only, 5V between two GND pins |
| 2 oz option | 39 track pieces 0.15 → 0.16 mm, J12/J13 (JST PH) pads 1.2 → 1.26 mm (annular ring 0.255 mm) | the board now meets JLCPCB's 2 oz outer-copper rules (track / space ≥ 0.16 mm, PTH ring ≥ 0.254 mm, mask dam ≥ 0.20 mm: tightest is U10 at 0.20 mm); works unchanged with 1 oz |
| Docs | D20, τ 1.5 ms, VDD1, 22 V loop, JP10 (no deep-sleep counting), 78L33 | README, silkscreen, design comments |

Firmware: pins released (I²C, 1-Wire, UART TX → input) before GPIO38 switches the 3.3 V rail off
(no back-feeding), HW_ID logged at boot, BME280 probed at 0x76 and 0x77, 22 V boost switched off
above 23 mA loop current, OUT1/OUT3 pin swap.

Checked and OK: pinouts (incl. XC6206, SOT-89, TPS16630 HTSSOP-20), isolation (no net crosses
GND / GND_VICTRON / GND_DALY), strapping pins, buck (5.02 V, min on-time, ripple), Q12 VGS clamp,
output FETs at 3 A (0.18 W), freewheel diodes, ADS1115 dividers vs firmware constants, I²C with
JP1/JP2 (≈ 4–5 m cable), switched-rail leakage.

## 2. JLCPCB order preview (before paying)

- `NEEDS_PREVIEW` in `production/*-cpl-review.csv`: **U16** (TPS16630, HTSSOP-20), **D14** (PESD36VS2UT),
  **D24/D25/D40** (BAS70-06: common anode pin 3 = DIN_VCC / PULSE_VCC; D24/D25 rotated 180°). U1 (78L33, now 0° offset),
  BZX84C5V6 and BAV70 were confirmed in the 2026-11-08 preview.
- Diode polarity (band at pad 1): D11, D13, D18, D21, D36 (SS210), D6, D7, D8, D9, D10;
  D12 (SMBJ33CA) and D22/D23/D28/D31/D32 (SMAJ33CA) are bidirectional.
- U16 (TPS16630) rotation corrected after the 2026-11-08 preview (now 0°): leads left/right, pin 1 top left.
- BOM: no "multiple lines matched to the same part" message.

## 3. Bring-up

1. VIN 12 V, all outputs off: current from VIN ≈ buck 26 µA + R63 12 µA + U16 21 µA + module.
   TP7 `VLD` = 0 V (U16 off).
2. Reverse VIN (current-limited supply): nothing gets warm, F1 does not trip, TP7 stays 0 V.
3. Switch OUT1/OUT2/OUT3 one by one: TP7 ≈ VIN, gate ≈ 5 V. OUT1 must follow GPIO19, OUT3 GPIO1.
4. Deep sleep with OUT1 and OUT3 on: both stay on (pad hold on the USB D- pad and the VBAT pin).
5. Load 3 A for 10 min: Q6/Q7/Q11, Q12, U16 at most warm; +VIN_LOAD right-hand track (1.3 mm) lukewarm.
6. eFuse: ~6.5 A → current limit 5.4 A, off after ≤ 0.16 s, retry every 0.65 s; short at an output
   (thick wire) → fast trip, no damage.
7. Digital input at 24 V during deep sleep: +5V stays at 5.0 V (zener clamps, no rail lift).
   24 V on OC1/OC2: input reads high, nothing gets warm, PULSE_VCC stays at 3.3 V; a 5 V Hall flow sensor
   still counts up to a few hundred Hz with JP11/JP12 fitted.
   Pull jumpers: JP13 at 1-2, IN1 open → reads high; at 2-3, IN1 open → reads low, 24 V → high (PNP sensor).
   JP16 left column the same for OC1 (top pair = pull-up, bottom pair = pull-down).
8. 4–20 mA: boost 22.2 V at TP6; shorted loop → firmware switches GPIO47 off ("loop current … > 23 mA").
   With a loop calibrator at 4 / 12 / 20 mA, cold and warm (≈ 50 °C at D9): reading within ±0.1 % – D9 (SMBJ5.0A)
   sits across the shunt and its leakage is only specified at 5 V. Repeat with an output at 3 A (ground-plane offset, README).
   USB-C with VIN applied and JP8 fitted: check whether the Heltec module back-feeds the buck's 5 V into VBUS
   (no diode on the module's 5V pin?) – until confirmed, pull JP8 for flashing with VIN connected.
9. JP3 at 2-3 with JP4 (4s BMS only): VDD_DALY = 3.3 V.
10. Boot log shows `[HW] revision ID ~1650 mV -> v1.0`.

## 4. Open points for the next revision (need a layout change)

- PTC or series resistor in J7 SIG; ESD at J11, VDD_VICTRON and the DALY VCC/BAT/S1 pins.
- ADuM VDD1 from the always-on 3.3 V (as on the weather node) instead of +3V3_SWB.
- L1 with Isat ≥ 2.4 A (LMR36510 current limit); C19/C20 22 µF in 1206 (DC bias).
- D33/D34 (AI clamps to +3V3_SWB) back-feed the switched rail with AI > 3.6 V while it is off.
- Per-channel current limit (today one 5.4 A eFuse for all outputs: one shorted output switches
  all three off until the short is removed). U16 FLT/IMON are not connected (no free GPIO / ADC).
- +VIN_LOAD right-hand track is 1.3 mm over 17 mm (next to the DIN_VCC link) – widen with more room.
