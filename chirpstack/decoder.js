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
 *   Ch  9  Analog Input     → LiPo voltage (V) OR DALY status flag (0/1)
 *   Ch 10  Analog Input     → Victron MPPT battery voltage (V, optional)
 *   Ch 11  Analog Input     → Victron MPPT PV power (W, optional)
 *   Ch 12  Analog Input     → Victron charge current (A, optional)
 *   Ch 13  Analog Input     → Victron charge state CS (0=Off, 3=Bulk, 4=Abs, 5=Float)
 *   Ch 14  Analog Input     → Victron yield today (kWh, optional)
 *   Ch 15  Analog Input     → DALY BMS pack voltage (V, optional)
 *   Ch 16  Analog Input     → DALY BMS state of charge (%, optional)
 *   Ch 17  Analog Input     → DALY BMS current (+A=charging, −A=discharging, optional)
 *   Ch 18  Temperature      → DALY BMS average battery temperature (°C, optional)
 *   Ch 19  Analog Input     → DALY BMS cell voltage spread max−min (mV, optional)
 *   Ch 20  Digital Input    → Diagnostic bitmask (bits 0–4, see below)
 *   Ch200  Digital Input    → Debug cycle counter (0-255, debug mode only)
 *   Ch202  Digital Input    → Debug send fail counter (0-255, debug mode only)
 *   Ch203  Analog Input     → Debug free heap (KiB, debug mode only)
 *   Ch204  Analog Input     → Debug uptime (h, debug mode only)
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
 * ── Port 2: Daily uplink (every 288 cycles = 24 h at 5 min sleep interval) ──
 *   Ch  1  Analog Input     → Total volume today (L)
 *   Ch  2  Analog Input     → Maximum flow rate today (L/min)
 *   Ch  3  Temperature      → Minimum temperature DS18B20 #1 (°C)
 *   Ch  4  Temperature      → Maximum temperature DS18B20 #1 (°C)
 *
 * ── Port 3: Compact binary telemetry (optional, selectable in WiFi portal) ──
 *   Byte0=version(0x02), Byte1..4=32-bit bitmask, Byte5..N=data
 *   Carries the same values as port 1 but ~50% smaller (no per-field overhead).
 */

// ── CayenneLPP type constants ──────────────────────────────────────────────
var LPP_DIGITAL_INPUT  = 0;    // 1 Byte unsigned
var LPP_ANALOG_INPUT   = 2;    // 2 Byte signed, ÷100
var LPP_TEMPERATURE    = 103;  // 2 Byte signed, ÷10
var LPP_HUMIDITY       = 104;  // 1 Byte unsigned, ÷2
var LPP_BAROMETRIC     = 115;  // 2 Byte unsigned, ÷10

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
                else if (ch === 9)  d.battery_status        = val;  // V (LiPo) or 0/1 (DALY)
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

            case LPP_DIGITAL_INPUT: {
                var dval = bytes[i++];
                if (ch === 20) d.diag = decodeDiag(dval);
                else if (ch === 200) d.debug_cycle_counter = dval;
                else if (ch === 202) d.debug_send_fail_counter = dval;
                else if (ch === 208) d.debug_link_gw_count = dval;
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
//   Byte 0    : protocol version (0x02)
//   Byte 1..4 : 32-bit field-presence bitmask, little-endian
//   Byte 5..N : data for each present field, in ascending bit-index order
// Values mirror the CayenneLPP resolution (analog ÷100, temp ÷10, hum ÷2,
// pressure ÷10, CS/diag/counters raw bytes).
var BIN_PROTO_VERSION = 0x02;

// [bit, output key, encoding]
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
    [27, "debug_link_gw_count",     "u8"]
];

function parsePort3(bytes) {
    var d = {};
    if (bytes.length < 5) {
        return { error: "binary payload too short (" + bytes.length + " bytes)" };
    }
    d.proto_version = bytes[0];
    if (bytes[0] !== BIN_PROTO_VERSION) {
        return {
            error: "unsupported binary protocol version " + bytes[0] +
                   " (expected " + BIN_PROTO_VERSION + ")"
        };
    }
    var mask = (bytes[1]) |
               (bytes[2] << 8) |
               (bytes[3] << 16) |
               (bytes[4] << 24);
    var i = 5;

    for (var f = 0; f < BIN_FIELDS.length; f++) {
        var bit  = BIN_FIELDS[f][0];
        var key  = BIN_FIELDS[f][1];
        var kind = BIN_FIELDS[f][2];
        if ((mask & (1 << bit)) === 0) continue;

        switch (kind) {
            case "i16_100": d[key] = round2(int16BE(bytes, i) / 100.0); i += 2; break;
            case "i16_10":  d[key] = round1(int16BE(bytes, i) / 10.0);  i += 2; break;
            case "u16_10":  d[key] = round1(uint16BE(bytes, i) / 10.0); i += 2; break;
            case "u8_2":    d[key] = round1(bytes[i] / 2.0); i += 1; break;
            case "u8":      d[key] = bytes[i]; i += 1; break;
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
