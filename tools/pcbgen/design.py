# lorawan-water-node hardware v1.0: KiCad/SMD design data (the Fritzing board is
# kept as the prototype in pcb/prototype).
# Every input, pulse input and output sits on an RTC GPIO (0-21): inputs can wake
# the ESP32-S3 from deep sleep, outputs are held per pin through deep sleep.
# Each part: ref, (lib, symbol), value, footprint, {pin: net|None}, placement key, block
R0805 = "Resistor_SMD:R_0805_2012Metric"
R1206 = "Resistor_SMD:R_1206_3216Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C1210 = "Capacitor_SMD:C_1210_3225Metric"
CP63 = "Capacitor_SMD:CP_Elec_6.3x7.7"
F1206 = "Fuse:Fuse_1206_3216Metric"
F1812 = "Fuse:Fuse_1812_4532Metric"
F2920 = "Fuse:Fuse_2920_7451Metric"
SMB = "Diode_SMD:D_SMB"
SMA = "Diode_SMD:D_SMA"
SOT23 = "Package_TO_SOT_SMD:SOT-23"
HDR = "Connector_PinHeader_2.54mm:PinHeader_1x{n:02d}_P2.54mm_Vertical"
SOCK18 = "Connector_PinSocket_2.54mm:PinSocket_1x18_P2.54mm_Vertical"
TB = "TerminalBlock_Phoenix:TerminalBlock_Phoenix_PT-1,5-{n}-3.5-H_1x{n:02d}_P3.50mm_Horizontal"

def conn(n): return ("Connector_Generic", f"Conn_01x{n:02d}")

PARTS = []
def P(ref, sym, value, fp, pins, place, block, desc="", lcsc_key=None, dnp=False):
    PARTS.append(dict(ref=ref, sym=sym, value=value, fp=fp, pins=pins, place=place,
                      block=block, desc=desc, lcsc_key=lcsc_key or value, dnp=dnp))

# ---------------- Heltec WiFi LoRa 32 V3 -----------------------------------
j1 = {str(i): None for i in range(1, 19)}
j1.update({"1": "GND", "2": "+5V", "11": "VE_ESP_TX", "12": "VE_ESP_RX", "13": "GPIO47", "14": "DALY_ESP_TX", "17": "HW_ID", "18": "OUT1_G"})
P("J1", conn(18), "Heltec V3 header (5V side)", SOCK18, j1, "HELTEC_L", "MCU",
  "Heltec pins: 1 GND, 2 5V, 11 GPIO34, 12 GPIO33, 13 GPIO47, 14 GPIO48, 17 GPIO20, 18 GPIO19", lcsc_key="SOCK18")
j2 = {str(i): None for i in range(1, 19)}
j2.update({"1": "GND", "2": "3V3_HELTEC", "5": "GPIO46", "6": "GPIO45", "7": "SCL", "8": "SDA", "9": "DALY_ESP_RX",
           "10": "ONEWIRE_IO", "11": "GPIO38", "12": "OUT3_G", "13": "OUT2_G", "14": "DIN1_OUT", "15": "DIN2_OUT",
           "16": "FLOW_OUT", "17": "FLOW2_OUT", "18": "DIN3_OUT"})
P("J2", conn(18), "Heltec V3 header (3V3 side)", SOCK18, j2, "HELTEC_R", "MCU",
  "Heltec pins: 1 GND, 2 3V3, 5 GPIO46, 6 GPIO45, 7 GPIO42, 8 GPIO41, 9 GPIO40, 10 GPIO39, 11 GPIO38, 12-18 GPIO1-7",
  lcsc_key="SOCK18")

# ---------------- Power input VIN 6-33 V (4s/8s LiFePO4, 12/24 V lead, 24 V PLC supply) -> 5 V / 3.3 V
P("J3", conn(2), "VIN 6-33V", TB.format(n=2), {"1": "VIN_IN", "2": "GND"}, None, "Power", "1 VIN (6-33 V), 2 GND", lcsc_key="TB2")
P("F1", ("Device", "Polyfuse"), "PTC 1.1A 60V", F2920, {"1": "VIN_IN", "2": "VIN_F"}, None, "Power")
# bidirectional TVS: a reversed battery does not drive it into forward conduction (D11 and Q12 block reverse polarity)
P("D12", ("Device", "D_TVS"), "SMBJ33CA", SMB, {"1": "VIN_F", "2": "GND"}, None, "Power", "TVS 33V, bidirectional (clamps < 54 V)")
P("D11", ("Diode", "SS34"), "SS210", "Diode_SMD:D_SMA", {"1": "+VIN", "2": "VIN_F"}, None, "Power",
  "reverse-polarity protection (100 V: blocks the full reversed VIN)", lcsc_key="SS210")
# VIN -> 5 V buck (LMR36510A: 4.2-65 V, 1 A, 400 kHz, 26 uA Iq; Vout = 1.0 V * (1 + 100k / 24.9k) = 5.02 V)
P("U6", ("Regulator_Switching", "LMR36510ADDA"), "LMR36510ADDAR",
  "Package_SO:Texas_HTSOP-8-1EP_3.9x4.9mm_P1.27mm_EP2.95x4.9mm_Mask2.4x3.1mm_ThermalVias",
  {"1": "GND", "2": "+VIN", "3": "+VIN", "4": None, "5": "BUCK_FB", "6": "BUCK_VCC", "7": "BUCK_BST", "8": "BUCK_SW", "9": "GND"},
  None, "Power", "4.2-65 V in, 5 V / 1 A, 26 uA Iq; EN tied to VIN", lcsc_key="LMR36510")
P("C16", ("Device", "C"), "10uF 100V", C1210, {"1": "+VIN", "2": "GND"}, None, "Power")
P("C17", ("Device", "C"), "220nF 100V", C0805, {"1": "+VIN", "2": "GND"}, None, "Power", "HF input capacitor at the VIN pin (220 nF per TI datasheet 8.2.1.2.5)")
P("C18", ("Device", "C"), "100nF", C0805, {"1": "BUCK_BST", "2": "BUCK_SW"}, None, "Power", "bootstrap")
P("C29", ("Device", "C"), "1uF", C0805, {"1": "BUCK_VCC", "2": "GND"}, None, "Power", "internal LDO (VCC pin)")
# Isat must exceed the LMR36510 low-side current limit (up to 1.6 A): SWPA4030 (1.3 A) is too small
P("L1", ("Device", "L"), "22uH", "Inductor_SMD:L_Sunlord_SWPA6045S", {"1": "BUCK_SW", "2": "+5V_BUCK"}, None, "Power", "Sunlord SWPA6045S220MT (Isat 2.05 A)", lcsc_key="L22u6045")
P("R40", ("Device", "R"), "100k", R0805, {"1": "+5V_BUCK", "2": "BUCK_FB"}, None, "Power", "feedback divider top")
P("R41", ("Device", "R"), "24.9k", R0805, {"1": "BUCK_FB", "2": "GND"}, None, "Power", "feedback divider bottom (5.02 V)")
P("C19", ("Device", "C"), "22uF 25V", C0805, {"1": "+5V_BUCK", "2": "GND"}, None, "Power", lcsc_key="22u")
P("C20", ("Device", "C"), "22uF 25V", C0805, {"1": "+5V_BUCK", "2": "GND"}, None, "Power", lcsc_key="22u")
P("JP8", conn(2), "5V BUCK", HDR.format(n=2), {"1": "+5V_BUCK", "2": "+5V"}, None, "Power",
  "jumper on = buck supplies the 5 V rail (normal); off = board powered via the Heltec USB-C only (bench test / flashing)", lcsc_key="HDR2")
P("D6", ("Device", "D_Zener"), "SMBJ6.0A", SMB, {"1": "+5V", "2": "GND"}, "SMBJ6.0A@562", "Power", "TVS 6V, unidirectional (5.0A would leak at the 5.02 V rail)", lcsc_key="SMBJ6.0A")
P("C1", ("Device", "C"), "22uF 25V", C0805, {"1": "+5V", "2": "GND"}, "C1", "Power", "5 V bulk at the Heltec 5V pin (LoRa TX bursts)", lcsc_key="22u")
P("C14", ("Device", "C"), "100nF", C0805, {"1": "+5V", "2": "GND"}, "C14", "Power")
# own 3.3 V sensor rail from +5V: the Heltec regulator only supplies the module; XC6206 Iq about 1 uA (deep sleep)
P("U15", ("Regulator_Linear", "XC6206PxxxMR"), "XC6206P332MR", SOT23, {"1": "GND", "2": "3V3_LDO", "3": "+5V"}, None, "Power",
  "Torex XC6206P332MR-G: 3.3 V / 200 mA LDO, 1 uA Iq, stable with 1 uF ceramic; pin 1 VSS, 2 VOUT, 3 VIN")
P("C37", ("Device", "C"), "1uF", C0805, {"1": "+5V", "2": "GND"}, None, "Power", "U15 input capacitor")
P("C38", ("Device", "C"), "1uF", C0805, {"1": "3V3_LDO", "2": "GND"}, None, "Power", "U15 output capacitor (before F2)")
P("F2", ("Device", "Polyfuse"), "PTC 0.25A", F1206, {"1": "3V3_LDO", "2": "+3V3"}, "F2", "Power")
P("C2", ("Device", "C"), "22uF 25V", C0805, {"1": "+3V3", "2": "GND"}, "C2", "Power", "3.3 V sensor-rail bulk", lcsc_key="22u")
P("C15", ("Device", "C"), "100nF", C0805, {"1": "+3V3", "2": "GND"}, "C15", "Power")

# ================= I/O block at the top edge (RTC GPIOs) =================
# --- 3 switched outputs (low side), 3 A each: load between J17 "+VIN" and OUT1/OUT2/OUT3
# load supply straight from the VIN terminal (not through F1): reverse-polarity P-FET Q12, then eFuse U16 (5.4 A limit,
# 1 us short-circuit trip) for all three outputs and the +VIN terminal; U16 is only enabled while an output is on
P("J17", conn(5), "Outputs VIN", TB.format(n=5), {"1": "GND", "2": "OUT3", "3": "OUT2", "4": "OUT1", "5": "+VIN_LOAD"}, None, "Outputs",
  "read left->right on the board: +VIN | OUT1 | OUT2 | OUT3 | GND (terminal rotated 180 deg)", lcsc_key="TB5")
P("Q12", ("Transistor_FET", "Q_PMOS_GDS"), "NCE60P50K", "Package_TO_SOT_SMD:TO-252-2",
  {"1": "LOAD_GATE", "2": "VIN_IN", "3": "VIN_LOAD_IN"}, None, "Outputs",
  "60 V P-channel reverse-polarity switch of the load supply (replaces diode D20); drain at VIN, source at +VIN_LOAD; 28 mOhm max at VGS=-10 V",
  lcsc_key="NCE60P50K")
P("D37", ("Device", "D_Zener"), "BZT52C12", "Diode_SMD:D_SOD-123", {"1": "VIN_LOAD_IN", "2": "LOAD_GATE"}, None, "Outputs",
  "12 V gate-source clamp for Q12 (VIN up to 33 V)")
P("R63", ("Device", "R"), "1M", R0805, {"1": "LOAD_GATE", "2": "GND"}, None, "Outputs",
  "pulls Q12 gate low; 1M keeps the standing zener current (deep sleep) at 21 uA (33 V)")
# eFuse U16 (TI TPS16630, 60 V / 31 mOhm): UVLO tied to IN (always above threshold), OVP off (GND), MODE = GND auto-retry,
# dVdT open (fast start), ILIM 18 / 3.3k = 5.4 A (datasheet: 0.6-6 A, R_ILIM >= 3k): pump / motor start currents up to 5.4 A pass;
# above that U16 regulates the current for max. 162 ms (or until thermal shutdown), then auto-retry after 648 ms; hard short: off within 1 us
# continuous rating stays 3 A in total (tracks, Q12); the eFuse is the short-circuit / stall protection, not a 3 A fuse
P("U16", ("water_node", "TPS16630PWP"), "TPS16630PWPR",
  "Package_SO:HTSSOP-20-1EP_4.4x6.5mm_P0.65mm_EP3.4x6.5mm_Mask2.96x2.96mm_ThermalVias",
  {"1": "VIN_LOAD_IN", "2": "VIN_LOAD_IN", "3": "VIN_LOAD_IN", "4": None, "5": None, "6": "VIN_LOAD_IN", "7": "VIN_LOAD_IN",
   "8": "GND", "9": "GND", "10": None, "11": "EF_ILIM", "12": "GND", "13": "EF_SHDN", "14": None, "15": None, "16": None,
   "17": None, "18": "+VIN_LOAD", "19": "+VIN_LOAD", "20": "+VIN_LOAD", "21": "GND"}, None, "Load eFuse",
  "load-supply eFuse: 5.4 A current limit, short-circuit trip, auto-retry after 0.65 s", lcsc_key="TPS16630")
P("C39", ("Device", "C"), "100nF 100V", C0805, {"1": "VIN_LOAD_IN", "2": "GND"}, None, "Load eFuse", "U16 input capacitor")
P("R64", ("Device", "R"), "3.3k", R0805, {"1": "EF_ILIM", "2": "GND"}, None, "Load eFuse", "U16 current limit 18 / 3.3k = 5.4 A (start current of pumps / motors)")
# SHDN: diode OR of the three output GPIOs (BAV70 common cathode) with 47k pull-down; all outputs off -> eFuse in
# shutdown (21 uA instead of 1.4 mA); the SHDN input sources < 10 uA, so 47k keeps it below the 0.8 V threshold
P("D38", ("Diode", "BAV70"), "BAV70", SOT23, {"1": "OUT1_G", "2": "OUT2_G", "3": "EF_SHDN"}, None, "Load eFuse", "OUT1 / OUT2 -> U16 SHDN")
P("D39", ("Diode", "BAV70"), "BAV70", SOT23, {"1": "OUT3_G", "2": None, "3": "EF_SHDN"}, None, "Load eFuse", "OUT3 -> U16 SHDN")
P("R65", ("Device", "R"), "47k", R0805, {"1": "EF_SHDN", "2": "GND"}, None, "Load eFuse", "U16 SHDN pull-down (off while all outputs are off)")
# channel order follows the board from left to right (J17 OUT1 | OUT2 | OUT3 above Q11 | Q7 | Q6): OUT1 = GPIO19, OUT3 = GPIO1
for n, (q, rs, rp, d, gpio, u, rd) in enumerate((("Q11", "R58", "R59", "D36", "GPIO19", "U14", "R62"),
                                                 ("Q7", "R34", "R35", "D18", "GPIO2", "U13", "R61"),
                                                 ("Q6", "R26", "R27", "D21", "GPIO1", "U12", "R60")), 1):
    # 5 V gate driver: the AON7264E is only specified from 4.5 V gate drive; the TTL input takes the 3.3 V GPIO
    P(u, ("74xGxx", "74LVC1G125"), "SN74AHCT1G125DCKR", "Package_TO_SOT_SMD:SOT-353_SC-70-5",
      {"1": "GND", "2": f"OUT{n}_G", "3": "GND", "4": f"OUT{n}_DRV", "5": "+5V"}, None, "Outputs",
      f"OUT{n} gate driver 3.3 V -> 5 V (+5V is on in deep sleep, follows the held GPIO)", lcsc_key="AHCT1G125")
    P(rd, ("Device", "R"), "100k", "Resistor_SMD:R_0402_1005Metric", {"1": f"OUT{n}_G", "2": "GND"}, None, "Outputs",
      "driver input pull-down: output off while the GPIO floats at reset", lcsc_key="100k0402")
    P(q, ("Transistor_FET", "SiS454DN"), "AON7264E", "Package_SO:Vishay_PowerPAK_1212-8_Single",
      {"1": "GND", "2": "GND", "3": "GND", "4": f"{q}_GATE", "5": f"OUT{n}"}, None, "Outputs",
      f"OUT{n} low-side switch 60 V, 13 mOhm at 4.5 V, driven with 5 V by {u}; {gpio} HIGH = on")
    P(rs, ("Device", "R"), "220", R0805, {"1": f"OUT{n}_DRV", "2": f"{q}_GATE"}, None, "Outputs", "gate series resistor")
    P(rp, ("Device", "R"), "100k", R0805, {"1": f"{q}_GATE", "2": "GND"}, None, "Outputs", f"keeps OUT{n} off at reset / without firmware")
    P(d, ("Diode", "SS34"), "SS210", "Diode_SMD:D_SMA", {"1": "+VIN_LOAD", "2": f"OUT{n}"}, None, "Outputs", f"freewheel diode OUT{n} (60 V: TVS clamp 53 V)", lcsc_key="SS210")

# --- 3 protected digital inputs, 0-33 V tolerant, also for dry contacts / OC / 3.3-24 V logic
# terminal -> TVS SMAJ33CA (bidirectional: also a reversed 24 V source); pull-up DIN_VCC -> Schottky (70 V) -> 100k; 47k series -> 5.6 V zener to GND + 100nF -> Schmitt
# JP13-JP15 per input: 1-2 = 100k pull-up (contacts, NPN / OC), 2-3 = 100k pull-down (PNP sensors), open = none (push-pull)
# (a clamp into +5V would lift the rail in deep sleep: 0.5 mA per input at 24 V, the buck cannot sink it)
# DIN_VCC (JP9): 1-2 = permanent 3.3 V (inputs can wake the node), 2-3 = switched 3.3 V (0 uA in deep sleep)
P("J18", conn(6), "Inputs", TB.format(n=6), {"1": "GND", "2": "AI2", "3": "AI1", "4": "DIN3", "5": "DIN2", "6": "DIN1"}, None, "Inputs",
  "read left->right on the board: IN1 | IN2 | IN3 | AI1 | AI2 | GND (terminal rotated 180 deg)", lcsc_key="TB6")
# pull-up diodes next to the jumpers (70 V Schottky, common anode on DIN_VCC): block external voltages from DIN_VCC
P("D24", ("Diode", "BAT54A"), "BAS70-06", SOT23, {"1": "DIN1_PU", "2": "DIN2_PU", "3": "DIN_VCC"}, None, "Inputs",
  "pull-up diodes IN1 / IN2 (70 V Schottky, common anode)", lcsc_key="BAS70-06")
P("D25", ("Diode", "BAT54A"), "BAS70-06", SOT23, {"1": "DIN3_PU", "2": None, "3": "DIN_VCC"}, None, "Inputs",
  "pull-up diode IN3 (70 V Schottky, common anode, 2nd diode unused)", lcsc_key="BAS70-06")
for i, (tvs, rpu, rs, c, dcl, jp) in (("1", ("D22", "R36", "R38", "C26", "D26", "JP13")), ("2", ("D23", "R37", "R39", "C27", "D27", "JP14")),
                                      ("3", ("D28", "R44", "R45", "C30", "D29", "JP15"))):
    P(tvs, ("Device", "D_TVS"), "SMAJ33CA", SMA, {"1": f"DIN{i}", "2": "GND"}, None, "Inputs", f"TVS input {i}, bidirectional (-33..+33 V)")
    P(jp, conn(3), f"IN{i} pull: 1-2 up, 2-3 down", HDR.format(n=3), {"1": f"DIN{i}_PU", "2": f"DIN{i}_R", "3": "GND"}, None, "Inputs",
      f"input {i}: 1-2 = pull-up, 2-3 = pull-down (PNP), open = none", lcsc_key="HDR3")
    P(rpu, ("Device", "R"), "100k", R0805, {"1": f"DIN{i}_R", "2": f"DIN{i}"}, None, "Inputs",
      f"pull-up / pull-down input {i} via JP (33 uA when closed, 0.33 mA at 33 V as pull-down)")
    P(rs, ("Device", "R"), "47k", R0805, {"1": f"DIN{i}", "2": f"DIN{i}_B"}, None, "Inputs", f"series resistor input {i} (24 V -> < 0.5 mA)")
    P(c, ("Device", "C"), "100nF", C0805, {"1": f"DIN{i}_B", "2": "GND"}, None, "Inputs", f"debounce input {i} (tau ~5 ms)")
    P(dcl, ("Diode", "BZX84Cxx"), "BZX84C5V6", SOT23, {"1": "GND", "2": None, "3": f"DIN{i}_B"}, None, "Inputs",
      f"clamp input {i}: 5.6 V zener to GND (74LVC inputs 6.5 V max)")
P("U9", ("74xGxx", "74LVC2G14"), "SN74LVC2G14DBVR", "Package_TO_SOT_SMD:SOT-23-6",
  {"1": "DIN1_B", "6": "DIN1_OUT", "3": "DIN2_B", "4": "DIN2_OUT", "5": "DIN_VCC", "2": "GND"}, None, "Inputs",
  "dual Schmitt trigger: IN1 -> GPIO3, IN2 -> GPIO4 (inverted: input high = GPIO low)", lcsc_key="LVC2G14")
PARTS[-1]["units"] = 3
P("U11", ("74xGxx", "74LVC1G14"), "SN74LVC1G14DBVR", "Package_TO_SOT_SMD:SOT-23-5",
  {"1": None, "2": "DIN3_B", "4": "DIN3_OUT", "5": "DIN_VCC", "3": "GND"}, None, "Inputs",
  "Schmitt trigger: IN3 -> GPIO7 (inverted)", lcsc_key="LVC1G14")
P("C28", ("Device", "C"), "100nF", C0805, {"1": "DIN_VCC", "2": "GND"}, None, "Inputs", "decoupling U9 / U11")
P("JP9", conn(3), "IN supply: 1-2 3V3, 2-3 3V3 switched", HDR.format(n=3), {"1": "+3V3", "2": "DIN_VCC", "3": "+3V3_SWB"}, None, "Inputs",
  "1-2 = inputs powered in deep sleep (wake-up), 2-3 = off in deep sleep (0 uA)", lcsc_key="HDR3")

# --- 2 analog inputs 0-10 V (J18 AI1/AI2) -> ADS1115 AIN2/AIN3: TVS, 100k/47k divider (10 V -> 3.2 V), RC, clamp
for i, (tvs, rt, rb, c, dcl) in (("1", ("D31", "R50", "R51", "C33", "D33")), ("2", ("D32", "R52", "R53", "C34", "D34"))):
    P(tvs, ("Device", "D_TVS"), "SMAJ33CA", SMA, {"1": f"AI{i}", "2": "GND"}, None, "Analog", f"TVS analog input {i}")
    P(rt, ("Device", "R"), "100k", R0805, {"1": f"AI{i}", "2": f"AI{i}_ADC"}, None, "Analog", f"divider top AI{i} (0-10 V -> 0-3.2 V)")
    P(rb, ("Device", "R"), "47k", R0805, {"1": f"AI{i}_ADC", "2": "GND"}, None, "Analog", f"divider bottom AI{i}")
    P(c, ("Device", "C"), "100nF", C0805, {"1": f"AI{i}_ADC", "2": "GND"}, None, "Analog", f"RC filter AI{i}")
    P(dcl, ("Diode", "BAT54S"), "BAT54S", SOT23, {"1": "GND", "2": "+3V3_SWB", "3": f"AI{i}_ADC"}, None, "Analog", f"clamp AI{i} to GND / 3V3")

# --- ADS1115 (I2C 0x48, on the switched 3.3 V): AIN0 VIN, AIN1 4-20 mA shunt, AIN2/3 0-10 V inputs
P("U10", ("Analog_ADC", "ADS1115IDGS"), "ADS1115IDGSR", "Package_SO:TSSOP-10_3x3mm_P0.5mm",
  {"1": "GND", "2": None, "3": "GND", "4": "VIN_ADC", "5": "LEVEL_ADC", "6": "AI1_ADC", "7": "AI2_ADC", "8": "+3V3_SWB", "9": "SDA", "10": "SCL"},
  None, "Analog", "16-bit ADC, I2C address 0x48 (ADDR = GND)", lcsc_key="ADS1115")
P("C31", ("Device", "C"), "100nF", C0805, {"1": "+3V3_SWB", "2": "GND"}, None, "Analog", "decoupling U10")
# VIN measurement: divider switched on together with the 3.3 V sensor rail (no current in deep sleep)
P("Q8", ("Transistor_FET", "AO3401A"), "SI2309CDS", SOT23, {"1": "VM_GATE", "2": "+VIN", "3": "VIN_SW"}, None, "Analog",
  "P-MOSFET -60 V: switches the VIN divider", lcsc_key="SI2309")
P("R46", ("Device", "R"), "100k", R0805, {"1": "+VIN", "2": "VM_GATE"}, None, "Analog", "keeps Q8 off")
P("D30", ("Device", "D_Zener"), "BZT52C12", "Diode_SMD:D_SOD-123", {"1": "+VIN", "2": "VM_GATE"}, None, "Analog", "gate-source clamp 12 V (VIN up to 33 V)")
P("R47", ("Device", "R"), "47k", R0805, {"1": "VM_GATE", "2": "VM_DRV"}, None, "Analog", "gate drive")
P("Q9", ("Transistor_FET", "2N7002"), "2N7002", SOT23, {"1": "+3V3_SWB", "2": "GND", "3": "VM_DRV"}, None, "Analog", "on while +3V3_SW is on; 60 V (drain sees VIN through R46/R47)", lcsc_key="2N7002")
P("R48", ("Device", "R"), "100k", R0805, {"1": "VIN_SW", "2": "VIN_ADC"}, None, "Analog", "VIN divider top (1:11, 33 V -> 3.0 V)")
P("R49", ("Device", "R"), "10k", R0805, {"1": "VIN_ADC", "2": "GND"}, None, "Analog", "VIN divider bottom")
P("C32", ("Device", "C"), "100nF", C0805, {"1": "VIN_ADC", "2": "GND"}, None, "Analog", "RC filter VIN")

# ---------------- hardware revision ID: divider on GPIO20 (ADC2), fed from the switched 3.3 V
# prototype (Fritzing): no divider (reads ~0 V with internal pull-down), v1.0: 100k/100k -> 1/2 * 3.3 V
P("R28", ("Device", "R"), "100k", R0805, {"1": "+3V3_SW", "2": "HW_ID"}, None, "MCU", "HW revision ID (top), v1.0: 100k")
P("R29", ("Device", "R"), "100k", R0805, {"1": "HW_ID", "2": "GND"}, None, "MCU", "HW revision ID (bottom), v1.0: 100k -> 0.50 * 3V3")
# strapping pins used as I/O: must stay LOW at boot
P("R56", ("Device", "R"), "100k", R0805, {"1": "GPIO46", "2": "GND"}, None, "MCU", "GPIO46 (strapping pin, J16) LOW at boot")
P("R57", ("Device", "R"), "100k", R0805, {"1": "GPIO45", "2": "GND"}, None, "MCU", "GPIO45 (strapping pin, DALY S1) LOW at boot")

# ---------------- test points and expansion header
# test points: single solder pads (1.0 mm hole, takes a header pin or a probe) next to each rail
TPFP = "TestPoint:TestPoint_THTPad_D2.0mm_Drill1.0mm"
for ref, net, lab in (("TP1", "GND", "GND"), ("TP2", "+VIN", "VIN"), ("TP3", "+5V", "5V"), ("TP4", "+3V3", "3V3"),
                      ("TP5", "+3V3_SW", "3V3S"), ("TP6", "+24V_LOOP", "22V")):
    P(ref, ("Connector", "TestPoint"), lab, TPFP, {"1": net}, None, "Power", "test point (not populated)", lcsc_key="-")
# bring-up probe pads (bare SMD copper, no part)
for ref, net, lab in (("TP7", "+VIN_LOAD", "VLD"), ("TP8", "+3V3_SWB", "3V3B")):
    P(ref, ("Connector", "TestPoint"), lab, "TestPoint:TestPoint_Pad_D1.5mm", {"1": net}, None, "Power",
      "probe pad (not populated)", lcsc_key="-")
# VIN at the end with GND as its only neighbour (a slipped probe / jumper cannot put VIN onto I2C or 5V);
# 5V between two GND pins; pins 4-7 = GND | 3V3S | SDA | SCL fit common 4-pin I2C modules
P("J16", conn(8), "Expansion", HDR.format(n=8),
  {"1": "+VIN", "2": "GND", "3": "+5V", "4": "GND", "5": "+3V3_SWB", "6": "SDA", "7": "SCL", "8": "GPIO46"},
  None, "MCU", "I2C (switched 3.3 V domain) + GPIO46, 3.3 V logic, unprotected", lcsc_key="HDR8")

# ---------------- Switched 3.3 V rail (GPIO38, LOW = on) ---------------------
# two parallel switches, both on GPIO38: +3V3_SW (1-Wire, pulse inputs, HW-ID, 5 V sensor rail) and
# +3V3_SWB (I2C domain, ADS1115, digital inputs, isolators) - keeps both nets short on the board
P("Q1", ("Transistor_FET", "AO3401A"), "AO3401A", SOT23, {"1": "GPIO38", "2": "+3V3", "3": "+3V3_SW"}, "LP0701N3-G", "Switched rails")
P("Q10", ("Transistor_FET", "AO3401A"), "AO3401A", SOT23, {"1": "GPIO38", "2": "+3V3", "3": "+3V3_SWB"}, None, "Switched rails", "2nd switched 3.3 V rail (I2C / analog / inputs)")
P("C35", ("Device", "C"), "100nF", C0805, {"1": "+3V3_SWB", "2": "GND"}, None, "Switched rails")
P("R18", ("Device", "R"), "100k", R0805, {"1": "+3V3", "2": "GPIO38"}, "R18", "Switched rails")
P("C8", ("Device", "C"), "100nF", C0805, {"1": "+3V3_SWB", "2": "GND"}, "C8", "Switched rails")
P("C10", ("Device", "C"), "100nF", C0805, {"1": "+3V3_SWB", "2": "GND"}, "C10", "Switched rails")

# ---------------- Switched 5 V flow-sensor rail -------------------------------
P("R7", ("Device", "R"), "1k", R0805, {"1": "+3V3_SW", "2": "Q3_GATE"}, "R7", "Switched rails")
P("R16", ("Device", "R"), "10k", R0805, {"1": "Q3_GATE", "2": "GND"}, "R16", "Switched rails")
P("Q3", ("Transistor_FET", "AO3400A"), "AO3400A", SOT23, {"1": "Q3_GATE", "2": "GND", "3": "Q2_GATE"}, "TN2106N3-G@1215", "Switched rails")
P("R19", ("Device", "R"), "100k", R0805, {"1": "+5V", "2": "Q2_GATE"}, "R19", "Switched rails")
P("Q2", ("Transistor_FET", "AO3401A"), "AO3401A", SOT23, {"1": "Q2_GATE", "2": "+5V", "3": "+5V_FLOW"}, "LP0701N3", "Switched rails")

# ---------------- Flow / pulse inputs (2 x open collector) via Schmitt trigger ----
# OC1/OC2 tolerate the full VIN (-0.7 ... +33 V): open collector / contacts to GND, 3.3 / 5 V logic,
# sensors with an internal 5 V pull-up and 12 / 24 V push-pull pulse outputs.
# Chain per channel: 36 V TVS (D14) at the terminal; pull-up PULSE_VCC -> Schottky (D40, 70 V) -> 10k -> terminal;
# 10k series -> 5.6 V zener to GND (no clamp into a rail: 33 V would back-feed 3 mA) -> RC 4.7k/47nF (JP11/JP12) -> Schmitt
# JP16 (2x3, one column per channel): top pair = 10k pull-up (OC / Hall), bottom pair = 10k pull-down (PNP), open = none (push-pull); 1206: 33 V -> 109 mW
# PULSE_VCC (JP10): 1-2 = permanent 3.3 V (stage powered in deep sleep; the firmware counts only while awake),
# 2-3 = switched 3.3 V (0 uA in deep sleep). The sensor supply J4 +5V is always switched.
P("J4", conn(4), "Pulse inputs", TB.format(n=4), {"1": "FLOW_OC", "2": "FLOW2_OC", "3": "FLOW_VCC", "4": "GND"}, None, "Flow",
  "1 OC1 (flow sensor), 2 OC2 (2nd pulse input), 3 +5 V, 4 GND; OC1/OC2 up to VIN (33 V)", lcsc_key="TB4")
P("F5", ("Device", "Polyfuse"), "PTC 0.1A", F1206, {"1": "+5V_FLOW", "2": "FLOW_VCC_F"}, "F5", "Flow")
P("R1", ("Device", "R"), "10", R0805, {"1": "FLOW_VCC_F", "2": "FLOW_VCC"}, "R1", "Flow")
P("C13", ("Device", "C"), "100nF", C0805, {"1": "FLOW_VCC", "2": "GND"}, "C13", "Flow")
P("D7", ("Device", "D_Zener"), "SMBJ6.0A", SMB, {"1": "FLOW_VCC", "2": "GND"}, "SMBJ5.0A@362", "Flow",
  "TVS 6V, unidirectional (5.0A would leak at 5 V)", lcsc_key="SMBJ6.0A")
P("D14", ("Device", "D_TVS_Dual_AAC"), "PESD36VS2UT", SOT23, {"1": "FLOW_OC", "2": "FLOW2_OC", "3": "GND"}, None, "Flow",
  "TVS 36 V at both pulse inputs (pin 3 common anode)", lcsc_key="PESD36")
P("D40", ("Diode", "BAT54A"), "BAS70-06", SOT23, {"1": "FLOW_PU", "2": "FLOW2_PU", "3": "PULSE_VCC"}, None, "Flow",
  "pull-up diodes OC1 / OC2 (70 V Schottky, common anode): block external voltages from PULSE_VCC", lcsc_key="BAS70-06")
P("JP16", ("Connector_Generic", "Conn_02x03_Odd_Even"), "OC pull: 1-3 up, 3-5 down", "Connector_PinHeader_2.54mm:PinHeader_2x03_P2.54mm_Vertical",
  {"1": "FLOW_PU", "3": "FLOW_R", "5": "GND", "2": "FLOW2_PU", "4": "FLOW2_R", "6": "GND"}, None, "Flow",
  "pull per channel (left column OC1, right OC2): top pair = pull-up, bottom pair = pull-down (PNP), open = none", lcsc_key="HDR2X3")
for ch, oc, (rs, rpu, dcl, rf, cf, jp) in (("1", "FLOW", ("R6", "R14", "D3", "R15", "C7", "JP11")), ("2", "FLOW2", ("R30", "R31", "D19", "R32", "C24", "JP12"))):
    P(rs, ("Device", "R"), "10k", R0805, {"1": f"{oc}_OC", "2": f"{oc}_A"}, None, "Flow", f"series resistor ch{ch} (33 V -> 2.7 mA into the zener)")
    P(rpu, ("Device", "R"), "10k", R1206, {"1": f"{oc}_R", "2": f"{oc}_OC"}, None, "Flow",
      f"OC pull-up / pull-down ch{ch} at the terminal via JP (0.3 mA OC on; 1206: 109 mW at 33 V as pull-down)", lcsc_key="10k1206")
    P(dcl, ("Diode", "BZX84Cxx"), "BZX84C5V6", SOT23, {"1": "GND", "2": None, "3": f"{oc}_A"}, None, "Flow",
      f"clamp ch{ch}: 5.6 V zener to GND (74LVC inputs 6.5 V max)")
    P(rf, ("Device", "R"), "4.7k", R0805, {"1": f"{oc}_A", "2": f"{oc}_B"}, None, "Flow",
      f"RC low-pass ch{ch} (47 nF: tau 1.2 ms rising through pull-up + series R, 0.7 ms falling)")
    P(cf, ("Device", "C"), "47nF", C0805, {"1": f"{oc}_B", "2": f"{oc}_CF"}, None, "Flow", f"RC low-pass ch{ch}")
    P(jp, conn(2), f"Filter ch{ch}", HDR.format(n=2), {"1": f"{oc}_CF", "2": "GND"}, None, "Flow",
      f"jumper on = low-pass ch{ch} active (flow sensors), off = fast pulses", lcsc_key="HDR2")
P("U2", ("74xGxx", "74LVC2G14"), "SN74LVC2G14DBVR", "Package_TO_SOT_SMD:SOT-23-6",
  {"1": "FLOW_B", "6": "FLOW_OUT", "3": "FLOW2_B", "4": "FLOW2_OUT", "5": "PULSE_VCC", "2": "GND"}, None, "Flow",
  "dual Schmitt-trigger inverter: ch1 -> GPIO5, ch2 -> GPIO6 (output inverted: pulse = HIGH)", lcsc_key="LVC2G14")
PARTS[-1]["units"] = 3
P("C25", ("Device", "C"), "100nF", C0805, {"1": "PULSE_VCC", "2": "GND"}, None, "Flow", "decoupling U2")
P("JP10", conn(3), "PULSE supply: 1-2 3V3, 2-3 3V3 switched", HDR.format(n=3), {"1": "+3V3", "2": "PULSE_VCC", "3": "+3V3_SW"}, None, "Flow",
  "1-2 = pulse stage powered in deep sleep (no deep-sleep counter in the firmware yet), 2-3 = off in deep sleep (0 uA)", lcsc_key="HDR3")

# ---------------- 24 V boost supply switch: +5V -> MT3608 (GPIO47, HIGH = on) -------
# fed from the 5 V rail, so the 4-20 mA loop works with any VIN (6-33 V) and on USB power
P("Q5", ("Transistor_FET", "AO3401A"), "AO3401A", SOT23,
  {"1": "HS_GATE", "2": "+5V", "3": "BOOST_IN"}, None, "High-side", "P-MOSFET: switches the 24 V boost input")
P("R17", ("Device", "R"), "10k", R0805, {"1": "+5V", "2": "HS_GATE"}, "R17", "High-side", "gate pull-up; with C36 sets the turn-off ramp (~30 ms)")
# soft start: Miller capacitor gate-drain. The gate sits at the plateau (~3.2 V) while
# ~0.5 mA flows through R12, so the boost input rises at 0.5 mA / 1 uF = 0.5 V/ms (~10 ms to 5 V):
# ~13 mA charging current for the 25 uF behind Q5 instead of an amp-level dump from the 5 V rail
P("C36", ("Device", "C"), "1uF", C0805, {"1": "BOOST_IN", "2": "HS_GATE"}, None, "High-side", "Q5 soft start (gate-drain)")
P("R12", ("Device", "R"), "4.7k", R0805, {"1": "HS_GATE", "2": "HS_DRV"}, "R12", "High-side")
P("Q4", ("Transistor_FET", "AO3400A"), "AO3400A", SOT23, {"1": "GPIO47", "2": "GND", "3": "HS_DRV"}, "TN2106N3-G@606", "High-side")
P("R13", ("Device", "R"), "10k", R0805, {"1": "GPIO47", "2": "GND"}, "R13", "High-side")

# 5 V -> 24 V boost for the 4-20 mA loop (only powered while Q5 is on)
P("U7", ("Regulator_Switching", "MT3608"), "MT3608", "Package_TO_SOT_SMD:SOT-23-6",
  {"1": "BOOST_SW", "2": "GND", "3": "BOOST_FB", "4": "BOOST_IN", "5": "BOOST_IN", "6": None}, None, "High-side", "Vout 22.2 V (max 28 V)", lcsc_key="MT3608")
P("L2", ("Device", "L"), "22uH", "Inductor_SMD:L_Sunlord_SWPA4030S", {"1": "BOOST_IN", "2": "BOOST_SW"}, None, "High-side", "Sunlord SWPA4030S220MT", lcsc_key="L22u")
P("D13", ("Diode", "SS34"), "SS210", "Diode_SMD:D_SMA", {"1": "24V_BOOST", "2": "BOOST_SW"}, None, "High-side",
  "100 V: margin above the MT3608 overvoltage", lcsc_key="SS210")
P("C22", ("Device", "C"), "10uF 50V", C1210, {"1": "BOOST_IN", "2": "GND"}, None, "High-side", "boost input (5 V)")
P("C23", ("Device", "C"), "10uF 50V", C1210, {"1": "24V_BOOST", "2": "GND"}, None, "High-side")
P("R24", ("Device", "R"), "360k", R0805, {"1": "24V_BOOST", "2": "BOOST_FB"}, None, "High-side",
  "Vout = 0.6 V * (1 + 360k/10k) = 22.2 V (below the SMBJ24A standoff with FB tolerance)")
P("R25", ("Device", "R"), "10k", R0805, {"1": "BOOST_FB", "2": "GND"}, None, "High-side")

# ---------------- 4-20 mA level sensor ----------------------------------------
P("F3", ("Device", "Polyfuse"), "PTC 0.05A", F1206, {"1": "24V_BOOST", "2": "+24V_LOOP"}, "F3", "Level")
P("C3", ("Device", "C"), "10uF 50V", C1210, {"1": "+24V_LOOP", "2": "GND"}, "C3", "Level")
P("C6", ("Device", "C"), "100nF", C0805, {"1": "+24V_LOOP", "2": "GND"}, "C6", "Level")
P("D8", ("Device", "D_Zener"), "SMBJ24A", SMB, {"1": "+24V_LOOP", "2": "GND"}, "SMBJ24A@3462", "Level", "TVS 24V, unidirectional")
P("J7", conn(2), "Level 4-20mA", TB.format(n=2), {"1": "+24V_LOOP", "2": "LEVEL_SHUNT"}, "Level", "Level", "1 loop +, 2 loop - (to shunt)", lcsc_key="TB2")
P("R4", ("Device", "R"), "120 0.1%", R1206, {"1": "LEVEL_SHUNT", "2": "GND"}, "R4", "Level", "4-20 mA shunt, 0.1% thin film", lcsc_key="R120P1")
P("D9", ("Device", "D_Zener"), "SMBJ5.0A", SMB, {"1": "LEVEL_SHUNT", "2": "GND"}, "SMBJ24A@3648", "Level", "TVS 5V, unidirectional (crowbar for F3)")
P("R8", ("Device", "R"), "1k", R0805, {"1": "LEVEL_SHUNT", "2": "LEVEL_ADC"}, "R8", "Level",
  "1k: clamp leakage and ADS1115 input current stay below 0.05 % of 20 mA (10k: up to ~0.5 %)")
P("C12", ("Device", "C"), "100nF", C0805, {"1": "LEVEL_ADC", "2": "GND"}, "C12", "Level")
P("D4", ("Diode", "BAT54S"), "BAT54S", SOT23, {"1": "GND", "2": "+3V3_SW", "3": "LEVEL_ADC"}, "D5+D6", "Level", "clamp ADS1115 AIN1 to GND / 3V3")

# ---------------- 1-Wire (DS18B20) --------------------------------------------
P("J8", conn(3), "1-Wire", TB.format(n=3), {"1": "GND", "2": "ONEWIRE_VCC", "3": "ONEWIRE_DQ"}, "1Wire", "1-Wire", lcsc_key="TB3")
P("F4", ("Device", "Polyfuse"), "PTC 0.05A", F1206, {"1": "+3V3_SW", "2": "ONEWIRE_VCC_F"}, "F4", "1-Wire")
P("R2", ("Device", "R"), "10", R0805, {"1": "ONEWIRE_VCC_F", "2": "ONEWIRE_VCC"}, "R2", "1-Wire")
P("C5", ("Device", "C"), "100nF", C0805, {"1": "ONEWIRE_VCC", "2": "GND"}, "C5", "1-Wire")
P("D10", ("Device", "D_Zener"), "SMLVT3V3", SMB, {"1": "ONEWIRE_VCC", "2": "GND"}, "SMLVT3V3", "1-Wire", "TVS 3.3V, unidirectional")
P("R9", ("Device", "R"), "2.2k", R0805, {"1": "ONEWIRE_VCC", "2": "ONEWIRE_IO"}, "R9", "1-Wire")
P("D15", ("Device", "D_TVS_Dual_AAC"), "PESD5V0S2BT", SOT23, {"1": "ONEWIRE_DQ", "2": None, "3": "GND"}, None, "1-Wire", "ESD/surge at the 1-Wire data input", lcsc_key="PESD")
P("R3", ("Device", "R"), "33", R0805, {"1": "ONEWIRE_IO", "2": "ONEWIRE_DQ"}, "R3", "1-Wire")
P("D5", ("Diode", "BAT54S"), "BAT54S", SOT23, {"1": "GND", "2": "+3V3", "3": "ONEWIRE_IO"}, "D7+D8", "1-Wire", "replaces BAT43 D7/D8")

# ---------------- I2C (BME280, ADS1115, J16): whole bus on the switched 3.3 V ---------
P("J11", conn(4), "BME280", HDR.format(n=4), {"1": "+3V3_SWB", "2": "GND", "3": "SCL", "4": "SDA"}, "BME280", "I2C", lcsc_key="HDR4")
P("R54", ("Device", "R"), "10k", R0805, {"1": "+3V3_SWB", "2": "SCL"}, None, "I2C", "SCL pull-up (on-board)")
P("R55", ("Device", "R"), "10k", R0805, {"1": "+3V3_SWB", "2": "SDA"}, None, "I2C", "SDA pull-up (on-board)")
# two separate 2-pin headers with one free 2.54 mm position between them: a shunt cannot bridge SCL and SDA
P("JP1", conn(2), "I2C pull-up SCL", HDR.format(n=2), {"1": "SCL_PU", "2": "SCL"}, None, "I2C",
  "jumper on = extra 4.7k on SCL (long BME280 cables)", lcsc_key="HDR2")
P("JP2", conn(2), "I2C pull-up SDA", HDR.format(n=2), {"1": "SDA_PU", "2": "SDA"}, None, "I2C",
  "jumper on = extra 4.7k on SDA (long BME280 cables)", lcsc_key="HDR2")
P("R10", ("Device", "R"), "4.7k", R0805, {"1": "+3V3_SWB", "2": "SCL_PU"}, "R10", "I2C")
P("R11", ("Device", "R"), "4.7k", R0805, {"1": "+3V3_SWB", "2": "SDA_PU"}, "R11", "I2C")

# ---------------- Victron VE.Direct (isolated) ---------------------------------
P("U3", ("Isolator", "ADuM121N"), "ADuM121N1BRZ", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
  {"1": "+3V3_SWB", "2": "VE_ESP_RX", "3": "VE_ESP_TX", "4": "GND", "5": "GND_VICTRON", "6": "VE_RX", "7": "VE_TX", "8": "VDD_VICTRON"},
  "ADuM121N1@2859", "Victron", "alt. ISO7721DR (not F)", lcsc_key="ADUM121N")
P("C9", ("Device", "C"), "100nF", C0805, {"1": "VDD_VICTRON", "2": "GND_VICTRON"}, "C9", "Victron")
P("J9", conn(4), "VE.Direct", TB.format(n=4), {"1": "GND_VICTRON", "2": "VE_RX", "3": "VE_TX", "4": "VDD_VICTRON"}, "VE.Direct", "Victron",
  "1 GND, 2 RX (to Victron), 3 TX (from Victron), 4 +V", lcsc_key="TB4")

P("D16", ("Device", "D_TVS_Dual_AAC"), "PESD5V0S2BT", SOT23, {"1": "VE_RX", "2": "VE_TX", "3": "GND_VICTRON"}, None, "Victron", "ESD at the VE.Direct data lines", lcsc_key="PESD")
P("J12", conn(4), "VE.Direct (JST-PH)", "Connector_JST:JST_PH_B4B-PH-K_1x04_P2.00mm_Vertical",
  {"1": "GND_VICTRON", "2": "VE_TX", "3": "VE_RX", "4": "VDD_VICTRON"}, None, "Victron",
  "device-side VE.Direct pinout for the original (crossed) Victron cable", lcsc_key="PH4")

# ---------------- DALY Smart BMS (isolated) -----------------------------------
P("U4", ("Isolator", "ADuM121N"), "ADuM121N1BRZ", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
  {"1": "+3V3_SWB", "2": "DALY_ESP_RX", "3": "DALY_ESP_TX", "4": "GND", "5": "GND_DALY", "6": "DALY_RX", "7": "DALY_TX", "8": "VDD_DALY"},
  "ADuM121N1@2665", "DALY", "alt. ISO7721DR (not F)", lcsc_key="ADUM121N")
P("C11", ("Device", "C"), "100nF", C0805, {"1": "VDD_DALY", "2": "GND_DALY"}, "C11", "DALY")
P("J10", conn(6), "DALY BMS", TB.format(n=6), {"1": "GND_DALY", "2": "DALY_VCC", "3": "DALY_VBAT", "4": "DALY_S1", "5": "DALY_TX", "6": "DALY_RX"},
  "DALY Smart BMS", "DALY", "BMS UART order: GND, 3V3, 12V, S1, TX (from BMS), RX (to BMS)", lcsc_key="TB6")
P("D17", ("Device", "D_TVS_Dual_AAC"), "PESD5V0S2BT", SOT23, {"1": "DALY_TX", "2": "DALY_RX", "3": "GND_DALY"}, None, "DALY", "ESD at the DALY UART data lines", lcsc_key="PESD")
P("J13", conn(6), "DALY UART (PH2.0)", "Connector_JST:JST_PH_B6B-PH-K_1x06_P2.00mm_Vertical",
  {"1": "GND_DALY", "2": "DALY_VCC", "3": "DALY_VBAT", "4": "DALY_S1", "5": "DALY_P5", "6": "DALY_P6"}, None, "DALY",
  "for the original DALY UART cable; pins 5/6 set by JP6/JP7", lcsc_key="PH6")
# J13 pins 5/6: real 3-pin jumpers (jumper cap 1-2 = 1:1 cable, 2-3 = crossed cable)
P("JP6", conn(3), "J13.5: 1-2 TX (1:1 cable), 2-3 RX (crossed)", HDR.format(n=3),
  {"1": "DALY_TX", "2": "DALY_P5", "3": "DALY_RX"}, None, "DALY", lcsc_key="HDR3")
P("JP7", conn(3), "J13.6: 1-2 RX (1:1 cable), 2-3 TX (crossed)", HDR.format(n=3),
  {"1": "DALY_RX", "2": "DALY_P6", "3": "DALY_TX"}, None, "DALY", lcsc_key="HDR3")
P("JP3", conn(3), "DALY VDD: 1-2 BMS, 2-3 78L33", HDR.format(n=3), {"1": "DALY_VCC", "2": "VDD_DALY", "3": "REG_OUT"}, "3.3 or 78Lxx", "DALY", lcsc_key="HDR3")
P("JP4", conn(2), "78L33 on/off", HDR.format(n=2), {"1": "DALY_VBAT", "2": "REG_IN"}, "on/off", "DALY", lcsc_key="HDR2")
P("U1", ("Regulator_Linear", "L78L33_SOT89"), "78L33", "Package_TO_SOT_SMD:SOT-89-3",
  {"1": "REG_OUT", "2": "GND_DALY", "3": "REG_IN"}, "78Lxx", "DALY",
  "ST L78L33ACUTR, SOT-89 (1 OUT, 2 GND, 3 IN): 3.3 V like the DALY UART; input max 30 V -> JP4 only with a 4s BMS")
P("C4", ("Device", "C"), "10uF 50V", C1210, {"1": "REG_IN", "2": "GND_DALY"}, "C4", "DALY")
P("JP5", conn(2), "DALY wakeup enable", HDR.format(n=2), {"1": "PC817_LED", "2": "GPIO45"}, "GPIO45", "DALY", lcsc_key="HDR2")
P("R5", ("Device", "R"), "220", R0805, {"1": "PC817_LED", "2": "PC817_A"}, "R5", "DALY")
P("U5", ("Isolator", "EL817"), "PC817 SMD", "Package_DIP:SMDIP-4_W9.53mm",
  {"1": "PC817_A", "2": "GND", "3": "GND_DALY", "4": "DALY_S1"}, "PC817", "DALY", lcsc_key="PC817")

MOUNTING_HOLES_IN = [(0.1623, 0.1335), (3.5623, 0.7335), (3.8623, 1.9335)]  # from rev10 drill file, inch, y up
BOARD_MIL = (3937.0, 2362.2)  # prototype board, 100 x 60 mm (v1.0: 100 x 100 mm, see layout_v6.py)

BLOCKS = ["MCU", "Power", "Switched rails", "Flow", "High-side", "Level", "1-Wire", "I2C", "Outputs", "Load eFuse", "Inputs", "Analog", "Victron", "DALY"]
