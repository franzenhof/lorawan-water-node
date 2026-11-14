#include "downlink.h"

static bool applyParamUpdate(Config& cfg, uint8_t paramId, int16_t value, bool& requiresRestart)
{
    switch (paramId) {
        case 1: // sleepSec, 0=continuous
            cfg.sleepSec = (value == 0) ? 0 : max(10, (int)value);
            return true;
        case 2: // samplingMs
            cfg.samplingMs = max(1000, (int)value);
            return true;
        case 8: // continuousSendSec
            cfg.continuousSendSec = max(10, (int)value);
            return true;
        case 3: // kFactor
            cfg.kFactor = max(1, (int)value);
            return true;
        case 4: // altitudeM
            cfg.altitudeM = constrain((int)value, 0, 9999);
            return true;
        case 5: // bmsInterval
            cfg.bmsInterval = max(1, (int)value);
            return true;
        case 10: // victronInterval
            cfg.victronInterval = max(1, (int)value);
            return true;
        case 11: // payloadFormat (0=CayenneLPP, 1=binary)
            cfg.payloadFormat = (value == 1) ? 1 : 0;
            return true;
        case 6: // debug mode
            cfg.debugMode = (value == 1) ? 1 : 0;
            return true;
        case 7: // LoRaWAN region by numeric code (0..6)
            codeToRegion((int)value, cfg.region, sizeof(cfg.region));
            requiresRestart = true;
            return true;
        case 9: // displayOnCycles (continuous mode only), 0 = always on
            cfg.displayOnCycles = max(0, min(1000, (int)value));
            return true;
        case 12: // kFactor2 (2nd pulse input, flow mode)
            cfg.kFactor2 = max(1, (int)value);
            return true;
        case 13: // pulse2Mode (0=off, 1=2nd flow sensor, 2=counter)
            cfg.pulse2Mode = constrain((int)value, 0, 2);
            return true;
        case 14: // dinEn (digital inputs IN1-IN3)
            cfg.dinEn = (value == 1) ? 1 : 0;
            return true;
        case 15: // aiEn (0-10 V inputs AI1/AI2)
            cfg.aiEn = (value == 1) ? 1 : 0;
            return true;
        case 16: // dinWake (wake on input change, bit 0 = IN1)
            cfg.dinWake = constrain((int)value, 0, 7);
            return true;
        default:
            return false;
    }
}

bool processParamDownlink(Config& cfg, const uint8_t* buf, size_t len, bool& requiresRestart)
{
    // Format: 0x10, then N tuples: [paramId][valueHi][valueLo] (signed int16, big-endian)
    if (len < 4 || buf[0] != 0x10 || ((len - 1) % 3) != 0) return false;

    bool anyApplied = false;
    for (size_t i = 1; i + 2 < len; i += 3) {
        uint8_t paramId = buf[i];
        int16_t value = (int16_t)(((uint16_t)buf[i + 1] << 8) | buf[i + 2]);
        if (applyParamUpdate(cfg, paramId, value, requiresRestart)) {
            anyApplied = true;
        }
    }
    if (anyApplied) {
        saveConfig(cfg);
        Serial.println("[Downlink] Parameters updated and saved.");
    }
    return anyApplied;
}

bool processOutputDownlink(const uint8_t* buf, size_t len, uint8_t& outMask)
{
    // Format: 0x20, then N pairs: [output 1|2|3][state 0=off, 1=on]
    if (len < 3 || buf[0] != 0x20 || ((len - 1) % 2) != 0) return false;

    uint8_t mask = outMask;
    for (size_t i = 1; i + 1 < len; i += 2) {
        uint8_t out = buf[i], state = buf[i + 1];
        if (out < 1 || out > 3 || state > 1) return false;  // reject the whole command
        uint8_t bit = (uint8_t)(1 << (out - 1));
        mask = state ? (mask | bit) : (mask & ~bit);
    }
    outMask = mask;
    return true;
}
