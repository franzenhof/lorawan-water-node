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

function testPort1WideFields() {
  // Fields that overflowed int16 x100 (> 327.67) in older firmware.
  const bytes = [
    11, 128, 0x01, 0x90,              // ch11 power: 400 W
    19, 100, 0x00, 0x00, 0x02, 0x00,  // ch19 generic: 512 mV cell diff
    203, 100, 0x00, 0x00, 0x01, 0x1F, // ch203 generic: 287 KiB heap
    204, 100, 0x00, 0x00, 0x30, 0x39  // ch204 generic: 12345 -> 1234.5 h
  ];

  const d = Decode(1, bytes);
  eq(d.victron_pv_w, 400, 'Port1 ch11 power');
  eq(d.daly_cell_diff_mv, 512, 'Port1 ch19 cell diff');
  eq(d.debug_free_heap_kib, 287, 'Port1 ch203 heap');
  eq(d.debug_uptime_h, 1234.5, 'Port1 ch204 uptime');
}

function testPort2WideVolume() {
  const bytes = [
    1, 100, 0x00, 0x01, 0xE2, 0x40,   // ch1 generic: 123456 -> 12345.6 L
    2, 2, 0x04, 0xB0,                 // ch2 12.00 L/min
    4, 103, 0x00, 0x7B                // ch4 12.3 C
  ];

  const d = Decode(2, bytes);
  eq(d.total_liters_today, 12345.6, 'Port2 wide liters');
  eq(d.max_flow_l_min, 12, 'Port2 max flow');
  eq(d.max_temp_source_c, 12.3, 'Port2 max temp');
}

function testPort3Version3WideFields() {
  const bytes = [
    0x03,
    0x00, 0x04, 0xC4, 0x00,    // mask bits 10,18,22,23
    0x01, 0x90,                // bit10 victron_pv_w = 400
    0x02, 0x00,                // bit18 daly_cell_diff_mv = 512
    0x01, 0x1F,                // bit22 debug_free_heap_kib = 287
    0x30, 0x39                 // bit23 debug_uptime_h = 1234.5
  ];

  const d = decodeUplink({ fPort: 3, bytes }).data;
  eq(d.proto_version, 3, 'Port3 v3 version');
  eq(d.victron_pv_w, 400, 'Port3 v3 PV power');
  eq(d.daly_cell_diff_mv, 512, 'Port3 v3 cell diff');
  eq(d.debug_free_heap_kib, 287, 'Port3 v3 heap');
  eq(d.debug_uptime_h, 1234.5, 'Port3 v3 uptime');
}

function testPort3Version2StillDecoded() {
  const bytes = [
    0x02,
    0x00, 0x04, 0x00, 0x00,    // mask bit 10
    0x27, 0x10                 // bit10 victron_pv_w = 100.00 (v2: int16 x100)
  ];

  const d = Decode(3, bytes);
  eq(d.proto_version, 2, 'Port3 v2 version');
  eq(d.victron_pv_w, 100, 'Port3 v2 PV power');
}

function testPort1BoardIo() {
  // PCB v1.0 I/O block
  const bytes = [
    21, 0, 0x06,                      // ch21 inputs: IN2 + IN3 high
    22, 1, 0x01,                      // ch22 outputs: OUT1 on
    23, 2, 0x01, 0xF4,                // ch23 2nd flow 5.00 L/min
    24, 100, 0x01, 0x23, 0x45, 0x67,  // ch24 counter 19088743 (> 2^24)
    25, 2, 0x03, 0xE8,                // ch25 AI1 10.00 V
    26, 2, 0x00, 0x7B                 // ch26 AI2 1.23 V
  ];

  const d = Decode(1, bytes);
  eq(d.inputs.din1, false, 'Port1 ch21 IN1');
  eq(d.inputs.din2, true, 'Port1 ch21 IN2');
  eq(d.outputs.out1, true, 'Port1 ch22 OUT1');
  eq(d.outputs.out2, false, 'Port1 ch22 OUT2');
  eq(d.flow2_l_min, 5, 'Port1 ch23 flow2');
  eq(d.inputs.din3, true, 'Port1 ch21 IN3');
  eq(d.pulse_count, 19088743, 'Port1 ch24 counter');
  eq(d.ai1_v, 10, 'Port1 ch25 AI1');
  eq(d.ai2_v, 1.23, 'Port1 ch26 AI2');
}

function testPort3BoardIo() {
  const bytes = [
    0x03,
    0x00, 0x00, 0x08, 0xF0,    // mask bits 19,28,29,30,31
    0x00,                      // bit19 diag
    0x03,                      // bit28 inputs: IN1+IN2 high
    0x02,                      // bit29 outputs: OUT2 on
    0x03, 0xE8,                // bit30 flow2_l_min = 10.00
    0xFF, 0xFF, 0xFF, 0xFE     // bit31 pulse_count = 4294967294
  ];

  const d = decodeUplink({ fPort: 3, bytes }).data;
  eq(d.inputs.din1 && d.inputs.din2, true, 'Port3 inputs');
  eq(d.outputs.out1, false, 'Port3 OUT1');
  eq(d.outputs.out2, true, 'Port3 OUT2');
  eq(d.outputs.out3, false, 'Port3 OUT3');
  eq(d.flow2_l_min, 10, 'Port3 flow2');
  eq(d.pulse_count, 4294967294, 'Port3 counter (bit 31)');
}

function testPort3Version4Analog() {
  const bytes = [
    0x04,
    0x00, 0x00, 0x08, 0x10, 0x03,  // mask bits 19,28,32,33
    0x00,                          // bit19 diag
    0x07,                          // bit28 inputs: IN1-IN3 high
    0x01, 0xC8,                    // bit32 ai1_v = 4.56
    0x03, 0xEA                     // bit33 ai2_v = 10.02
  ];

  const d = decodeUplink({ fPort: 3, bytes }).data;
  eq(d.proto_version, 4, 'Port3 v4 version');
  eq(d.inputs.din3, true, 'Port3 v4 IN3');
  eq(d.ai1_v, 4.56, 'Port3 v4 AI1');
  eq(d.ai2_v, 10.02, 'Port3 v4 AI2');
}

function run() {
  testPort1MixedPayload();
  testPort2DailyPayload();
  testPort3RejectsUnsupportedVersion();
  testPort3CurrentRfDebugFields();
  testPort1WideFields();
  testPort2WideVolume();
  testPort3Version3WideFields();
  testPort3Version2StillDecoded();
  testPort1BoardIo();
  testPort3BoardIo();
  testPort3Version4Analog();
  console.log('All decoder tests passed.');
}

run();
