// Encoder/decoder round trip for the binary format (FPort 3): compiles the
// firmware's src/payload_binary.cpp for the host, encodes known values and
// checks that chirpstack/decoder.js decodes them back. Needs a C++ compiler
// (c++/g++/clang++); skipped with a message if none is available.
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const vm = require('vm');
const { execFileSync } = require('child_process');

const root = path.join(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'chirpstack', 'decoder.js'), 'utf8');
const sandbox = { console };
vm.createContext(sandbox);
vm.runInContext(source, sandbox);

const exe = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'payload-rt-')), 'roundtrip');
let compiled = false;
for (const cxx of [process.env.CXX, 'c++', 'g++', 'clang++'].filter(Boolean)) {
  try {
    execFileSync(cxx, ['-std=c++11', '-Wall', '-Werror', '-I', path.join(root, 'src'),
      path.join(root, 'src', 'payload_binary.cpp'), path.join(__dirname, 'payload_roundtrip.cpp'),
      '-o', exe], { stdio: 'pipe' });
    compiled = true;
    break;
  } catch (e) {
    if (e.code !== 'ENOENT') throw new Error(`${cxx} failed:\n${e.stderr}`);
  }
}
if (!compiled) {
  console.log('SKIPPED payload round trip: no C++ compiler found.');
  process.exit(0);
}

const cases = {};
for (const c of JSON.parse(execFileSync(exe, { encoding: 'utf8' }))) cases[c.name] = c.bytes;
const decode = (name) => sandbox.decodeUplink({ fPort: 3, bytes: cases[name] }).data;

function eq(actual, expected, message) {
  assert.strictEqual(actual, expected, `${message}: got ${actual}, expected ${expected}`);
}

const full = decode('full');
eq(full.proto_version, 4, 'version');
eq(full.level_sensor_ma, 12.34, 'level');
eq(full.flow_l_min, 7.5, 'flow');
eq(full.temp_source_c, 8.1, 'temp1');
eq(full.temp_tank_floor_c, 9.1, 'temp2');
eq(full.temp_air_c, 10.1, 'temp3');
eq(full.bme280_temp_c, -3.4, 'bme temp');
eq(full.bme280_humidity_pct, 81.5, 'bme humidity');
eq(full.bme280_pressure_hpa, 1013.2, 'bme pressure');
eq(full.battery_status, 3.87, 'battery');
eq(full.victron_battery_v, 13.42, 'victron V');
eq(full.victron_pv_w, 412, 'victron PV > 327 W');
eq(full.victron_charge_a, 2.35, 'victron A');
eq(full.victron_cs, 4, 'victron CS');
eq(full.victron_yield_today_kwh, 1.27, 'victron yield');
eq(full.daly_pack_v, 26.4, 'daly V');
eq(full.daly_soc_pct, 87, 'daly SOC');
eq(full.daly_current_a, -1.25, 'daly A');
eq(full.daly_bat_temp_c, 14.5, 'daly temp');
eq(full.daly_cell_diff_mv, 512, 'daly cell diff > 327 mV');
assert.ok(full.diag.victron_no_frame && full.diag.daly_no_response && full.diag.last_reset_was_wdt, 'diag bits');
eq(full.debug_cycle_counter, 200, 'debug cycle');
eq(full.debug_send_fail_counter, 3, 'debug fail');
eq(full.debug_free_heap_kib, 287, 'debug heap');
eq(full.debug_uptime_h, 1234.6, 'debug uptime > 327 h');
eq(full.debug_dl_rssi_dbm, -87.5, 'debug RSSI');
eq(full.debug_dl_snr_db, 7.25, 'debug SNR');
eq(full.debug_link_margin_db, 23, 'debug margin');
eq(full.debug_link_gw_count, 2, 'debug gw');
eq(full.inputs.din1, true, 'IN1');
eq(full.inputs.din2, false, 'IN2');
eq(full.inputs.din3, true, 'IN3');
eq(full.outputs.out1, false, 'OUT1');
eq(full.outputs.out2, true, 'OUT2');
eq(full.flow2_l_min, 3.25, 'flow2');
eq(full.pulse_count, 4000000123, 'counter uint32');
eq(full.ai1_v, 4.56, 'AI1');
eq(full.ai2_v, 10.02, 'AI2 (bit 33, 5th mask byte)');
assert.ok(cases.full.length <= 72, 'full payload fits BIN_MAX_SIZE');

const minimal = decode('minimal');
eq(cases.minimal.length, 7, 'minimal size (header + diag)');
eq(minimal.diag.victron_no_frame, false, 'minimal diag');
eq(minimal.flow_l_min, undefined, 'minimal has no flow');

const clamp = decode('clamp');
eq(clamp.level_sensor_ma, 327.67, 'level clamps at int16 max');
eq(clamp.victron_pv_w, 65535, 'PV clamps at uint16 max');
eq(clamp.victron_charge_a, -327.68, 'current clamps at int16 min');
eq(clamp.daly_cell_diff_mv, 0, 'unsigned field clamps at 0');

console.log(`Payload round trip passed (full binary payload: ${cases.full.length} bytes).`);
