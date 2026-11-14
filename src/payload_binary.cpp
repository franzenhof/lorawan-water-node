// Compact binary encoder (FPort 3). Plain C++ on purpose - see payload.h.
#include "payload.h"
#include <math.h>
#include <string.h>

namespace {

struct Writer {
    uint8_t* buf;
    size_t   n = 0;
    uint64_t mask = 0;

    explicit Writer(uint8_t* b) : buf(b) {}

    // Out-of-range values are clamped instead of wrapping.
    static long clampL(long v, long lo, long hi) { return v < lo ? lo : (v > hi ? hi : v); }

    void i16(float v, float scale) {
        uint16_t x = (uint16_t)(int16_t)clampL(lroundf(v * scale), -32768L, 32767L);
        buf[n++] = (uint8_t)(x >> 8);
        buf[n++] = (uint8_t)(x & 0xFF);
    }
    void u16(float v, float scale) {
        uint16_t x = (uint16_t)clampL(lroundf(v * scale), 0L, 65535L);
        buf[n++] = (uint8_t)(x >> 8);
        buf[n++] = (uint8_t)(x & 0xFF);
    }
    void u32(uint32_t v) {
        buf[n++] = (uint8_t)(v >> 24);
        buf[n++] = (uint8_t)(v >> 16);
        buf[n++] = (uint8_t)(v >> 8);
        buf[n++] = (uint8_t)(v & 0xFF);
    }
    void u8(uint8_t v) { buf[n++] = v; }
    void set(BinField f) { mask |= (1ULL << f); }
};

} // namespace

size_t encodeBinary(const Measurements& m, uint8_t* out)
{
    // Data goes behind the header; the mask is only known at the end.
    Writer w(out + BIN_HEADER_SIZE);

    if (m.hasLevel) { w.set(BF_LEVEL); w.i16(m.levelMa, 100.0f); }
    if (m.hasFlow)  { w.set(BF_FLOW);  w.i16(m.flowLMin, 100.0f); }
    for (int i = 0; i < 3; i++)
        if (m.hasTemp[i]) { w.set((BinField)(BF_TEMP1 + i)); w.i16(m.tempC[i], 10.0f); }
    if (m.hasBme) {
        w.set(BF_BME_TEMP); w.i16(m.bmeTempC, 10.0f);
        w.set(BF_BME_HUM);  w.u8((uint8_t)Writer::clampL(lroundf(m.bmeHumPct * 2.0f), 0L, 255L));
        w.set(BF_BME_PRES); w.u16(m.bmePresHpa, 10.0f);
    }
    if (m.hasBattery) { w.set(BF_BATT_STATUS); w.i16(m.battery, 100.0f); }
    if (m.hasVictron) {
        w.set(BF_VIC_V); w.i16(m.victronV, 100.0f);
        w.set(BF_VIC_P); w.u16(m.victronPvW, 1.0f);
        w.set(BF_VIC_I); w.i16(m.victronA, 100.0f);
    }
    if (m.hasVictronCs)    { w.set(BF_VIC_CS);    w.u8(m.victronCs); }
    if (m.hasVictronYield) { w.set(BF_VIC_YIELD); w.i16(m.victronYieldKwh, 100.0f); }
    if (m.hasBms) {
        w.set(BF_DALY_V);        w.i16(m.bmsV, 100.0f);
        w.set(BF_DALY_SOC);      w.i16(m.bmsSocPct, 100.0f);
        w.set(BF_DALY_I);        w.i16(m.bmsA, 100.0f);
        w.set(BF_DALY_TEMP);     w.i16(m.bmsTempC, 10.0f);
        w.set(BF_DALY_CELLDIFF); w.u16(m.bmsCellDiffMv, 1.0f);
    }
    w.set(BF_DIAG); w.u8(m.statusFlags);
    if (m.debug) {
        w.set(BF_DBG_CYCLE);  w.u8(m.dbgCycle);
        w.set(BF_DBG_FAIL);   w.u8(m.dbgSendFail);
        w.set(BF_DBG_HEAP);   w.u16(m.dbgHeapKib, 1.0f);
        w.set(BF_DBG_UPTIME); w.u16(m.dbgUptimeH, 10.0f);
        if (m.hasDlQuality) {
            w.set(BF_DBG_DL_RSSI); w.i16(m.dlRssiDbm, 100.0f);
            w.set(BF_DBG_DL_SNR);  w.i16(m.dlSnrDb, 100.0f);
        }
        if (m.hasLinkCheck) {
            w.set(BF_DBG_LINK_MARGIN); w.i16(m.linkMarginDb, 100.0f);
            w.set(BF_DBG_LINK_GW);     w.u8(m.linkGwCount);
        }
    }
    if (m.hasDin)    { w.set(BF_DIN);    w.u8(m.dinMask); }
    if (m.hasOut)    { w.set(BF_OUT);    w.u8(m.outMask); }
    if (m.hasFlow2)  { w.set(BF_FLOW2);  w.i16(m.flow2LMin, 100.0f); }
    if (m.hasCount2) { w.set(BF_COUNT2); w.u32(m.count2); }
    if (m.hasAi) {
        w.set(BF_AI1); w.i16(m.ai1V, 100.0f);
        w.set(BF_AI2); w.i16(m.ai2V, 100.0f);
    }

    out[0] = BIN_PROTO_VERSION;
    for (size_t i = 0; i < BIN_HEADER_SIZE - 1; i++)
        out[1 + i] = (uint8_t)((w.mask >> (8 * i)) & 0xFF);
    return BIN_HEADER_SIZE + w.n;
}
