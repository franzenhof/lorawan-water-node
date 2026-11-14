/**
 * ChirpStack Payload Decoder – LoRaWAN Water Node (Heltec WiFi LoRa V3)
 *
 * Compatible with ChirpStack v3 (Decode function) and v4 (decodeUplink function).
 * Payload format: CayenneLPP (custom channel mapping, see below).
 *
 * ── Port 1: Normal measurement cycle ─────────────────────────────────────────
 *   Ch  1  Analog Input     → 4-20 mA current of level sensor (mA, optional)
 *   Ch  2  Analog Input     → Flow rate (open-collector flow sensor, L/min, optional)
 *   Ch  3  Temperature      → DS18B20 #1 – spring water (°C, optional)
 *   Ch  4  Temperature      → DS18B20 #2 – tank floor (°C, optional)
 *   Ch  5  Temperature      → DS18B20 #3 – air above tank (°C, optional)
 *   Ch  6  Temperature      → BME280 enclosure temperature (°C, optional)
 *   Ch  7  Rel. Humidity    → BME280 relative humidity (%, optional)
 *   Ch  8  Baro. Pressure   → BME280 pressure QNH (hPa, optional)
 *   Ch  9  Analog Input     → VIN supply voltage (V, ADS1115) OR DALY status flag (0/1)
 *   Ch 10  Analog Input     → Victron MPPT battery voltage (V, optional)
 *   Ch 11  Power            → Victron MPPT PV power (W, uint16, optional)
 *   Ch 12  Analog Input     → Victron charge current (A, optional)
 *   Ch 13  Analog Input     → Victron charge state CS (0=Off, 3=Bulk, 4=Abs, 5=Float)
 *   Ch 14  Analog Input     → Victron yield today (kWh, optional)
 *   Ch 15  Analog Input     → DALY BMS pack voltage (V, optional)
 *   Ch 16  Analog Input     → DALY BMS state of charge (%, optional)
 *   Ch 17  Analog Input     → DALY BMS current (+A=charging, −A=discharging, optional)
 *   Ch 18  Temperature      → DALY BMS average battery temperature (°C, optional)
 *   Ch 19  Generic Sensor   → DALY BMS cell voltage spread max−min (mV, uint32, optional)
 *   Ch 20  Digital Input    → Diagnostic bitmask (bits 0–4, see below)
 *   Ch 21  Digital Input    → digital inputs, bit0 IN1, bit1 IN2, bit2 IN3
 *                             (1 = terminal high/open, 0 = pulled to GND)
 *   Ch 22  Digital Output   → switched outputs, bit0 OUT1, bit1 OUT2, bit2 OUT3 (1 = on)
 *   Ch 23  Analog Input     → 2nd flow sensor (L/min, pulse input 2 in flow mode)
 *   Ch 24  Generic Sensor   → pulse counter (pulses since power-on, uint32,
 *                             pulse input 2 in counter mode)
 *   Ch 25  Analog Input     → 0-10 V input AI1 (V)
 *   Ch 26  Analog Input     → 0-10 V input AI2 (V)
 *   Ch200  Digital Input    → Debug cycle counter (0-255, debug mode only)
 *   Ch202  Digital Input    → Debug send fail counter (0-255, debug mode only)
 *   Ch203  Generic Sensor   → Debug free heap (KiB, debug mode only)
 *   Ch204  Generic Sensor   → Debug uptime (0.1 h, debug mode only)
 *   Ch205  Analog Input     → Debug downlink RSSI (dBm, debug mode only)
 *   Ch206  Analog Input     → Debug downlink SNR (dB, debug mode only)
 *   Ch207  Analog Input     → Debug LinkCheck margin (dB, debug mode only)
 *   Ch208  Digital Input    → Debug LinkCheck gateway count (debug mode only)
 *
 * Diagnostic byte (Ch 20 Digital Input):
 *   Bit 0 : Victron no frame received      (1=error)
 *   Bit 1 : DALY BMS no response           (1=error)
 *   Bit 2 : DS18B20 read error             (1=at least one sensor failed)
 *   Bit 3 : 4-20 mA outside 3.5–20.5 mA  (1=error)
 *   Bit 4 : Last reset was WDT             (1=watchdog reset occurred)
 *
 * Victron charge state (CS value, Ch 13):
 *   0 = Off / Not charging
 *   2 = Fault
 *   3 = Bulk
 *   4 = Absorption
 *   5 = Float
 *   7 = Equalize (manual)
 *
 * ── Port 2: Daily uplink (after 24 h of real time) ───────────────────────────
 *   Ch  1  Generic Sensor   → Total volume (0.1 L, uint32). In deep-sleep mode
 *                             this is extrapolated from the sampling windows
 *                             (the flow sensor is unpowered while sleeping).
 *                             Older firmware sent Analog Input (L, max 327.67).
 *   Ch  2  Analog Input     → Maximum flow rate today (L/min)
 *   Ch  3  Temperature      → Minimum temperature DS18B20 #1 (°C, if any reading)
 *   Ch  4  Temperature      → Maximum temperature DS18B20 #1 (°C, if any reading)
 *
 * ── Port 3: Compact binary telemetry (optional, selectable in WiFi portal) ──
 *   Byte0=version(0x04; 0x02/0x03 from older firmware still decoded),
 *   Byte1..5=40-bit bitmask (0x02/0x03: Byte1..4, 32 bits), then the data
 *   Carries the same values as port 1 but ~50% smaller (no per-field overhead).
 *
 * Ranges: Analog Input is int16 x100, i.e. ±327.67. Fields that can exceed
 * that (PV power, cell diff, heap, uptime, daily volume) use wider types
 * since firmware with binary protocol 0x03; older firmware wrapped them.
 */

// ── CayenneLPP type constants ──────────────────────────────────────────────
var LPP_DIGITAL_INPUT  = 0;    // 1 Byte unsigned
var LPP_DIGITAL_OUTPUT = 1;    // 1 Byte unsigned
var LPP_ANALOG_INPUT   = 2;    // 2 Byte signed, ÷100
var LPP_GENERIC_SENSOR = 100;  // 4 Byte unsigned, ×1
var LPP_TEMPERATURE    = 103;  // 2 Byte signed, ÷10
var LPP_HUMIDITY       = 104;  // 1 Byte unsigned, ÷2
var LPP_BAROMETRIC     = 115;  // 2 Byte unsigned, ÷10
var LPP_POWER          = 128;  // 2 Byte unsigned, 1 W

// ── Victron CS code to text ────────────────────────────────────────────────
var VICTRON_CS = {
    0: "Off",
    2: "Fault",
    3: "Bulk",
    4: "Absorption",
    5: "Float",
    7: "Equalize"
};

// ── Helper functions ───────────────────────────────────────────────────────

function uint16BE(bytes, i) {
    return (bytes[i] << 8) | bytes[i + 1];
}

function uint32BE(bytes, i) {
    return ((bytes[i] << 24) >>> 0) + (bytes[i + 1] << 16) + (bytes[i + 2] << 8) + bytes[i + 3];
}

function int16BE(bytes, i) {
    var v = uint16BE(bytes, i);
    return v >= 0x8000 ? v - 0x10000 : v;
}

function round2(v) {
    return Math.round(v * 100) / 100;
}

function round1(v) {
    return Math.round(v * 10) / 10;
}

function decodeInputs(byte) {
    return { din1: (byte & 0x01) !== 0, din2: (byte & 0x02) !== 0, din3: (byte & 0x04) !== 0 };
}

function decodeOutputs(byte) {
    return { out1: (byte & 0x01) !== 0, out2: (byte & 0x02) !== 0, out3: (byte & 0x04) !== 0 };
}

function decodeDiag(byte) {
    return {
        victron_no_frame:    (byte & 0x01) !== 0,
        daly_no_response:    (byte & 0x02) !== 0,
        ds18b20_read_error:  (byte & 0x04) !== 0,
        level_out_of_range:  (byte & 0x08) !== 0,
        last_reset_was_wdt:  (byte & 0x10) !== 0
    };
}

// ── LPP parser for port 1 ─────────────────────────────────────────────────

function parsePort1(bytes) {
    var d = {};
    var i = 0;

    while (i + 1 < bytes.length) {
        var ch   = bytes[i++];
        var type = bytes[i++];

        switch (type) {

            case LPP_ANALOG_INPUT: {
                var val = round2(int16BE(bytes, i) / 100.0);
                i += 2;
                if      (ch === 1)  d.level_sensor_ma       = val;  // 4-20 mA
                else if (ch === 2)  d.flow_l_min            = val;  // L/min
                else if (ch === 9)  d.battery_status        = val;  // V (VIN) or 0/1 (DALY)
                else if (ch === 10) d.victron_battery_v     = val;
                else if (ch === 11) d.victron_pv_w          = val;
                else if (ch === 12) d.victron_charge_a      = val;
                else if (ch === 13) {
                    d.victron_cs        = val;
                    d.victron_cs_text   = VICTRON_CS[Math.round(val)] || "Unknown (" + val + ")";
                }
                else if (ch === 14) d.victron_yield_today_kwh = val;
                else if (ch === 15) d.daly_pack_v           = val;
                else if (ch === 16) d.daly_soc_pct          = val;
                else if (ch === 17) d.daly_current_a        = val;  // + = charging, - = discharging
                else if (ch === 19) d.daly_cell_diff_mv     = val;
                else if (ch === 23) d.flow2_l_min           = val;  // L/min, 2nd flow sensor
                else if (ch === 25) d.ai1_v                 = val;  // V, 0-10 V input AI1
                else if (ch === 26) d.ai2_v                 = val;  // V, 0-10 V input AI2
                else if (ch === 203) d.debug_free_heap_kib    = val;
                else if (ch === 204) d.debug_uptime_h         = val;
                else if (ch === 205) d.debug_dl_rssi_dbm      = val;
                else if (ch === 206) d.debug_dl_snr_db        = val;
                else if (ch === 207) d.debug_link_margin_db   = val;
                break;
            }

            case LPP_TEMPERATURE: {
                var tval = round1(int16BE(bytes, i) / 10.0);
                i += 2;
                if      (ch === 3)  d.temp_source_c         = tval;  // DS18B20 spring water
                else if (ch === 4)  d.temp_tank_floor_c     = tval;  // DS18B20 tank floor
                else if (ch === 5)  d.temp_air_c            = tval;  // DS18B20 air
                else if (ch === 6)  d.bme280_temp_c         = tval;
                else if (ch === 18) d.daly_bat_temp_c       = tval;
                break;
            }

            case LPP_HUMIDITY: {
                d.bme280_humidity_pct = round1(bytes[i++] / 2.0);
                break;
            }

            case LPP_BAROMETRIC: {
                d.bme280_pressure_hpa = round1(uint16BE(bytes, i) / 10.0);
                i += 2;
                break;
            }

            case LPP_POWER: {
                var pval = uint16BE(bytes, i);
                i += 2;
                if (ch === 11) d.victron_pv_w = pval;
                break;
            }

            case LPP_GENERIC_SENSOR: {
                var gval = uint32BE(bytes, i);
                i += 4;
                if      (ch === 19)  d.daly_cell_diff_mv   = gval;
                else if (ch === 203) d.debug_free_heap_kib = gval;
                else if (ch === 204) d.debug_uptime_h      = round1(gval / 10.0);
                else if (ch === 24)  d.pulse_count         = gval;
                break;
            }

            case LPP_DIGITAL_INPUT: {
                var dval = bytes[i++];
                if (ch === 20) d.diag = decodeDiag(dval);
                else if (ch === 21) d.inputs = decodeInputs(dval);
                else if (ch === 200) d.debug_cycle_counter = dval;
                else if (ch === 202) d.debug_send_fail_counter = dval;
                else if (ch === 208) d.debug_link_gw_count = dval;
                break;
            }

            case LPP_DIGITAL_OUTPUT: {
                var oval = bytes[i++];
                if (ch === 22) d.outputs = decodeOutputs(oval);
                break;
            }

            default:
                // Unknown type – stop parsing to avoid offset errors
                console.error("LPP Port1: unknown type 0x" + type.toString(16) +
                              " at offset " + (i - 1));
                i = bytes.length;
                break;
        }
    }
    return d;
}

// ── LPP parser for port 2 (daily uplink) ──────────────────────────────────

function parsePort2(bytes) {
    var d = { _type: "daily_summary" };
    var i = 0;

    while (i + 1 < bytes.length) {
        var ch   = bytes[i++];
        var type = bytes[i++];

        switch (type) {

            case LPP_ANALOG_INPUT: {
                var val = round2(int16BE(bytes, i) / 100.0);
                i += 2;
                if      (ch === 1) d.total_liters_today    = val;
                else if (ch === 2) d.max_flow_l_min        = val;
                break;
            }

            case LPP_GENERIC_SENSOR: {
                var lval = uint32BE(bytes, i);
                i += 4;
                if (ch === 1) d.total_liters_today = round1(lval / 10.0);
                break;
            }

            case LPP_TEMPERATURE: {
                var tval = round1(int16BE(bytes, i) / 10.0);
                i += 2;
                if      (ch === 3) d.min_temp_source_c     = tval;
                else if (ch === 4) d.max_temp_source_c     = tval;
                break;
            }

            default:
                console.error("LPP Port2: unknown type 0x" + type.toString(16) +
                              " at offset " + (i - 1));
                i = bytes.length;
                break;
        }
    }
    return d;
}

// ── Binary parser for port 3 (compact custom protocol) ────────────────────
//   Byte 0    : protocol version (0x04)
//   Byte 1..5 : 40-bit field-presence bitmask, little-endian (0x02/0x03: Byte 1..4)
//   then      : data for each present field, in ascending bit-index order
// Values mirror the CayenneLPP resolution (analog ÷100, temp ÷10, hum ÷2,
// pressure ÷10, CS/diag/counters raw bytes).
// Version 0x03 widened four fields (see BIN_V3_OVERRIDES); 0x02 is still
// decoded for devices running older firmware.
var BIN_PROTO_VERSION = 0x04;
var BIN_SUPPORTED_VERSIONS = [0x02, 0x03, 0x04];

// [bit, output key, encoding (version 0x02)]
var BIN_FIELDS = [
    [0,  "level_sensor_ma",         "i16_100"],
    [1,  "flow_l_min",              "i16_100"],
    [2,  "temp_source_c",           "i16_10"],
    [3,  "temp_tank_floor_c",       "i16_10"],
    [4,  "temp_air_c",              "i16_10"],
    [5,  "bme280_temp_c",           "i16_10"],
    [6,  "bme280_humidity_pct",     "u8_2"],
    [7,  "bme280_pressure_hpa",     "u16_10"],
    [8,  "battery_status",          "i16_100"],
    [9,  "victron_battery_v",       "i16_100"],
    [10, "victron_pv_w",            "i16_100"],
    [11, "victron_charge_a",        "i16_100"],
    [12, "victron_cs",              "cs"],
    [13, "victron_yield_today_kwh", "i16_100"],
    [14, "daly_pack_v",             "i16_100"],
    [15, "daly_soc_pct",            "i16_100"],
    [16, "daly_current_a",          "i16_100"],
    [17, "daly_bat_temp_c",         "i16_10"],
    [18, "daly_cell_diff_mv",       "i16_100"],
    [19, "diag",                    "diag"],
    [20, "debug_cycle_counter",     "u8"],
    [21, "debug_send_fail_counter", "u8"],
    [22, "debug_free_heap_kib",     "i16_100"],
    [23, "debug_uptime_h",          "i16_100"],
    [24, "debug_dl_rssi_dbm",       "i16_100"],
    [25, "debug_dl_snr_db",         "i16_100"],
    [26, "debug_link_margin_db",    "i16_100"],
    [27, "debug_link_gw_count",     "u8"],
    [28, "inputs",                  "din"],
    [29, "outputs",                 "dout"],
    [30, "flow2_l_min",             "i16_100"],
    [31, "pulse_count",             "u32"],
    [32, "ai1_v",                   "i16_100"],
    [33, "ai2_v",                   "i16_100"]
];

// bit -> encoding in version 0x03
var BIN_V3_OVERRIDES = {
    10: "u16_1",   // victron_pv_w (W)
    18: "u16_1",   // daly_cell_diff_mv (mV)
    22: "u16_1",   // debug_free_heap_kib (KiB)
    23: "u16_10"   // debug_uptime_h (0.1 h)
};

function parsePort3(bytes) {
    var d = {};
    if (bytes.length < 5) {
        return { error: "binary payload too short (" + bytes.length + " bytes)" };
    }
    d.proto_version = bytes[0];
    if (BIN_SUPPORTED_VERSIONS.indexOf(bytes[0]) === -1) {
        return {
            error: "unsupported binary protocol version " + bytes[0] +
                   " (expected " + BIN_SUPPORTED_VERSIONS.join(" or ") + ")"
        };
    }
    var v3 = bytes[0] >= 0x03;
    var maskBytes = bytes[0] >= 0x04 ? 5 : 4;
    if (bytes.length < 1 + maskBytes) {
        return { error: "binary payload too short (" + bytes.length + " bytes)" };
    }
    function hasBit(bit) {
        return (bit >> 3) < maskBytes && (bytes[1 + (bit >> 3)] & (1 << (bit & 7))) !== 0;
    }
    var i = 1 + maskBytes;

    for (var f = 0; f < BIN_FIELDS.length; f++) {
        var bit  = BIN_FIELDS[f][0];
        var key  = BIN_FIELDS[f][1];
        var kind = BIN_FIELDS[f][2];
        if (v3 && BIN_V3_OVERRIDES[bit]) kind = BIN_V3_OVERRIDES[bit];
        if (!hasBit(bit)) continue;

        switch (kind) {
            case "i16_100": d[key] = round2(int16BE(bytes, i) / 100.0); i += 2; break;
            case "i16_10":  d[key] = round1(int16BE(bytes, i) / 10.0);  i += 2; break;
            case "u16_10":  d[key] = round1(uint16BE(bytes, i) / 10.0); i += 2; break;
            case "u16_1":   d[key] = uint16BE(bytes, i); i += 2; break;
            case "u8_2":    d[key] = round1(bytes[i] / 2.0); i += 1; break;
            case "u8":      d[key] = bytes[i]; i += 1; break;
            case "u32":     d[key] = uint32BE(bytes, i); i += 4; break;
            case "din":     d.inputs = decodeInputs(bytes[i]); i += 1; break;
            case "dout":    d.outputs = decodeOutputs(bytes[i]); i += 1; break;
            case "cs":
                d.victron_cs      = bytes[i];
                d.victron_cs_text = VICTRON_CS[bytes[i]] || ("Unknown (" + bytes[i] + ")");
                i += 1;
                break;
            case "diag":    d.diag = decodeDiag(bytes[i]); i += 1; break;
        }
    }
    return d;
}

// ── ChirpStack v4 Entry Point ──────────────────────────────────────────────

function decodeUplink(input) {
    try {
        var data;
        if      (input.fPort === 2) data = parsePort2(input.bytes);
        else if (input.fPort === 3) data = parsePort3(input.bytes);
        else                        data = parsePort1(input.bytes);
        return { data: data, errors: [] };
    } catch (e) {
        return { data: {}, errors: [e.toString()] };
    }
}

// ── ChirpStack v3 Entry Point ──────────────────────────────────────────────

function Decode(fPort, bytes) {
    try {
        if (fPort === 2) return parsePort2(bytes);
        if (fPort === 3) return parsePort3(bytes);
        return parsePort1(bytes);
    } catch (e) {
        return { error: e.toString() };
    }
}
