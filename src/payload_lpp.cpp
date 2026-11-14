// CayenneLPP encoders (FPort 1 measurement, FPort 2 daily summary).
#include "payload.h"
#include <Arduino.h>
#include <CayenneLPP.h>

namespace {

// CayenneLPP converts with a truncating cast ((uint32_t)(|v| * mult)), so
// e.g. 12.34 would be sent as 12.33. Round to the field's resolution first
// and nudge a quarter step away from zero so the cast lands on the rounded
// value. Values outside the field's range are clamped instead of wrapping
// (an int16 x100 field would turn 400 W into -255.36 W).
float lppPrep(float v, float mult, float lo, float hi)
{
    v = constrain(v, lo, hi);
    float steps = roundf(v * mult);
    return (steps + (steps >= 0.0f ? 0.25f : -0.25f)) / mult;
}

void lppAnalog(CayenneLPP& p, uint8_t ch, float v) { p.addAnalogInput(ch, lppPrep(v, 100.0f, -327.68f, 327.67f)); }
void lppTemp  (CayenneLPP& p, uint8_t ch, float v) { p.addTemperature(ch, lppPrep(v, 10.0f, -3276.8f, 3276.7f)); }
void lppUInt  (CayenneLPP& p, uint8_t ch, float v) { p.addGenericSensor(ch, lppPrep(v, 1.0f, 0.0f, 4294967295.0f)); }

// Exact uint32 Generic Sensor: addGenericSensor() goes through float, which
// rounds counts above 2^24, so the four value bytes are rewritten afterwards.
void lppCount(CayenneLPP& p, uint8_t ch, uint32_t v)
{
    if (!p.addGenericSensor(ch, 0.0f)) return;
    uint8_t* b = p.getBuffer() + p.getSize() - 4;
    b[0] = (uint8_t)(v >> 24);  b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);   b[3] = (uint8_t)(v & 0xFF);
}

} // namespace

void encodeLpp(const Measurements& m, CayenneLPP& lpp)
{
    lpp.reset();

    if (m.hasLevel) lppAnalog(lpp, 1, m.levelMa);
    if (m.hasFlow)  lppAnalog(lpp, 2, m.flowLMin);

    for (int i = 0; i < 3; i++)
        if (m.hasTemp[i]) lppTemp(lpp, 3 + i, m.tempC[i]);

    if (m.hasBme) {
        lppTemp(lpp, 6, m.bmeTempC);
        lpp.addRelativeHumidity  (7, lppPrep(m.bmeHumPct,  2.0f, 0.0f, 127.5f));
        lpp.addBarometricPressure(8, lppPrep(m.bmePresHpa, 10.0f, 0.0f, 6553.5f));
    }

    // CH9: VIN voltage (bmsMode 2) or DALY status flag (bmsMode 1)
    if (m.hasBattery) lppAnalog(lpp, 9, m.battery);

    // Victron telemetry: contiguous channel block 10–14
    if (m.hasVictron) {
        lppAnalog(lpp, 10, m.victronV);
        lpp.addPower(11, lppPrep(m.victronPvW, 1.0f, 0.0f, 65535.0f));  // W, uint16 (int16 x100 overflowed above 327 W)
        lppAnalog(lpp, 12, m.victronA);
    }
    if (m.hasVictronCs)    lppAnalog(lpp, 13, m.victronCs);
    if (m.hasVictronYield) lppAnalog(lpp, 14, m.victronYieldKwh);

    // DALY BMS telemetry: contiguous channel block 15–19
    if (m.hasBms) {
        lppAnalog(lpp, 15, m.bmsV);
        lppAnalog(lpp, 16, m.bmsSocPct);
        lppAnalog(lpp, 17, m.bmsA);           // A, positive=charging, negative=discharging
        lppTemp  (lpp, 18, m.bmsTempC);       // average battery temperature
        lppUInt  (lpp, 19, m.bmsCellDiffMv);  // mV cell voltage spread (battery health)
    }

    // CH20: diagnostic flags (bitmask)
    lpp.addDigitalInput(20, m.statusFlags);

    // I/O block: CH21 digital inputs, CH22 switched outputs (bitmasks),
    // CH23 2nd flow sensor (L/min) or CH24 pulse counter (uint32),
    // CH25/CH26 0-10 V inputs AI1/AI2 (V)
    if (m.hasDin)    lpp.addDigitalInput (21, m.dinMask);
    if (m.hasOut)    lpp.addDigitalOutput(22, m.outMask);
    if (m.hasFlow2)  lppAnalog(lpp, 23, m.flow2LMin);
    if (m.hasCount2) lppCount(lpp, 24, m.count2);
    if (m.hasAi) {
        lppAnalog(lpp, 25, m.ai1V);
        lppAnalog(lpp, 26, m.ai2V);
    }

    if (m.debug) {
        lpp.addDigitalInput(200, m.dbgCycle);
        lpp.addDigitalInput(202, m.dbgSendFail);
        lppUInt(lpp, 203, m.dbgHeapKib);           // KiB
        lppUInt(lpp, 204, m.dbgUptimeH * 10.0f);   // 0.1 h
        if (m.hasDlQuality) {
            lppAnalog(lpp, 205, m.dlRssiDbm);      // dBm
            lppAnalog(lpp, 206, m.dlSnrDb);        // dB
        }
        if (m.hasLinkCheck) {
            lppAnalog(lpp, 207, m.linkMarginDb);   // dB (gateway demod margin)
            lpp.addDigitalInput(208, m.linkGwCount);
        }
    }
    if (lpp.getError() != LPP_ERROR_OK)
        Serial.printf("[LPP] Encoding error %d - payload truncated (%u bytes)\n",
                      lpp.getError(), (unsigned)lpp.getSize());
}

void encodeDailyLpp(const DailyStats& d, CayenneLPP& lpp)
{
    lpp.reset();
    lppUInt  (lpp, 1, d.totalLiters * 10.0f);  // 0.1 L, uint32
    lppAnalog(lpp, 2, d.maxFlowLMin);
    if (d.minTempC <= d.maxTempC) {            // else: no valid DS18B20 reading
        lppTemp(lpp, 3, d.minTempC);
        lppTemp(lpp, 4, d.maxTempC);
    }
}
