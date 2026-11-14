# pcbgen – KiCad generator for hardware v1.0

Scripts that produced `pcb/v1.0/` (schematic, placement, routing, silkscreen, production files).
The KiCad files in `pcb/v1.0/` are the reference; use this generator to rebuild them after a
circuit or layout change (or edit the board directly in KiCad for small changes).

| File | Purpose |
|------|---------|
| `design.py` | parts, values, footprints and nets (the netlist) |
| `lcsc.py` | LCSC / JLCPCB part numbers |
| `schem_*.py` | schematic placement per function block (`gen_sch2.py` draws the wiring) |
| `layout_*.py` | board outline, fixed parts, placement zones, labels, protection positions |
| `gen_sch2.py` | drawn schematic (wires, power symbols, labels) |
| `gen_pcb2.py` | board: footprints, connectivity-aware placement, rotation pass, keep-outs |
| `finish.py`, `bridge.py` | session import, ground pours, stitching/island vias, ground bridges |
| `brand.py`, `fixrefs.py` | silkscreen: terminal titles, notes, reference texts |
| `checknet.py`, `domaincheck.py`, `drc.py` | checks: netlist vs. design, isolation, DRC summary |
| `build2.sh` | full flow: schematic → ERC → netlist → placement → parallel Freerouting attempts → best board |
| `export.sh` | Gerber/drill ZIP, JLCPCB SMD BOM/CPL, THT LCSC list, schematic PDF, renders, final DRC |
| `jlcpcb_cpl.py` | rotates the KiCad positions into JLCPCB's orientation (package rules) and writes the review list |

Requirements: KiCad 10 (`kicad-cli` and its bundled Python with `pcbnew`), Java and
[Freerouting](https://github.com/freerouting/freerouting) 2.5 (`FREEROUTING_JAR=/path/freerouting-2.5.0.jar`).

```bash
mkdir -p /tmp/work && cp pcb/v1.0/*.kicad_pro /tmp/work/
FREEROUTING_JAR=~/freerouting-2.5.0.jar ATTEMPTS=12 PARALLEL=4 bash tools/pcbgen/build2.sh /tmp/work
bash tools/pcbgen/export.sh tools/pcbgen pcb/v1.0 lorawan-water-node   # expects the result in tools/pcbgen/proj/ – copy /tmp/work there first
```

Routing: every attempt gets a slightly varied placement (`JITTER=1`), ground is routed as a normal
net before the pours are added (`NOPLANE=1`, avoids isolated ground islands); the attempt with the
fewest DRC errors is kept. Hand touch-ups of the released v1.0 board after the build (repeat them after a rebuild, then
check DRC and the top render):

- best of 16 attempts (`ATTEMPTS=16`), then one incremental Freerouting pass on it (export DSN of the
  routed board, route, `finish.py <board> board.ses`) for the last unrouted nets;
- OUT1_D: the last 1.3 mm before its via necked down to 0.8 mm (clearance to D20 / F10); DIN_VCC: U9 pad 5
  joined under the body (the `UNDER_U9` keep-out removed);
- all copper pours with 0.2 mm clearance / 0.2 mm minimum width (instead of 0.3 / 0.25) – otherwise GND
  pockets stay unconnected; JP11/JP12 GND pads with solid zone connection;
- silkscreen: brand block re-placed with the fixed `brand.py`; notes that found no top-side spot were
  dropped (their content is in the bottom quick reference, left-aligned at x = 50.8 mm); J16 reference left
  of its pins;
- top title of J3 shortened to `6-33V` (collided with J7);
- L1 changed to the 6 × 6 mm SWPA6045S (Isat 2.05 A ≥ LMR36510 current limit) in place: L1 0.4 mm and C18
  0.5 mm to the right with their track ends, L1 courtyard tightened to ±3.15 mm (body 6.0 mm) so it fits
  between JP8 and U6; C17 220 nF 100 V.
- after the design review (same footprints, values only): Q9 2N7002 (60 V), freewheel diodes D18/D21/D36 SS36
  (60 V), D6 SMBJ6.0A; JP5 pin label `LED`; extra silkscreen notes on both sides and moved references
  (scripts in the session scratchpad, not part of the generator);
- high-current paths: two extra vias next to every via of VIN_IN, +VIN_LOAD, OUT1-3 and OUT1-3_D, narrow
  segments widened to 1.25-1.5 mm where clearance allows, copper pours on both D20 pads (F.Cu, B.Cu with
  stitching vias); the total output current is rated 3 A continuous (D20 dissipation);
- Q5 soft start: C36 (1 uF, gate-drain) placed at (60.0, 90.0) and routed by a small grid router (HS_GATE
  over two vias, BOOST_IN to the B.Cu track), R17 10k; schematic regenerated, footprints re-linked to its UUIDs.
- 5 V gate drivers U12-U14 (74AHCT1G125, SC-70-5) with input pull-downs R60-R62 (0402) under the Heltec
  module: each GPIO trace was cut on a straight segment (OUT1/OUT2 on B.Cu, OUT3 at J1 pin 18), the load side
  renamed to OUTx_DRV, the gaps and the supply pins routed by the grid router.

- November 2026 rework (scripts in the session scratchpad, footprints re-linked to the regenerated schematic):
  D20 (SS54C) replaced by the P-MOSFET Q12 (NCE60P50K, TO-252-2) in its place, legs up; D37 (BZT52C12) and R63 (100k)
  under the Heltec module, gate over B.Cu under the Q12 tab; OUT3, DIN1_OUT (Freerouting) and the OUT1_D via (two vias
  in the F7 pad 2) re-routed around it. Own 3.3 V LDO U15 (XC6206P332MR) with C37/C38 under the module below J2; +5V and
  3V3_LDO pass the J2 2-3 / 3-4 gaps with 0.4 mm tracks to F2, J2.2 (Heltec 3V3) is no longer used. TP7/TP8 SMD probe
  pads, bottom S/N and DATE field. `export.sh` now writes the SMD-only BOM, a THT LCSC list and the CPL rotated by
  `jlcpcb_cpl.py` (plus `*-cpl-review.csv`).
- JP1 (2 × 2 I²C pull-up header) split into JP1 (SCL) and JP2 (SDA), two 1 × 2 headers 5.08 mm apart so a shunt
  cannot bridge SCL and SDA; R10/R11 vertical at x = 99.6 mm, +3V3_SWB fed along x = 100.75 mm, B.Cu +3V3 straightened.
- Design review 2026-11-08 (see `pcb/v1.0/CHECKLIST.md`): F7/F8/F10 removed, eFuse U16 (TPS16630, own symbol in
  `symbols/water_node.kicad_sym`, loaded by `sexp.py`; KiCad project gets it via `pcb/v1.0/sym-lib-table`) on the old F7
  place with C39/R64, SHDN diode OR D38/D39 + R65 under the Heltec module. Output channels swapped to the board order
  (OUT1 = Q11/GPIO19, OUT3 = Q6/GPIO1, nets renamed). Output block re-routed: power paths by hand (+VIN_LOAD F.Cu along
  the top and the right side, OUT1/OUT2/OUT3 on B.Cu at x = 108.7 / 111.3 / 116.85 mm, 1.5 mm, second vias at the FETs),
  the crossing signals (DIN1-3_OUT, SDA, EF_SHDN, OUTx_G) by incremental Freerouting passes with everything else locked.
  Value / part swaps in place (BZX84C5V6, BAS70-04, SMBJ33CA, SS210, 78L33, 1M, 360k, 1k, SMBJ6.0A); the +5V feed of
  the old input clamps removed. `gen_sch2.py` skips hidden (stacked) symbol pins.
- J4 pulse inputs for 0-33 V: D40 (BAS70-06) added at (78.4, 118.2), R14/R31 moved to the terminal side (nets only),
  pulse-input nets and PULSE_VCC re-routed by Freerouting with everything else locked, GND island fixed by `finish.py fix`.
- Pull-up / pull-down jumpers: JP13-JP15 (1x3, IN1-3) at the right edge below the AI block with the input pull-up
  diodes D24/D25 (2 x BAS70-06, D35 removed) next to them, JP16 (2x3, OC1 | OC2) left of the board title; R14/R31 in 1206.
  The +5V diagonal next to JP16 moved 0.4 mm. New nets routed by Freerouting with everything else locked (only the local
  +3V3_SWB piece at TP8 ripped); DIN_VCC at U9 and LEVEL_ADC at U10 finished by hand, GND islands by `finish.py fix`.

## 3D models

`models3d.py` (called by `export.sh`) writes simple VRML models into `pcb/v1.0/3dmodels/` for footprints whose KiCad library model doesn't exist (1812 / 2920 PTC fuses) and points them there via `${KIPRJMOD}`. The Heltec module itself is not modelled — the renders show its sockets and the silkscreen underneath.
