// Device configuration (NVS namespace "config"), LoRaWAN region helpers and
// credential parsing.
#pragma once
#include <Arduino.h>
#include <RadioLib.h>

// Feature defaults for a fresh (empty-NVS) device; override via build_flags
#ifndef DEFAULT_VICTRON_EN
  #define DEFAULT_VICTRON_EN  1
#endif
#ifndef DEFAULT_BMS_MODE
  #define DEFAULT_BMS_MODE    1   // 0=off, 1=DALY, 2=LiPo
#endif
#ifndef DEFAULT_HIGHSIDE_EN
  #define DEFAULT_HIGHSIDE_EN 1
#endif
#ifndef DEFAULT_LEVEL_EN
  #define DEFAULT_LEVEL_EN    1
#endif
#ifndef DEFAULT_FLOW_EN
  #define DEFAULT_FLOW_EN     1
#endif
#ifndef DEFAULT_PULSE2_MODE
  #define DEFAULT_PULSE2_MODE 0   // 0=off, 1=2nd flow sensor, 2=counter
#endif
#ifndef DEFAULT_DIN_EN
  #define DEFAULT_DIN_EN      0   // digital inputs IN1-IN3 (off by default like every I/O function)
#endif
#ifndef DEFAULT_AI_EN
  #define DEFAULT_AI_EN       0   // 0-10 V inputs AI1/AI2
#endif

// ============================================================================
// CONFIGURATION
// ============================================================================
struct Config {
    uint64_t devEui        = 0;
    uint64_t joinEui       = 0;
    uint8_t  appKey[16]    = {};
    char     region[8]     = "EU868";
    int      sleepSec      = 300;
    int      samplingMs    = 15000;
    int      continuousSendSec = 300;
    int      kFactor       = 450;
    // Continuous mode only (sleepSec==0): number of measurement cycles the
    // display stays on after a short EXT0 button press (or after boot).
    // 0 = always on (no auto-off, no countdown).
    // In deep-sleep mode (sleepSec>0) the display is always off on a normal
    // timer wakeup and always on for a button (EXT0) wakeup or a fresh boot —
    // that behavior is automatic and not configurable.
    int      displayOnCycles = 5;
    int      cpuFreqMhz    = 80;
    int      bmsMode       = DEFAULT_BMS_MODE;    // 0=off, 1=DALY BMS, 2=VIN voltage (ADS1115)
    int      victronEn     = DEFAULT_VICTRON_EN;  // 1=Victron VE.Direct enabled
    int      highsideEn    = DEFAULT_HIGHSIDE_EN; // 1=high-side switch / 24V DC-DC enabled
    int      levelEn       = DEFAULT_LEVEL_EN;    // 1=4-20mA level sensor enabled
    int      flowEn        = DEFAULT_FLOW_EN;     // 1=flow sensor enabled
    int      pulse2Mode    = DEFAULT_PULSE2_MODE; // J4 OC2: 0=off, 1=2nd flow sensor (L/min), 2=pulse counter
    int      kFactor2      = 450;                 // pulses/L of the 2nd flow sensor (pulse2Mode 1)
    int      dinEn         = DEFAULT_DIN_EN;      // 1=send digital inputs IN1-IN3 (J18)
    int      aiEn          = DEFAULT_AI_EN;       // 1=send 0-10 V inputs AI1/AI2 (J18)
    int      dinWake       = 0;                   // wake from deep sleep when IN1-IN3 change, bit 0 = IN1 (JP9 at 1-2)
    int      bmsInterval   = 6;                   // read DALY every N cycles
    int      victronInterval = 1;                 // send Victron telemetry every N cycles (1=every cycle)
    int      payloadFormat = 0;                   // 0=CayenneLPP (FPort 1), 1=custom binary (FPort 3)
    int      altitudeM     = 0;                   // altitude above sea level in meters for QNH calculation
    int      debugMode     = 0;                   // 1 = include extra debug telemetry
    // DS18B20 (1-Wire) and BME280 are always auto-detected; no config flags needed.
};

void normalizeRegionInput(const char* raw, char* out, size_t outSize);
void codeToRegion(int code, char* out, size_t outSize);
const LoRaWANBand_t* regionToBand(const char* region);

bool parseHexBytes(const char* in, uint8_t* out, size_t nBytes);
bool parseEui(const char* in, uint64_t& out);

Config loadConfig();
void   saveConfig(const Config& c);
