#include "config.h"
#include <Preferences.h>

static constexpr const char* REGION_CODES[] = {
    "EU868", "US915", "AU915", "AS923", "IN865", "KR920", "CN470"
};
static constexpr size_t REGION_CODES_COUNT = sizeof(REGION_CODES) / sizeof(REGION_CODES[0]);

static bool isSupportedRegion(const String& region)
{
    for (size_t i = 0; i < REGION_CODES_COUNT; i++) {
        if (region.equals(REGION_CODES[i])) return true;
    }
    return false;
}

void normalizeRegionInput(const char* raw, char* out, size_t outSize)
{
    String region = raw ? String(raw) : String("");
    region.trim();
    region.toUpperCase();
    region.replace(" ", "");
    if (!isSupportedRegion(region)) region = "EU868";
    strlcpy(out, region.c_str(), outSize);
}

void codeToRegion(int code, char* out, size_t outSize)
{
    if (code < 0 || code >= (int)REGION_CODES_COUNT) {
        strlcpy(out, "EU868", outSize);
        return;
    }
    strlcpy(out, REGION_CODES[code], outSize);
}

const LoRaWANBand_t* regionToBand(const char* region)
{
    if (strcmp(region, "US915") == 0) return &US915;
    if (strcmp(region, "AU915") == 0) return &AU915;
    if (strcmp(region, "AS923") == 0) return &AS923;
    if (strcmp(region, "IN865") == 0) return &IN865;
    if (strcmp(region, "KR920") == 0) return &KR920;
    if (strcmp(region, "CN470") == 0) return &CN470;
    return &EU868;
}

// Parses exactly nBytes*2 hex digits (nBytes <= 16). ':', '-' and spaces are
// skipped so values pasted from a network-server console work. Anything
// else - wrong length, non-hex characters - is rejected instead of silently
// storing a partial value (strtoull("70:B3:...") would yield 0x70).
bool parseHexBytes(const char* in, uint8_t* out, size_t nBytes)
{
    uint8_t tmp[16] = {};
    size_t digits = 0;
    for (const char* p = in; *p; p++) {
        if (*p == ':' || *p == '-' || *p == ' ') continue;
        if (!isxdigit((unsigned char)*p) || digits >= nBytes * 2) return false;
        uint8_t v = isdigit((unsigned char)*p) ? *p - '0' : toupper((unsigned char)*p) - 'A' + 10;
        tmp[digits / 2] = (uint8_t)((tmp[digits / 2] << 4) | v);
        digits++;
    }
    if (digits != nBytes * 2) return false;
    memcpy(out, tmp, nBytes);
    return true;
}

bool parseEui(const char* in, uint64_t& out)
{
    uint8_t b[8];
    if (!parseHexBytes(in, b, 8)) return false;
    out = 0;
    for (int i = 0; i < 8; i++) out = (out << 8) | b[i];
    return true;
}

// ============================================================================
// CONFIGURATION: NVS READ / WRITE
// ============================================================================
Config loadConfig()
{
    Preferences prefs;
    Config c;
    // On first boot the "config" namespace doesn't exist in NVS yet →
    // begin() returns false. Return the all-defaults Config (devEui == 0),
    // which causes setup() to open the Wi-Fi configuration portal.
    if (!prefs.begin("config", true)) {
        return c;
    }
    c.sleepSec    = prefs.getInt    ("sleep",      c.sleepSec);
    c.samplingMs  = prefs.getInt    ("sample",     c.samplingMs);
    c.continuousSendSec = prefs.getInt("sendint",   c.continuousSendSec);
    c.kFactor     = prefs.getInt    ("kfactor",    c.kFactor);
    c.displayOnCycles = prefs.getInt("dispcyc",    c.displayOnCycles);
    c.cpuFreqMhz  = prefs.getInt    ("cpufreq",    c.cpuFreqMhz);
    c.bmsMode     = prefs.getInt    ("bmsmode",    c.bmsMode);
    c.victronEn   = prefs.getInt    ("victornen",  c.victronEn);
    c.highsideEn  = prefs.getInt    ("highsideen", c.highsideEn);
    c.levelEn     = prefs.getInt    ("levelen",    c.levelEn);
    c.flowEn      = prefs.getInt    ("flowen",     c.flowEn);
    c.pulse2Mode  = prefs.getInt    ("p2mode",     c.pulse2Mode);
    c.kFactor2    = prefs.getInt    ("kfactor2",   c.kFactor2);
    c.dinEn       = prefs.getInt    ("dinen",      c.dinEn);
    c.aiEn        = prefs.getInt    ("aien",       c.aiEn);
    c.dinWake     = prefs.getInt    ("dinwake",    c.dinWake);
    c.bmsInterval = prefs.getInt    ("bmsint",     c.bmsInterval);
    c.victronInterval = prefs.getInt("vicint",     c.victronInterval);
    c.payloadFormat = prefs.getInt  ("payfmt",     c.payloadFormat);
    c.altitudeM   = prefs.getInt    ("altitude",   c.altitudeM);
    c.debugMode   = prefs.getInt    ("debugmode",  c.debugMode);
    c.devEui      = prefs.getULong64("deveui",     0);
    c.joinEui     = prefs.getULong64("joineui",    0);
    String region = prefs.getString ("region",     c.region);
    normalizeRegionInput(region.c_str(), c.region, sizeof(c.region));
    prefs.getBytes("appkey", c.appKey, 16);
    prefs.end();
    return c;
}

void saveConfig(const Config& c)
{
    Preferences prefs;
    prefs.begin("config", false);
    prefs.putInt    ("sleep",      c.sleepSec);
    prefs.putInt    ("sample",     c.samplingMs);
    prefs.putInt    ("sendint",    c.continuousSendSec);
    prefs.putInt    ("kfactor",    c.kFactor);
    prefs.putInt    ("dispcyc",    c.displayOnCycles);
    prefs.putInt    ("cpufreq",    c.cpuFreqMhz);
    prefs.putInt    ("bmsmode",    c.bmsMode);
    prefs.putInt    ("victornen",  c.victronEn);
    prefs.putInt    ("highsideen", c.highsideEn);
    prefs.putInt    ("levelen",    c.levelEn);
    prefs.putInt    ("flowen",     c.flowEn);
    prefs.putInt    ("p2mode",     c.pulse2Mode);
    prefs.putInt    ("kfactor2",   c.kFactor2);
    prefs.putInt    ("dinen",      c.dinEn);
    prefs.putInt    ("aien",       c.aiEn);
    prefs.putInt    ("dinwake",    c.dinWake);
    prefs.putInt    ("bmsint",     c.bmsInterval);
    prefs.putInt    ("vicint",     c.victronInterval);
    prefs.putInt    ("payfmt",     c.payloadFormat);
    prefs.putInt    ("altitude",   c.altitudeM);
    prefs.putInt    ("debugmode",  c.debugMode);
    prefs.putULong64("deveui",     c.devEui);
    prefs.putULong64("joineui",    c.joinEui);
    prefs.putString ("region",     c.region);
    prefs.putBytes  ("appkey",     c.appKey, 16);
    prefs.end();
}
