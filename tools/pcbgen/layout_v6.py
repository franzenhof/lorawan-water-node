"""Layout for hardware v1.0 (I/O block at the top edge: 3 A outputs, 3 digital + 2 analog inputs): 100 x 75 mm, 4 x M3 in the corners (4 mm inset -> 92 x 67 mm pattern).

Board-local mm, origin top-left, y down. Heltec module at the left edge
(USB-C flush with the edge) just below the top-left mounting hole; 20
terminal poles along the bottom edge between the two lower holes:
12V | Level | Flow | 1-Wire || VE.Direct | DALY.
Robust rules: 0.3 mm signal / 0.8 mm power tracks, 0.8/0.4 mm vias,
0.5 mm edge clearance, 1.5 mm isolation moat.
"""
W, H = 100.0, 100.0

X0 = 4.2                      # Heltec pin 1 (USB end); module edge 1.5 mm inside -> USB-C does not protrude
ROW_TOP = 9.5                 # Heltec J3 (our J2, 3V3 side)
ROW_BOT = ROW_TOP + 22.86     # Heltec J2 (our J1, 5V side)
MOD = (X0 - 2.7, ROW_TOP - 1.32, X0 - 2.7 + 50.2, ROW_BOT + 1.32)
TPIN = H - 5.0
HOLES = [(4.0, 4.0), (W - 4.0, 4.0), (4.0, H - 16.0), (W - 4.0, H - 16.0)]   # 92 x 80 mm, lower pair above the terminal row
PITCH, TB_GAP = 3.5, 4.6         # terminal pitch, min. pin distance between blocks (courtyards)
J3_X = 10.0
J7_X = J3_X + PITCH + 5.5                     # J3: +12V | GND
J4_X = J7_X + PITCH + 5.0                     # J4 is 4-pole: OC1 | OC2 | +5V | GND
J8_X = J4_X + 3 * PITCH + 5.0
MOAT_V = J8_X + 2 * PITCH + 2.29 + 0.5        # left edge of the Victron moat (right of J8)
VIC_L = MOAT_V + 1.5
J9_X = VIC_L + 2.8
VIC_R = J9_X + 3 * PITCH + 2.4
DALY_X = VIC_R + 1.5
J10_X = DALY_X + 2.2
TERMS = [("J3", J3_X), ("J7", J7_X), ("J4", J4_X), ("J8", J8_X), ("J9", J9_X), ("J10", J10_X)]

# isolation geometry (moat 1.5-1.6 mm)
VIC_TOP = H - 24.5            # Victron domain top edge (below U3)
DALY_TOP = H - 40.0
U_X = (VIC_R + DALY_X) / 2    # U4/U5 barrier x (centre of the DALY moat)

FIXED = {
    "J2": (X0, ROW_TOP, 90), "J1": (X0, ROW_BOT, 90),
    "U3": (J9_X + 1.5 * PITCH, VIC_TOP - 0.75, 270),  # side 1 up, side 2 down to J9
    "U5": (U_X, DALY_TOP + 4.0, 0),      # LED left, transistor right
    "U4": (U_X, DALY_TOP + 10.5, 0),     # side 1 left, side 2 right
}
for ref, x in TERMS: FIXED[ref] = (x, TPIN, 0)
# I/O block at the top edge, terminals rotated 180 deg (wire entry towards the top edge, pin 1 on the right)
TPIN_TOP = 5.0
J17_P1, J18_P1 = 68.0, 90.1                   # pin-1 x of outputs (5-pole) and inputs (6-pole)
FIXED["J17"] = (J17_P1, TPIN_TOP, 180)        # left->right: +VIN | OUT1 | OUT2 | OUT3 | GND
FIXED["J18"] = (J18_P1, TPIN_TOP, 180)        # left->right: IN1 | IN2 | IN3 | AI1 | AI2 | GND

C_TOP, C_BOT = MOD[3] + 0.5, TPIN - 3.9
ZONES = [   # order = placement priority; ANCHORS below pull critical pads together
    ("OUTS", MOD[2] + 1.3, 70.0, TPIN_TOP + 8.6, 54.0, ["D20", "U16", "C39", "R64", "R65", "D18", "D21", "D36", "Q6", "Q7", "Q11", "R26", "R27", "R34", "R35", "R58", "R59"]),
    ("INS", 70.0, W - 0.6, TPIN_TOP + 8.6, 36.0, ["U9", "U11", "C28", "R36", "R38", "D26", "C26", "R37", "R39", "D27", "C27",
                                                   "R44", "R45", "D29", "C30", "JP9"]),
    ("ANA", 70.0, W - 0.6, 36.0, DALY_TOP - 2.5, ["Q10", "C35", "U10", "C31", "R50", "R51", "C33", "D33", "R52", "R53", "C34", "D34",
                                                   "R49", "R48", "C32", "Q8", "R46", "D30", "R47", "Q9",
                                                   "D24", "D25", "JP13", "JP14", "JP15"]),   # IN1-3 pull jumpers + pull-up diodes
    ("C", 0.6, MOAT_V - 0.6, C_TOP, C_BOT, [
        "D12", "D11",                                              # VIN input (F1 pinned at J3)
        "F3", "C6", "C3", "R4", "R8", "D4", "C12",                 # 4-20 mA (TVS pinned at J7)
        "R6", "C13", "R1", "F5", "R30",                            # flow (TVS/ESD pinned at J4)
        "R3", "D5", "R9", "C5", "R2", "F4",                        # 1-Wire (TVS/ESD pinned at J8)
        "U2", "C25", "C7", "C24", "R15", "R32",                    # Schmitt trigger + RC at the Heltec pins
        "R14", "D3", "R31", "D19",                                 # pulse-input pull-ups / clamps
        "C14", "D6", "C1",                                         # 5 V HF decoupling/TVS/bulk at the Heltec 5V pin
        "U6", "C17", "C16", "C18", "C29", "L1", "R40", "R41", "C19", "C20", "JP8",   # buck, compact hot loop, 5 V jumper
        "U7", "L2", "D13", "C22", "C23", "R24", "R25",             # boost, compact SW loop
        "Q5", "R17", "Q4", "R13", "R12",                           # boost supply switch + GPIO47 driver
        "R28", "R29", "R56", "R57",                                # HW revision ID divider, strapping-pin pull-downs
        "JP11", "JP12", "JP10", "JP16",                            # pulse filter / supply / pull jumpers
        "TP2", "TP3", "TP6", "TP1"]),                              # test points next to their rails
    ("TOPSTRIP", 8.0, MOD[2], 0.6, MOD[1] - 0.2, ["F2", "C15", "J16", "J11", "JP1", "JP2", "R10", "R11", "TP4"]),    # 3V3 from Heltec pin 2, I2C headers
    ("MID", MOAT_V, U_X - 6.5, C_TOP, VIC_TOP - 1.6, ["C8", "C10", "R5", "JP5", "TP5", "Q1", "R18", "C2", "Q2", "Q3", "R19", "R7", "R16", "R54", "R55"]),
    ("VIC", VIC_L + 0.6, VIC_R - 0.6, VIC_TOP + 0.6, C_BOT, ["J12", "D16", "C9"]),
    ("DALY", DALY_X + 0.8, W - 0.6, DALY_TOP + 0.6, C_BOT - 4.0, ["D17", "C11", "J13", "JP6", "JP7", "U1", "C4", "JP3", "JP4"]),
]
# (ref, own pad, anchor ref, anchor pad): the placer keeps these pads close (weight 25)
ANCHORS = [
    ("F1", "1", "J3", "1"), ("D12", "1", "F1", "2"), ("D11", "2", "D12", "1"),
    ("U6", "2", "D11", "1"), ("C17", "1", "U6", "2"), ("C16", "1", "U6", "2"), ("C18", "1", "U6", "7"), ("C29", "1", "U6", "6"),
    ("L1", "1", "U6", "8"), ("C19", "1", "L1", "2"), ("C20", "1", "L1", "2"), ("JP8", "1", "L1", "2"), ("R41", "1", "U6", "5"), ("R40", "2", "U6", "5"),
    ("C14", "1", "J1", "2"), ("D6", "1", "J1", "2"), ("C1", "1", "J1", "2"),
    ("Q4", "1", "J1", "13"), ("R13", "1", "Q4", "1"), ("R12", "2", "Q4", "3"), ("R17", "2", "Q5", "1"),
    ("U7", "5", "Q5", "3"), ("C22", "1", "U7", "5"), ("L2", "2", "U7", "1"), ("D13", "2", "U7", "1"), ("C23", "1", "D13", "1"),
    ("R24", "2", "U7", "3"), ("R25", "1", "U7", "3"),
    ("D8", "1", "J7", "1"), ("C6", "1", "J7", "1"), ("F3", "2", "J7", "1"), ("C3", "1", "F3", "1"),
    ("D9", "1", "J7", "2"), ("R4", "1", "J7", "2"), ("R8", "1", "R4", "1"), ("C12", "1", "R8", "2"), ("D4", "3", "R8", "2"),
    ("D7", "1", "J4", "2"), ("C13", "1", "J4", "2"), ("R1", "2", "J4", "2"), ("F5", "2", "R1", "1"), ("R6", "1", "J4", "1"), ("D3", "3", "R6", "2"),
    ("D10", "1", "J8", "2"), ("C5", "1", "J8", "2"), ("R2", "2", "J8", "2"), ("F4", "2", "R2", "1"), ("R3", "2", "J8", "3"), ("D5", "3", "R3", "1"), ("R9", "2", "R3", "1"),
    ("F2", "1", "J2", "2"), ("C15", "1", "F2", "2"),
    ("C8", "1", "U3", "1"), ("C10", "1", "U4", "1"), ("C9", "1", "U3", "8"), ("C11", "1", "U4", "8"),
    ("C4", "1", "U1", "3"),
    ("U2", "6", "J1", "14"), ("C25", "1", "U2", "5"), ("C7", "1", "U2", "1"), ("C24", "1", "U2", "3"),
    ("R15", "2", "U2", "1"), ("R32", "2", "U2", "3"), ("R14", "2", "R6", "2"), ("D3", "3", "R6", "2"),
    ("R30", "1", "J4", "2"), ("R31", "2", "R30", "2"), ("D19", "3", "R30", "2"),
    ("J12", "1", "J9", "1"), ("J13", "1", "J10", "1"),
    ("D14", "1", "J4", "1"), ("D15", "1", "J8", "3"), ("D16", "1", "J9", "2"), ("D17", "1", "J10", "5"),
    ("Q5", "1", "R12", "1"),
    ("R28", "2", "J1", "17"), ("R29", "1", "J1", "17"),
    ("TP2", "1", "C16", "1"), ("TP3", "1", "C14", "1"), ("TP4", "1", "C15", "1"), ("TP5", "1", "C8", "1"), ("TP6", "1", "C6", "1"),
    ("TP1", "1", "C17", "2"),
    ("D20", "1", "J17", "5"), ("U16", "18", "J17", "5"),
    ("D36", "2", "J17", "4"), ("D18", "2", "J17", "3"), ("D21", "2", "J17", "2"), ("Q11", "5", "D36", "2"), ("R58", "2", "Q11", "4"), ("R59", "1", "Q11", "4"),
    ("Q6", "5", "D21", "2"), ("R26", "2", "Q6", "1"), ("R27", "1", "Q6", "1"),
    ("Q7", "5", "D18", "2"), ("R34", "2", "Q7", "1"), ("R35", "1", "Q7", "1"),
    ("R36", "2", "J18", "6"), ("D24", "1", "JP13", "1"), ("R38", "1", "J18", "6"), ("C26", "1", "R38", "2"), ("D26", "3", "R38", "2"),
    ("R37", "2", "J18", "5"), ("D24", "2", "JP14", "1"), ("R39", "1", "J18", "5"), ("C27", "1", "R39", "2"), ("D27", "3", "R39", "2"),
    ("R44", "2", "J18", "4"), ("D25", "1", "JP15", "1"), ("R45", "1", "J18", "4"), ("C30", "1", "R45", "2"), ("D29", "3", "R45", "2"),
    ("R50", "1", "J18", "3"), ("R51", "1", "R50", "2"), ("C33", "1", "R50", "2"), ("D33", "3", "R50", "2"),
    ("R52", "1", "J18", "2"), ("R53", "1", "R52", "2"), ("C34", "1", "R52", "2"), ("D34", "3", "R52", "2"),
    ("U9", "1", "C26", "1"), ("U11", "2", "C30", "1"), ("C28", "1", "U9", "5"), ("C31", "1", "U10", "8"), ("U10", "6", "C33", "1"),
    ("R48", "2", "R49", "1"), ("C32", "1", "R49", "1"), ("R46", "2", "Q8", "1"), ("D30", "2", "Q8", "1"), ("R47", "1", "Q8", "1"), ("Q9", "3", "R47", "2"),
    ("R56", "1", "J2", "5"), ("R57", "1", "J2", "6"), ("JP11", "1", "C7", "2"), ("JP12", "1", "C24", "2"), ("C29", "2", "U6", "1"),
    ("R54", "2", "J11", "3"), ("R55", "2", "J11", "4"), ("JP1", "2", "J11", "3"),
    # ground side of decoupling / converter capacitors to the IC ground pin
    ("C17", "2", "U6", "1"), ("C16", "2", "U6", "1"), ("C19", "2", "U6", "1"), ("C20", "2", "U6", "1"),
    ("C22", "2", "U7", "2"), ("C23", "2", "U7", "2"),
    ("C8", "2", "U3", "4"), ("C10", "2", "U4", "4"), ("C9", "2", "U3", "5"), ("C11", "2", "U4", "5"),
]
SPILL = [(0.6, MOAT_V - 0.6, C_TOP, C_BOT), (MOAT_V, U_X - 6.8, C_TOP, VIC_TOP - 5.0), (MOD[2] + 1.3, W - 0.6, TPIN_TOP + 8.6, DALY_TOP - 2.5)]

MOATS = [  # (x0, y0, x1, y1): no tracks / vias / pours
    (MOAT_V, VIC_TOP - 1.5, VIC_R, VIC_TOP), (MOAT_V, VIC_TOP - 1.5, VIC_L, H),    # Victron
    (VIC_R, DALY_TOP - 1.6, DALY_X, H), (VIC_R, DALY_TOP - 1.6, W, DALY_TOP),  # DALY
]
DOMAINS = {
    "/GND_VICTRON": [(VIC_L, VIC_TOP), (VIC_R, VIC_TOP), (VIC_R, H), (VIC_L, H)],
    "/GND_DALY": [(DALY_X, DALY_TOP), (W, DALY_TOP), (W, H), (DALY_X, H)],
}
GND_OUTLINE = [(0, 0), (W, 0), (W, DALY_TOP - 1.6), (VIC_R, DALY_TOP - 1.6), (VIC_R, VIC_TOP - 1.5),
               (MOAT_V, VIC_TOP - 1.5), (MOAT_V, H), (0, H)]
ISOLATORS = ("U3", "U4", "U5")
M = MOD[0] + 25.1
TEXTS = (("Heltec WiFi LoRa 32 V3.2", M, ROW_TOP + 7.5, 1.5), ("(pin-compatible V3 / V3.1)", M, ROW_TOP + 10.0, 1.0), ("USB-C", MOD[0] + 3.5, ROW_TOP + 11.4, 1.2),
         ("ANT", MOD[0] + 46.5, ROW_TOP + 11.4, 1.2), ("LoRaWAN Water Node v1.0", M, ROW_TOP + 15.0, 1.0), ("franzenhof.com", M, ROW_TOP + 17.5, 1.0))
# visible brand block on the top side, placed after routing on a free spot (outside the module)
BRAND = (("LoRaWAN Water Node v1.0", 1.6), ("franzenhof.com", 1.6))
# bottom-side terminal labels (left->right as seen from the top) and back title block
BACK_LABELS = {"J3": ["VIN", "GND"], "J7": ["+22V", "SIG"], "J4": ["OC1", "OC2", "+5V", "GND"], "J8": ["GND", "3V3", "DQ"],
               "J9": ["GND", "RX", "TX", "V+"], "J10": ["GND", "3V3", "BAT", "S1", "TX", "RX"],
               "J17": ["GND", "OUT3", "OUT2", "OUT1", "+VIN"], "J18": ["GND", "AI2", "AI1", "IN3", "IN2", "IN1"]}
BACK_TITLES = {"J3": "VIN 6-33V DC", "J7": "4-20mA 2-WIRE", "J4": "OC 0-33V", "J8": "1-WIRE DS18B20", "J9": "VE.DIRECT ISO", "J10": "DALY BMS ISO",
               "J17": "OUT 3A LOW-SIDE", "J18": "IN 0-33V / AI 0-10V"}
# quick reference on the bottom side, in the free strip under the Heltec module, plus the title block
# (the released board has these lines left-aligned at x = 50.8 mm, see tools/pcbgen/README.md)
_QR = ["QUICK REFERENCE - LoRaWAN Water Node v1.0",
      "J3   VIN 6-33 V DC (LiFePO4 4s/8s, 12/24 V), reverse-polarity protected",
      "J17  OUT1-3 low side: load between +VIN and OUTx, output switches to GND",
      "     3 A each (2.5 A warm), 3 A total continuous, load voltage = VIN",
      "J18  IN1-3: LOW < 0.8 V or contact to GND, HIGH > 2.2 V or open, max 33 V",
      "     AI1/AI2: 0-10 V, Ri 147 kOhm, 16 bit (ADS1115, I2C 0x48)",
      "J7   4-20 mA two-wire: +22V (switched), SIG to 120 Ohm shunt",
      "J4   OC1/OC2: OC / contact to GND or pulses 3.3-24 V (max 33 V)",
      "I2C  3V3S = switched 3.3 V: BME280 0x76 (J11), ADS1115 0x48, J16",
      "JP1/2 extra 4.7k SCL/SDA pull-ups (10k on board) for long I2C cables",
      "JP8  buck -> 5 V; remove for USB-C power (never USB-C with JP8 fitted)",
      "JP9/JP10  IN/PULSE supply 1-2: always on (IN wake), 2-3: off (0 uA)",
      "JP11/JP12 OC1/OC2 1.5 ms filter: fit = flow/reed < 300 Hz, off = fast",
      "JP13-16  pull IN1-3 / OC1-2: top pair up, bottom pair down (PNP)",
      "J9/J10 isolated; J12/J13 original VE.Direct / DALY cable; JP5 DALY wake",
      "JP3  DALY iso supply 1-2: 3.3 V from BMS, 2-3: 78L33 (only 4s BMS + JP4)",
      "JP6/JP7  J13 pin 5/6: 1-2 = 1:1 cable, 2-3 = crossed cable (both alike)"]
BACK_TEXTS = tuple((t, 26.6, 11.6 + i * 1.22, 0.8 if i else 0.95) for i, t in enumerate(_QR)) + \
             (("LoRaWAN Water Node v1.0", 35.0, 36.8, 1.6), ("franzenhof.com  -  CERN-OHL-P v2", 35.0, 39.4, 1.1))

POWER_NETS = ["GND", "+5V_BUCK", "VIN_F", "+VIN", "+5V", "3V3_HELTEC", "+3V3", "BUCK_SW", "BOOST_IN", "BOOST_SW"]
FINE_NETS, FINE_W = ["VIN_ADC", "LEVEL_ADC", "AI1_ADC", "AI2_ADC", "SDA", "SCL"], 0.2   # ADS1115 0.5 mm pitch
HEAVY_NETS, HEAVY_W = ["VIN_IN", "VIN_LOAD_IN", "+VIN_LOAD", "OUT1", "OUT2", "OUT3"], 1.5   # 3 A outputs (3 A total, eFuse 5.4 A for start currents): ~15 K rise on 35 um
POWER_W = 0.6
DEF_W, CLEAR = 0.3, 0.2
VIA, PVIA = (0.8, 0.4), (1.0, 0.5)
PCLEAR = 0.2
EDGE = 0.5
GAP = 0.55
GND_VIAS = [("U7", "2"),
            # surge current of every TVS / ESD diode goes straight into the ground plane
            ("D12", "2"), ("D8", "2"), ("D9", "2"), ("D7", "2"), ("D10", "2"), ("D14", "3"), ("D15", "3"),
            ("D16", "3"), ("D17", "3"), ("D6", "2"), ("D22", "2"), ("D23", "2"), ("D28", "2"), ("D31", "2"), ("D32", "2"), ("Q6", "2"), ("Q7", "2"), ("Q11", "2"),
            # every decoupling / filter capacitor gets its own ground via (placed before routing)
            ("C5", "2"), ("C6", "2"), ("C7", "2"), ("C8", "2"), ("C10", "2"), ("C12", "2"), ("C13", "2"), ("C14", "2"),
            ("C15", "2"), ("C17", "2"), ("C25", "2"), ("C26", "2"), ("C27", "2"), ("C28", "2"), ("C29", "2"), ("C30", "2"), ("C31", "2"), ("C33", "2"), ("C34", "2"),
            ("C9", "2"), ("C11", "2"), ("C4", "2"),
            ("U2", "2"), ("U9", "2"), ("U11", "3"), ("U10", "3"), ("U6", "1")]                    # SOT-23-6 GND pins sit between two signal pins

# protection right behind the terminal pins (pad at the pin, other pad pointing away)
RA = TPIN - 9.6                # row A pad y (above the terminal label strip)
FIXED_PADS = [
    ("F1", "1", J3_X + 1.0, RA, "2", "up"),                    # J3.1 VBAT_IN -> PTC
    # top I/O block: protection right behind the (rotated) terminal pins
    ("D22", "1", J18_P1 - 5 * PITCH, TPIN_TOP + 9.6, "2", "down"),        # J18 IN1 TVS
    ("D23", "1", J18_P1 - 4 * PITCH, TPIN_TOP + 9.6, "2", "down"),        # J18 IN2 TVS
    ("D28", "1", J18_P1 - 3 * PITCH, TPIN_TOP + 9.6, "2", "down"),        # J18 IN3 TVS
    ("D31", "1", J18_P1 - 2 * PITCH, TPIN_TOP + 9.6, "2", "down"),        # J18 AI1 TVS
    ("D32", "1", J18_P1 - PITCH, TPIN_TOP + 9.6, "2", "down"),            # J18 AI2 TVS
    ("D8", "1", J7_X - 0.5, RA, "2", "up"),              # J7.1 +24V_LOOP TVS
    ("D9", "1", J7_X + PITCH + 1.0, RA, "2", "up"),      # J7.2 shunt-node TVS
    ("D14", "1", J4_X - 0.5, RA + 0.9, "3", "up"),       # J4.1 flow OC ESD
    ("D7", "1", J4_X + 2 * PITCH + 0.5, RA, "2", "up"),  # J4.3 pulse-sensor 5 V TVS
    ("D10", "1", J8_X + PITCH - 1.2, RA, "2", "up"),     # J8.2 1-Wire VCC TVS
    ("D15", "1", J8_X + 2 * PITCH - 0.8, RA + 0.9, "3", "up"),  # J8.3 1-Wire DQ ESD
    ("C10", "1", U_X - 2.48 - 2.22, DALY_TOP + 10.5 - 1.905, "2", "left"),  # ADuM U4 VDD1 (pin 1) bypass
]
# silkscreen labels next to the pins of unpopulated headers
PIN_LABELS = {"TP1": ["GND"], "TP2": ["VIN"], "TP3": ["5V"], "TP4": ["3V3"], "TP5": ["3V3S"], "TP6": ["22V"],
              "J16": ["VIN", "GND", "5V", "GND", "3V3S", "SDA", "SCL", "IO46"], "J11": ["3V3S", "GND", "SCL", "SDA"],
              "JP3": ["BMS", "ISO", "78L33"], "JP4": ["BAT", "78L33"], "JP5": ["S1", "IO45"],
              "JP6": ["TX", "J13.5", "RX"], "JP7": ["RX", "J13.6", "TX"], "JP8": ["BUCK", "5V"],
              "JP9": ["3V3", "IN", "3V3S"], "JP10": ["3V3", "PULSE", "3V3S"], "JP11": ["RC1", "GND"], "JP12": ["RC2", "GND"],
              "JP13": ["PU", "IN1", "PD"], "JP14": ["PU", "IN2", "PD"], "JP15": ["PU", "IN3", "PD"]}
# labels beside the rows of 2-row headers: (ref, [(pad, pad, text), ...])
ROW_LABELS = {"JP16": [("1", "2", "PU"), ("5", "6", "PD")]}     # columns: OC1 | OC2
REFINE = ["C25", "C7", "C24", "C10", "C22", "R24", "R25", "C14", "L1", "C19", "C20", "R4", "R6", "R3", "F3", "C8", "C9", "C11", "C17", "C16", "C18", "C23", "C29", "C28", "C31"]

# extra silkscreen notes placed relative to a footprint: (ref, text, dx, dy, size)
NOTES = [("JP8", "JP8 BUCK -> 5V rail\nremove for USB-C power", 0, 0, 0.8),
         ("J11", "J11 BME280 (I2C)", 0, 0, 0.8), ("J12", "J12 VE.Direct\noriginal cable", 0, 0, 0.8), ("J13", "J13 DALY\noriginal cable", 0, 0, 0.8),
         ("J16", "J16 EXPANSION 3.3V logic", 0, 0, 0.8), ("JP1", "JP1/JP2 extra 4k7 I2C pull-up\nfit for long cables", 0, 0, 0.8),
         ("JP9", "JP9 IN1-3 SUPPLY\n1-2 always on (wake-up)\n2-3 switched (0 uA)", 0, 0, 0.8),
         ("JP10", "JP10 PULSE SUPPLY\n1-2 always on\n2-3 switched (0 uA)", 0, 0, 0.8),
         ("JP11", "JP11 FILTER OC1\nfit = 1.5 ms low-pass\n(flow sensor, reed)", 0, 0, 0.8), ("JP12", "JP12 FILTER OC2\nfit = 1.5 ms low-pass\n(flow sensor, reed)", 0, 0, 0.8),
         ("JP3", "JP3 DALY ISO SUPPLY\n1-2 3.3V from BMS\n2-3 on-board 78L33", 0, 0, 0.8),
         ("JP4", "JP4 78L33 from BMS BAT\nonly 4s BMS (< 30 V)", 0, 0, 0.8), ("JP5", "JP5 DALY wake-up (S1)", 0, 0, 0.8),
         ("JP6", "JP6/JP7 = J13 pin 5/6\n1-2: 1:1 cable\n2-3: crossed cable", 0, 0, 0.8),
         ("J17", "OUTx switches to GND:\nload between +VIN and OUTx\n3 A each, 3 A total", 0, 0, 0.8),
         ("J18", "IN: LOW < 0.8 V / contact to GND\nHIGH > 2.2 V or open, max 33 V\nAI: 0-10 V, Ri 147 kOhm", 0, 0, 0.8)]
TERM_FUNC = {"J3": "6-33V", "J7": "4-20mA", "J4": "OC 0-33V", "J8": "DS18B20", "J9": "VE.DIRECT ISO", "J10": "DALY BMS ISO",
             "J17": "OUT 3A TO GND", "J18": "IN 0-33V  AI 0-10V"}
# pin functions printed on the top side right at each terminal (left -> right as seen on the board)
TOP_PIN_LABELS = {"J3": ["VIN", "GND"], "J7": ["+22V", "SIG"], "J4": ["OC1", "OC2", "+5V", "GND"], "J8": ["GND", "3V3", "DQ"],
                  "J9": ["GND", "RX", "TX", "V+"], "J10": ["GND", "3V3", "BAT", "S1", "TX", "RX"],
                  "J17": ["+VIN", "OUT1", "OUT2", "OUT3", "GND"], "J18": ["IN1", "IN2", "IN3", "AI1", "AI2", "GND"]}
SOLID_GND = ("J1", "J2", "J11", "J12", "J13", "J16", "TP1")
