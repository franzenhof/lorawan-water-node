const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const decoderPath = require('path').join(__dirname, '..', 'chirpstack', 'decoder.js');
const source = fs.readFileSync(decoderPath, 'utf8');

const sandbox = { console };
vm.createContext(sandbox);
vm.runInContext(source, sandbox, { filename: decoderPath });

const Decode = sandbox.Decode;
const decodeUplink = sandbox.decodeUplink;

function eq(actual, expected, message) {
  assert.strictEqual(actual, expected, message);
}

function ok(value, message) {
  assert.ok(value, message);
}

function testPort1MixedPayload() {
  const bytes = [
    1, 2, 0x04, 0xD2,          // ch1 12.34 mA
    3, 103, 0x00, 0xD7,        // ch3 21.5 C
    7, 104, 112,               // ch7 56.0 %
    13, 2, 0x01, 0x90,         // ch13 CS=4.00 -> Absorption
    20, 0, 0x13,               // diag bits: 0,1,4
    205, 2, 0xDD, 0xD2,        // ch205 -87.5 dBm
    208, 0, 0x03               // ch208 GW count = 3
  ];

  const d = Decode(1, bytes);
  eq(d.level_sensor_ma, 12.34, 'Port1 ch1 decode');
  eq(d.temp_source_c, 21.5, 'Port1 ch3 decode');
  eq(d.bme280_humidity_pct, 56, 'Port1 ch7 decode');
  eq(d.victron_cs, 4, 'Port1 ch13 decode');
  eq(d.victron_cs_text, 'Absorption', 'Port1 victron cs text');
  eq(d.debug_dl_rssi_dbm, -87.5, 'Port1 ch205 decode');
  eq(d.debug_link_gw_count, 3, 'Port1 ch208 decode');
  ok(d.diag.victron_no_frame, 'Port1 diag bit0');
  ok(d.diag.daly_no_response, 'Port1 diag bit1');
  ok(d.diag.last_reset_was_wdt, 'Port1 diag bit4');
}

function testPort2DailyPayload() {
  const bytes = [
    1, 2, 0x30, 0x39,          // ch1 123.45 L
    3, 103, 0x00, 0x38         // ch3 5.6 C
  ];

  const d = Decode(2, bytes);
  eq(d._type, 'daily_summary', 'Port2 type');
  eq(d.total_liters_today, 123.45, 'Port2 liters');
  eq(d.min_temp_source_c, 5.6, 'Port2 min temp');
}

function testPort3RejectsUnsupportedVersion() {
  const bytes = [
    0x01,
    0x01, 0x10, 0x08,          // mask bits 0,12,19
    0x04, 0x1A,                // bit0 level_sensor_ma = 10.50
    0x05,                      // bit12 victron_cs = 5 (Float)
    0x04                       // bit19 diag: ds18b20_read_error
  ];

  const d = Decode(3, bytes);
  ok(typeof d.error === 'string', 'Port3 unsupported version returns error');
  ok(d.error.indexOf('unsupported binary protocol version') !== -1, 'Port3 unsupported version message');
}

function testPort3CurrentRfDebugFields() {
  const bytes = [
    0x02,
    0x02, 0x00, 0x00, 0x0F,    // mask bits 1,24,25,26,27
    0x00, 0xFA,                // bit1 flow_l_min = 2.50
    0xE4, 0x8F,                // bit24 debug_dl_rssi_dbm = -70.25
    0x03, 0xCF,                // bit25 debug_dl_snr_db = 9.75
    0x08, 0xFC,                // bit26 debug_link_margin_db = 23.00
    0x02                       // bit27 debug_link_gw_count = 2
  ];

  const d = decodeUplink({ fPort: 3, bytes }).data;
  eq(d.proto_version, 2, 'Port3 proto version');
  eq(d.flow_l_min, 2.5, 'Port3 flow');
  eq(d.debug_dl_rssi_dbm, -70.25, 'Port3 RSSI');
  eq(d.debug_dl_snr_db, 9.75, 'Port3 SNR');
  eq(d.debug_link_margin_db, 23, 'Port3 margin');
  eq(d.debug_link_gw_count, 2, 'Port3 gw count');
}

function run() {
  testPort1MixedPayload();
  testPort2DailyPayload();
  testPort3RejectsUnsupportedVersion();
  testPort3CurrentRfDebugFields();
  console.log('All decoder tests passed.');
}

run();
