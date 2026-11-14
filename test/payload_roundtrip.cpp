// Host-side helper for payload_roundtrip.test.js: encodes a few measurement
// sets with the firmware's binary encoder (src/payload_binary.cpp) and prints
// them as JSON, so the test can decode them with chirpstack/decoder.js.
#include <stdio.h>
#include "payload.h"

static void emit(const char* name, const Measurements& m, bool last)
{
    uint8_t buf[BIN_MAX_SIZE];
    size_t n = encodeBinary(m, buf);
    printf("  {\"name\": \"%s\", \"bytes\": [", name);
    for (size_t i = 0; i < n; i++) printf("%s%u", i ? ", " : "", buf[i]);
    printf("]}%s\n", last ? "" : ",");
}

int main()
{
    printf("[\n");

    // Every field present, typical values.
    Measurements full;
    full.hasLevel = true;        full.levelMa = 12.34f;
    full.hasFlow = true;         full.flowLMin = 7.5f;
    for (int i = 0; i < 3; i++) { full.hasTemp[i] = true; full.tempC[i] = 8.1f + i; }
    full.hasBme = true;          full.bmeTempC = -3.4f; full.bmeHumPct = 81.5f; full.bmePresHpa = 1013.2f;
    full.hasBattery = true;      full.battery = 3.87f;
    full.hasVictron = true;      full.victronV = 13.42f; full.victronPvW = 412.0f; full.victronA = 2.35f;
    full.hasVictronCs = true;    full.victronCs = 4;
    full.hasVictronYield = true; full.victronYieldKwh = 1.27f;
    full.hasBms = true;          full.bmsV = 26.4f; full.bmsSocPct = 87.0f; full.bmsA = -1.25f;
                                 full.bmsTempC = 14.5f; full.bmsCellDiffMv = 512.0f;
    full.statusFlags = 0x13;
    full.debug = true;           full.dbgCycle = 200; full.dbgSendFail = 3;
                                 full.dbgHeapKib = 287.4f; full.dbgUptimeH = 1234.56f;
    full.hasDlQuality = true;    full.dlRssiDbm = -87.5f; full.dlSnrDb = 7.25f;
    full.hasLinkCheck = true;    full.linkMarginDb = 23.0f; full.linkGwCount = 2;
    full.hasDin = true;          full.dinMask = 0x05;
    full.hasOut = true;          full.outMask = 0x02;
    full.hasFlow2 = true;        full.flow2LMin = 3.25f;
    full.hasCount2 = true;       full.count2 = 4000000123UL;
    full.hasAi = true;           full.ai1V = 4.56f; full.ai2V = 10.02f;
    emit("full", full, false);

    // Minimal: only the diag byte is mandatory.
    Measurements minimal;
    emit("minimal", minimal, false);

    // Out-of-range values must clamp, not wrap.
    Measurements clamp;
    clamp.hasLevel = true;       clamp.levelMa = 400.0f;      // > 327.67
    clamp.hasVictron = true;     clamp.victronV = 13.0f; clamp.victronPvW = 70000.0f; clamp.victronA = -400.0f;
    clamp.hasBms = true;         clamp.bmsCellDiffMv = -5.0f;  // unsigned field
    emit("clamp", clamp, true);

    printf("]\n");
    return 0;
}
