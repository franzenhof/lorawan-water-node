"""Schematic placement for the water node hardware v1.0 (gen_sch2.py), same conventions as schem_w.py.

Coordinates in 2.54 mm grid units, relative to the block origin.
  R/C/L/F: a=90 horizontal (pin 1 left), a=270 (pin 1 right), a=0 vertical (pin 1 top)
  diodes:  a=270 vertical (K top), a=180 horizontal (K right)
  BAT54S:  a=180 -> COM up, K (pin 2) left, A (pin 1) right
  FET:     a=0 -> G left, D top, S bottom; P-FET a=180 -> G right, S top, D bottom
"""
TITLE = "LoRaWAN Water Node"
REV = "1.0"
COMMENTS = ["VIN 6-33 V; all inputs and outputs on RTC GPIOs; parts with LCSC numbers (JLCPCB)",
            "Isolated grounds: GND_VICTRON, GND_DALY", "franzenhof.com"]
PAPER = "A1"

RAILS = {"GND": "GND", "GND_VICTRON": "GND", "GND_DALY": "GND",
         "+3V3": "+3V3", "3V3_HELTEC": "+3V3", "+3V3_SW": "+3V3", "+3V3_SWB": "+3V3",
         "+5V": "+5V", "+5V_BUCK": "+5V", "+5V_FLOW": "+5V",
         "+VIN": "+VDC", "+VIN_LOAD": "+VDC", "+24V_LOOP": "+24V",
         "VDD_VICTRON": "VCC", "VDD_DALY": "VCC"}
DOWN_RAILS = ["GND", "GND_VICTRON", "GND_DALY"]
PWR_FLAGS = ["GND", "GND_VICTRON", "GND_DALY", "+3V3", "3V3_HELTEC", "+3V3_SW", "+3V3_SWB", "+5V", "+5V_BUCK", "+5V_FLOW",
             "+VIN", "VIN_LOAD_IN", "+24V_LOOP", "VDD_VICTRON", "VDD_DALY", "BOOST_IN", "REG_IN", "DIN_VCC", "PULSE_VCC"]
FLAG_AT = (70, 52)
LABEL_PARTS = ["J1", "J2", "J3", "J4", "J7", "J8", "J9", "J10", "J11", "J12", "J13", "J16", "J17", "J18",
               "JP3", "JP4", "JP5", "JP6", "JP7", "JP9", "JP10", "JP13", "JP14", "JP15", "JP16", "U10"]
LABEL_ONLY_NETS = ["BOOST_FB", "DIN_VCC", "PULSE_VCC", "BUCK_VCC"]
LABEL_AT = {("F1", "1"): True, ("R4", "1"): True, ("C13", "1"): True, ("C5", "1"): True, ("D18", "2"): True,
            ("D21", "2"): True, ("D36", "2"): True, ("D22", "1"): True, ("D23", "1"): True, ("R18", "2"): True}

M = {"m": True}
H, HR, V, VF = {"a": 90}, {"a": 270}, {"a": 0}, {"a": 180}
DK, DH = {"a": 270}, {"a": 180}
BS = {"a": 180}
PF = {"a": 180}                    # P-FET: G right, S top, D bottom
PFL = {"a": 180, "m": True}        # P-FET: G left, S top, D bottom

BLOCKS = [
    dict(title="Heltec WiFi LoRa 32 V3.2", x=0, y=0, w=66, h=62, parts=[
        ("J1", 16, 30), ("J2", 40, 30, M),
        ("R28", 8, 48, V), ("R29", 8, 56, V), ("R56", 14, 48, V), ("R57", 14, 56, V),
        ("J16", 30, 50, M),
        ("TP1", 44, 47), ("TP2", 47, 47, VF), ("TP3", 50, 47, VF), ("TP4", 53, 47, VF), ("TP5", 56, 47, VF),
        ("TP6", 59, 47, VF), ("TP7", 47, 53, VF), ("TP8", 53, 53, VF)],
        notes=[(3, 7, "J1 = header on the 5 V side, J2 = header on the 3V3 side (pin 1 = GND, USB-C end)\n"
                      "V3 / V3.1 / V3.2 are pin-compatible"),
               (3, 42, "HW revision ID (GPIO20): v1.0 = 100k / 100k -> 0.5 x 3V3\nR56 / R57: strapping pins GPIO46 / GPIO45 LOW at boot"),
               (22, 60, "J16 expansion header (I2C on 3V3S, GPIO46), TP1-TP8 test pads (not populated)")]),
    dict(title="Power supply: VIN 6-33 V -> 5 V buck, 3.3 V", x=68, y=0, w=96, h=46, parts=[
        ("J3", 5, 10, M),
        ("F1", 12, 9, H), ("D12", 16, 10.5, DK), ("D11", 21, 9, DH), ("C16", 25, 12, V), ("C17", 28, 12, V),
        ("U6", 38, 11), ("C18", 47, 8, H), ("C29", 50, 17, V), ("L1", 52, 10, H), ("C19", 57, 12, V), ("C20", 61, 12, V),
        ("R40", 66, 12, V), ("R41", 66, 18, V),
        ("JP8", 30, 20), ("D6", 36, 22, DK), ("C1", 40, 22, V), ("C14", 44, 22, V),
        ("U15", 8, 32), ("C37", 3, 35, V), ("C38", 13, 35, V),
        ("F2", 19, 32, H), ("C2", 25, 34, V), ("C15", 28, 34, V)],
        notes=[(70, 8, "LMR36510: 4.2-65 V in, 5 V / 1 A out, Iq 26 uA\nVout = 1.0 V x (1 + 100k / 24.9k) = 5.02 V\nVIN: PTC 1.1 A 60 V, TVS SMBJ33A, reverse diode SS34"),
               (50, 20, "JP8 on = buck supplies 5 V (normal)\nJP8 off = Heltec USB-C only (bench test)"),
               (33, 32, "3.3 V sensor rail: own LDO XC6206 (Iq 1 uA) from 5 V, PTC 250 mA\nHeltec 3V3 supplies only the module")]),
    dict(title="4-20 mA level sensor: 5 V -> 24 V boost switched by GPIO47", x=166, y=0, w=76, h=52, parts=[
        ("Q5", 6, 10, PF), ("R17", 11, 8.5, V), ("R12", 18, 10, H), ("Q4", 22, 12), ("R13", 18, 13.5, V), ("C36", 14, 15.5, V),
        ("C22", 12, 21.5, V), ("L2", 24, 17, H), ("U7", 24, 21), ("D13", 31, 20, DH), ("C23", 35, 21.5, V),
        ("R24", 39, 21.5, V), ("R25", 43, 27.5, V), ("F3", 48, 20, H),
        ("J7", 4, 44, M), ("C3", 6, 35.5, V), ("C6", 10, 35.5, V), ("D8", 14, 35.5, DK),
        ("R4", 20, 43.5, V), ("D9", 23, 43.5, DK), ("R8", 28, 42, H), ("C12", 32, 43.5, V), ("D4", 36, 44, BS)],
        notes=[(34, 8, "Q5 switches the 5 V input of the MT3608 boost:\n24 V = 0.6 V x (1 + 390k / 10k)\nC36 1 uF gate-drain: soft start ~10 ms (~13 mA)"),
               (40, 34, "J7: LOOP+ = +24V_LOOP, LOOP- = LEVEL_SHUNT\nR4 120 R 0.1 % shunt (4-20 mA -> 0.48-2.4 V) -> ADS1115 AIN1")]),
    dict(title="Switched rails", x=244, y=0, w=40, h=40, parts=[
        ("Q1", 8, 10, PF), ("R18", 12, 8.5, V), ("C8", 26, 12, V), ("C10", 30, 12, V),
        ("Q10", 30, 26, PF), ("C35", 36, 28, V),
        ("R7", 10, 24.5, V), ("Q3", 14, 26), ("R16", 10, 29.5, V),
        ("Q2", 20, 22, PFL), ("R19", 16, 20.5, V)],
        notes=[(3, 34, "GPIO38 LOW = +3V3_SW and +3V3_SWB on\n+5V_FLOW follows +3V3_SW")]),
    dict(title="Pulse inputs (flow sensor + 2nd input)", x=0, y=64, w=66, h=48, parts=[
        ("J4", 4, 30, M), ("D14", 12, 15, {"a": 90}),
        ("R30", 17, 9, H), ("R31", 9, 8, V), ("D19", 24, 11, DK), ("R32", 30, 9, H), ("C24", 34, 10.5, V), ("JP12", 36, 15),
        ("U2", 42, 9, {"u": 2}),
        ("R6", 17, 22, H), ("R14", 9, 21, V), ("D3", 24, 24, DK), ("D40", 4, 11, {"a": 90}), ("R15", 30, 22, H), ("C7", 34, 23.5, V), ("JP11", 36, 28),
        ("U2", 42, 22, {"u": 1}), ("U2", 56, 30, {"u": 3}), ("C25", 60, 30, V),
        ("F5", 8, 40, H), ("R1", 13, 40, H), ("C13", 17, 41.5, V), ("D7", 20, 41.5, DK), ("JP10", 60, 40, M),
        ("JP16", 41, 32)],
        notes=[(26, 36, "J4: OC1 | OC2 | +5V | GND, pulse 1 -> GPIO5, pulse 2 -> GPIO6\nopen collector / contact / 3.3-24 V logic, up to VIN (36 V TVS, 10k series, 5.6 V zener)\n"
                        "74LVC2G14 inverts: pulse = HIGH; JP11 / JP12 on = low-pass\nJP10: 1-2 = powered in deep sleep, 2-3 = switched\n"
                        "JP16: OC1 1/3/5, OC2 2/4/6; 1-3 = pull-up, 3-5 = pull-down (PNP)")]),
    dict(title="1-Wire (DS18B20)", x=68, y=64, w=40, h=32, parts=[
        ("F4", 8, 10, H), ("R2", 13, 10, H), ("C5", 17, 11.5, V), ("D10", 20, 11.5, DK), ("R9", 25, 11.5, V),
        ("D15", 8, 23.5, {"a": 270}), ("R3", 14, 20, HR), ("D5", 21, 22, BS), ("J8", 36, 18)],
        notes=[(3, 28, "J8: GND | 3V3 | DQ")]),
    dict(title="I2C (switched 3.3 V): BME280", x=110, y=64, w=34, h=28, parts=[
        ("J11", 4, 12, M), ("R10", 12, 14, H), ("R11", 12, 17, H), ("JP1", 20, 14), ("JP2", 20, 18), ("R54", 27, 8, V), ("R55", 31, 8, V)],
        notes=[(3, 23, "J11: 3V3S | GND | SCL | SDA, R54 / R55 10k on-board\nJP1 / JP2 on = extra 4.7k on SCL / SDA")]),
    dict(title="Outputs VIN (low side, 3 A)", x=146, y=64, w=44, h=61, parts=[
        ("J17", 41, 6), ("Q12", 15, 10, PF), ("D37", 23, 8.5, {"a": 90}), ("R63", 27, 13, V),
        ("D36", 35, 17.5, DK), ("Q11", 26, 22), ("R58", 19, 22, H), ("U14", 11, 22), ("R62", 5, 23.5, V), ("R59", 22, 23.5, V),
        ("D18", 35, 31.5, DK), ("Q7", 26, 36), ("R34", 19, 36, H), ("U13", 11, 36), ("R61", 5, 37.5, V), ("R35", 22, 37.5, V),
        ("D21", 35, 45.5, DK), ("Q6", 26, 50), ("R26", 19, 50, H), ("U12", 11, 50), ("R60", 5, 51.5, V), ("R27", 22, 51.5, V)],
        notes=[(2, 4, "Q12: reverse-\npolarity P-FET\nD37: gate clamp\n-> eFuse U16"),
               (3, 56, "J17: +VIN | OUT1 | OUT2 | OUT3 | GND, max 3 A each, 3 A total (eFuse U16: 5.4 A limit for start currents)\nGPIO19 = OUT1, GPIO2 = OUT2, GPIO1 = OUT3 (HIGH = on)\nU12-U14 74AHCT1G125: 5 V gate drive, R60-R62 off at reset")]),
    dict(title="Load supply eFuse (+VIN_LOAD)", x=258, y=64, w=46, h=50, parts=[
        ("U16", 22, 20), ("C39", 6, 13, V), ("R64", 34, 27, V),
        ("D38", 14, 38), ("D39", 14, 44), ("R65", 30, 42, V)],
        notes=[(3, 47, "U16 TPS16630: 60 V eFuse in the load supply behind Q12, ILIM 3.3k = 5.4 A, auto-retry\n"
                        "SHDN = OR of the output GPIOs: eFuse off (21 uA) while all outputs are off")]),
    dict(title="Digital inputs 0-33 V", x=192, y=64, w=64, h=62, parts=[
        ("J18", 48, 51, M), ("D22", 8, 11.5, DK), ("R36", 13, 7.5, V), ("R38", 18, 10, H), ("C26", 22, 11.5, V),
        ("D26", 27, 13, DK), ("U9", 36, 10, {"u": 1}),
        ("D23", 8, 27.5, DK), ("R37", 13, 23.5, V), ("R39", 18, 26, H), ("C27", 22, 27.5, V),
        ("D27", 27, 29, DK), ("U9", 36, 26, {"u": 2}),
        ("D28", 8, 43.5, DK), ("R44", 13, 39.5, V), ("R45", 18, 42, H), ("C30", 22, 43.5, V),
        ("D29", 27, 45, DK), ("U11", 36, 42),
        ("U9", 50, 18, {"u": 3}), ("C28", 56, 18, V), ("JP9", 57, 33), ("D24", 4, 8.5, {"a": 90}), ("D25", 4, 24, {"a": 90}),
        ("JP13", 46, 30, M), ("JP14", 46, 37, M), ("JP15", 46, 44, M)],
        notes=[(3, 56, "J18: IN1 | IN2 | IN3 | AI1 | AI2 | GND; dry contact / OC / 3.3-24 V logic\n47k series -> < 0.5 mA at 24 V, 5.6 V zener; inverted (input high = GPIO low)\n"
                        "IN1 -> GPIO3, IN2 -> GPIO4, IN3 -> GPIO7; JP9: 1-2 = active in deep sleep, 2-3 = switched\n"
                        "JP13-JP15: 1-2 = pull-up, 2-3 = pull-down (PNP), open = none")]),
    dict(title="Analog: ADS1115 (VIN, 4-20 mA, 2 x 0-10 V)", x=142, y=126, w=76, h=60, parts=[
        ("D31", 6, 9.5, DK), ("R50", 12, 8, H), ("R51", 18, 9.5, V), ("C33", 22, 9.5, V), ("D33", 27, 10, BS),
        ("D32", 6, 23.5, DK), ("R52", 12, 22, H), ("R53", 18, 23.5, V), ("C34", 22, 23.5, V), ("D34", 27, 24, BS),
        ("U10", 50, 24), ("C31", 58, 16, V),
        ("Q8", 8, 38, PF), ("R46", 13, 36.5, V), ("D30", 18, 36.5, DK), ("R47", 24, 38, H), ("Q9", 30, 40),
        ("R48", 7, 44, V), ("R49", 7, 50, V), ("C32", 11, 50, V)],
        notes=[(18, 50, "AI1 / AI2: 100k / 47k -> 10 V = 3.2 V; VIN: 100k / 10k (1:11), switched by Q8 with +3V3_SW\n"
                        "U10 ADS1115 at 0x48 on the switched 3.3 V: AIN0 VIN, AIN1 4-20 mA, AIN2 AI1, AIN3 AI2")]),
    dict(title="Victron VE.Direct (isolated)", x=0, y=114, w=62, h=36, parts=[
        ("U3", 30, 16), ("C9", 40, 10, V), ("D16", 42, 23), ("J9", 56, 16), ("J12", 56, 27)],
        notes=[(3, 26, "GPIO33 = RX (from Victron TX), GPIO34 = TX\nJ9: GND | RX | TX | +V\nJ12: JST-PH for the original (crossed) cable")]),
    dict(title="DALY Smart BMS (isolated)", x=64, y=114, w=76, h=48, parts=[
        ("U4", 30, 16), ("C11", 40, 10, V), ("D17", 42, 23),
        ("U1", 16, 36), ("C4", 10, 37.5, V),
        ("R5", 26, 35, H), ("U5", 34, 36), ("J10", 70, 14), ("J13", 70, 25),
        ("JP4", 4, 36, M), ("JP3", 26, 42, M), ("JP5", 18, 30, M), ("JP6", 54, 42), ("JP7", 62, 42)],
        notes=[(44, 30, "J10 / J13: GND | 3V3 | 12V | S1 | TX | RX\nJP3 DALY VDD: 1-2 BMS 3.3 V, 2-3 78L33 (4s BMS only)\nJP4: 78L33 on, JP5: wake-up via GPIO45\n"
                        "JP6 / JP7: J13 pin 5/6\n  1-2 = 1:1 cable, 2-3 = crossed cable")]),
]
