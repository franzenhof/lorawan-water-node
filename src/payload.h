// Uplink payload encoding: CayenneLPP (FPort 1), compact binary (FPort 3)
// and the daily summary (FPort 2). The channel/bit layout is the contract
// with chirpstack/decoder.js - change both together.
//
// This header and payload_binary.cpp are plain C++ (no Arduino), so the
// binary encoder can be compiled on the host and checked against the
// decoder (test/payload_roundtrip.test.js).
#pragma once
#include <stddef.h>
#include <stdint.h>

class CayenneLPP;

// One measurement cycle, already reduced to what goes on air. A field is
// only encoded when its flag is set.
struct Measurements {
    bool    hasLevel = false;          float levelMa = 0.0f;
    bool    hasFlow = false;           float flowLMin = 0.0f;
    bool    hasFlow2 = false;          float flow2LMin = 0.0f;  // 2nd pulse input, flow mode
    bool    hasCount2 = false;         uint32_t count2 = 0;     // 2nd pulse input, counter mode
    bool    hasDin = false;            uint8_t dinMask = 0;     // bit0 IN1, bit1 IN2, bit2 IN3 (1 = terminal high)
    bool    hasOut = false;            uint8_t outMask = 0;     // bit0 OUT1, bit1 OUT2, bit2 OUT3 (1 = on)
    bool    hasAi = false;             float ai1V = 0.0f, ai2V = 0.0f;  // 0-10 V inputs
    bool    hasTemp[3] = {};           float tempC[3] = {};     // DS18B20 #1..#3
    bool    hasBme = false;            float bmeTempC = 0.0f, bmeHumPct = 0.0f, bmePresHpa = 0.0f;
    bool    hasBattery = false;        float battery = 0.0f;    // VIN voltage or DALY status 0/1
    bool    hasVictron = false;        float victronV = 0.0f, victronPvW = 0.0f, victronA = 0.0f;
    bool    hasVictronCs = false;      uint8_t victronCs = 0;
    bool    hasVictronYield = false;   float victronYieldKwh = 0.0f;
    bool    hasBms = false;            float bmsV = 0.0f, bmsSocPct = 0.0f, bmsA = 0.0f,
                                             bmsTempC = 0.0f, bmsCellDiffMv = 0.0f;
    uint8_t statusFlags = 0;           // diag bitmask, always sent
    bool    debug = false;
    uint8_t dbgCycle = 0, dbgSendFail = 0;
    float   dbgHeapKib = 0.0f, dbgUptimeH = 0.0f;
    bool    hasDlQuality = false;      float dlRssiDbm = 0.0f, dlSnrDb = 0.0f;
    bool    hasLinkCheck = false;      float linkMarginDb = 0.0f; uint8_t linkGwCount = 0;
};

// Daily statistics (FPort 2), accumulated across cycles in RTC RAM.
struct DailyStats {
    uint16_t cycleCount;    // cycles since last daily uplink
    float    totalLiters;   // total volume (L) - estimate in deep-sleep mode
    float    maxFlowLMin;   // maximum flow rate (L/min)
    float    minTempC;      // minimum temperature (°C, DS18B20[0])
    float    maxTempC;      // maximum temperature (°C, DS18B20[0])
    uint32_t elapsedSec;    // real time covered so far; daily uplink at 24 h
};
static constexpr DailyStats DAILY_RESET = { 0, 0.0f, 0.0f, 100.0f, -100.0f, 0 };

// ---- Custom binary payload (FPort 3) --------------------------------------
// Layout:  [0] = protocol version
//          [1..5] = 40-bit field-presence bitmask, little-endian
//                   (bit i lives in byte[1 + i/8], LSB first)
//          [6..N] = data for each present field, in ascending bit-index order
// Values mirror CayenneLPP resolution so both formats carry identical data:
//   analog int16 x100 · temperature int16 x10 · humidity uint8 x2 ·
//   barometric uint16 x10 · diag/counters uint8 · Victron CS uint8 (raw).
// Version 3 widened the fields that overflowed int16 x100 (> 327.67):
//   Victron PV power uint16 W · DALY cell diff uint16 mV ·
//   free heap uint16 KiB · uptime uint16 x10 h.
// Version 4 (PCB v1.0) widened the mask to 40 bits for the I/O block:
// digital inputs/outputs uint8 bitmasks, 2nd flow int16 x100, pulse counter
// uint32, 0-10 V inputs int16 x100.
constexpr uint8_t BIN_PROTO_VERSION = 0x04;
constexpr size_t  BIN_HEADER_SIZE   = 6;
constexpr size_t  BIN_MAX_SIZE      = 72;   // every field incl. debug and all I/Os: 68 bytes

enum BinField {
    BF_LEVEL = 0, BF_FLOW, BF_TEMP1, BF_TEMP2, BF_TEMP3,
    BF_BME_TEMP, BF_BME_HUM, BF_BME_PRES, BF_BATT_STATUS,
    BF_VIC_V, BF_VIC_P, BF_VIC_I, BF_VIC_CS, BF_VIC_YIELD,
    BF_DALY_V, BF_DALY_SOC, BF_DALY_I, BF_DALY_TEMP, BF_DALY_CELLDIFF,
    BF_DIAG, BF_DBG_CYCLE, BF_DBG_FAIL, BF_DBG_HEAP, BF_DBG_UPTIME,
    BF_DBG_DL_RSSI, BF_DBG_DL_SNR, BF_DBG_LINK_MARGIN, BF_DBG_LINK_GW,
    BF_DIN, BF_OUT, BF_FLOW2, BF_COUNT2, BF_AI1, BF_AI2
};

// Returns the payload length (at most BIN_MAX_SIZE; out must hold that).
size_t encodeBinary(const Measurements& m, uint8_t* out);

// CayenneLPP with the custom channel mapping (FPort 1). Resets lpp first.
void encodeLpp(const Measurements& m, CayenneLPP& lpp);

// Daily summary (FPort 2). Resets lpp first.
void encodeDailyLpp(const DailyStats& d, CayenneLPP& lpp);
