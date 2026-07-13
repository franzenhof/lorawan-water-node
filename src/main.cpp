// ============================================================================
// LORAWAN WATER NODE FOR CHIRPSTACK (HELTEC V3)
//
// Always present:    Flow (open-collector), 1-Wire (DS18B20 x3), BME280, LoRaWAN
// Runtime flags:     victronEn, bmsMode, highsideEn, levelEn, flowEn  (via WiFi portal)
// Compile defaults:  DEFAULT_VICTRON_EN / DEFAULT_BMS_MODE /
//                    DEFAULT_HIGHSIDE_EN / DEFAULT_LEVEL_EN  (via platformio.ini)
// ============================================================================

// This flag MUST be placed before heltec_unofficial
#define HELTEC_LORA_MODE 1

#include <Arduino.h>
#include <heltec_unofficial.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <CayenneLPP.h>
#include <sys/time.h>
#include <VeDirectFrameHandler.h>
#include <daly-bms-uart.h>
#include <esp_task_wdt.h>  // Hardware-Watchdog
#include <esp_netif.h>
#include <esp_event.h>
#include <ArduinoOTA.h>
#include <new>
#include "generated_version.h"
#include "decoder_js.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

// ============================================================================
// PCNT DRIVER (ESP-IDF v4 vs v5)
// ============================================================================
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  #include <driver/pulse_cnt.h>
  static pcnt_unit_handle_t    pcnt_unit = nullptr;
  static pcnt_channel_handle_t pcnt_chan = nullptr;
#else
  #include <driver/pcnt.h>
  constexpr pcnt_unit_t PCNT_PORT_UNIT = PCNT_UNIT_0;
#endif

// ============================================================================
// PIN ASSIGNMENT
// ============================================================================
constexpr int PIN_DALY_TRIGGER     =  1;  // DALY BMS S1 wakeup (via PC817, inverted)
constexpr int PIN_ADC_SENSOR       =  2;  // 4-20 mA level sensor (shunt R4 = 120 Ohm)
constexpr int PIN_ONE_WIRE         =  3;  // DS18B20 1-Wire bus
constexpr int PIN_VICTRON_RX       =  4;  // VE.Direct UART RX
constexpr int PIN_VICTRON_TX       =  5;  // VE.Direct UART TX
constexpr int PIN_DALY_RX          =  6;  // DALY BMS UART RX
constexpr int PIN_DALY_TX          =  7;  // DALY BMS UART TX
constexpr int PIN_OPTO_SWITCH      = 38;  // VCC3.3 switched + 5V flow sensor rail:
                                          //   GPIO38 → LP0701N3(1) gate (direct, P-channel): LOW=on, HIGH=off
                                          //   VCC3.3_SW → VN2222L gate → LP0701N3(2) gate → flow sensor 5V rail
                                          //   (VN2222L re-inverts so LP0701N3(2) also turns on when GPIO38 LOW)
constexpr int PIN_I2C_SDA          = 41;  // BME280
constexpr int PIN_I2C_SCL          = 42;  // BME280
constexpr int PIN_HIGH_SIDE_SWITCH = 47;  // HS switch: VN2222L->FQP27P06, HIGH=on
constexpr int PIN_FLOW_SENSOR      = 48;  // flow sensor pulses via HCPL-260L
constexpr int PIN_USER_BUTTON      =  0;  // After boot: hold 3 s → config portal; during sleep: EXT0 wakeup

// PC817 inverted: GPIO HIGH -> LED conducts -> S1 pulled LOW -> BMS wakes up
constexpr int DALY_S1_ACTIVE   = HIGH;
constexpr int DALY_S1_INACTIVE = LOW;

// ============================================================================
// CONSTANTS
// ============================================================================
constexpr uint32_t WAKEUP_DELAY_MS      = 2000;
constexpr uint32_t DALY_WAKEUP_PULSE_MS =  200;
constexpr float    ADC_SHUNT_OHM        = 120.0f;  // R4 = 120 Ohm (max 20mA -> 2.4V)

// Default values from platformio.ini (fallback if not set)
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
    int      bmsMode       = DEFAULT_BMS_MODE;    // 0=off, 1=DALY BMS, 2=LiPo ADC (heltec_vbat)
    int      victronEn     = DEFAULT_VICTRON_EN;  // 1=Victron VE.Direct enabled
    int      highsideEn    = DEFAULT_HIGHSIDE_EN; // 1=high-side switch / 24V DC-DC enabled
    int      levelEn       = DEFAULT_LEVEL_EN;    // 1=4-20mA level sensor enabled
    int      flowEn        = DEFAULT_FLOW_EN;     // 1=flow sensor enabled
    int      bmsInterval   = 6;                   // read DALY every N cycles
    int      victronInterval = 1;                 // send Victron telemetry every N cycles (1=every cycle)
    int      payloadFormat = 0;                   // 0=CayenneLPP (FPort 1), 1=custom binary (FPort 3)
    int      altitudeM     = 0;                   // altitude above sea level in meters for QNH calculation
    int      debugMode     = 0;                   // 1 = include extra debug telemetry
    // DS18B20 (1-Wire) and BME280 are always auto-detected; no config flags needed.
};

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

static void normalizeRegionInput(const char* raw, char* out, size_t outSize)
{
    String region = raw ? String(raw) : String("");
    region.trim();
    region.toUpperCase();
    region.replace(" ", "");
    if (!isSupportedRegion(region)) region = "EU868";
    strlcpy(out, region.c_str(), outSize);
}

static void codeToRegion(int code, char* out, size_t outSize)
{
    if (code < 0 || code >= (int)REGION_CODES_COUNT) {
        strlcpy(out, "EU868", outSize);
        return;
    }
    strlcpy(out, REGION_CODES[code], outSize);
}

static const LoRaWANBand_t* regionToBand(const char* region)
{
    if (strcmp(region, "US915") == 0) return &US915;
    if (strcmp(region, "AU915") == 0) return &AU915;
    if (strcmp(region, "AS923") == 0) return &AS923;
    if (strcmp(region, "IN865") == 0) return &IN865;
    if (strcmp(region, "KR920") == 0) return &KR920;
    if (strcmp(region, "CN470") == 0) return &CN470;
    return &EU868;
}

// ============================================================================
// GLOBALS
// ============================================================================
alignas(LoRaWANNode) static uint8_t nodeStorage[sizeof(LoRaWANNode)];
LoRaWANNode*         node = nullptr;
TwoWire              CustomI2C = TwoWire(1);
Adafruit_BME280      bme;
OneWire              oneWire(PIN_ONE_WIRE);
DallasTemperature    sensors(&oneWire);
CayenneLPP        lpp(100);  // 75 bytes max for full configuration (DR3/SF9 required)

// ---- Custom binary payload (FPort 3) --------------------------------------
// Layout:  [0] = protocol version
//          [1..4] = 32-bit field-presence bitmask, little-endian
//                   (bit i lives in byte[1 + i/8], LSB first)
//          [5..N] = data for each present field, in ascending bit-index order
// Values mirror CayenneLPP resolution so both formats carry identical data:
//   analog int16 x100 · temperature int16 x10 · humidity uint8 x2 ·
//   barometric uint16 x10 · diag/counters uint8 · Victron CS uint8 (raw).
#define BIN_PROTO_VERSION 0x02
enum BinField {
    BF_LEVEL = 0, BF_FLOW, BF_TEMP1, BF_TEMP2, BF_TEMP3,
    BF_BME_TEMP, BF_BME_HUM, BF_BME_PRES, BF_BATT_STATUS,
    BF_VIC_V, BF_VIC_P, BF_VIC_I, BF_VIC_CS, BF_VIC_YIELD,
    BF_DALY_V, BF_DALY_SOC, BF_DALY_I, BF_DALY_TEMP, BF_DALY_CELLDIFF,
    BF_DIAG, BF_DBG_CYCLE, BF_DBG_FAIL, BF_DBG_HEAP, BF_DBG_UPTIME,
    BF_DBG_DL_RSSI, BF_DBG_DL_SNR, BF_DBG_LINK_MARGIN, BF_DBG_LINK_GW
};
static uint8_t binBuf[64];
static size_t  binLen = 0;

// User-button interrupt: an ISR latches a press so the main loop can react even
// while blocked (measurement window, LoRaWAN TX, end-of-cycle delay). Used only
// while the CPU is awake; deep-sleep button wake stays on the EXT0 source.
volatile bool     g_btnPressed   = false;
volatile uint32_t g_btnLastIsrMs = 0;
void IRAM_ATTR onButtonISR() {
    uint32_t now = millis();
    if (now - g_btnLastIsrMs > 50) {  // debounce: ignore mechanical bounce
        g_btnPressed   = true;
        g_btnLastIsrMs = now;
    }
}

// RTC RAM: survives deep sleep, reset on power loss
RTC_DATA_ATTR static uint8_t  lorawanSession[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];  // RadioLib session state
RTC_DATA_ATTR static bool     sessionSaved  = false;
RTC_DATA_ATTR static bool     forceConfigPortal = false;
RTC_DATA_ATTR static bool     lastBootWasWDT = false;
RTC_DATA_ATTR static uint8_t  debugCycleCounter = 0;
RTC_DATA_ATTR static uint8_t  debugSendFailCounter = 0;
RTC_DATA_ATTR static int      bmsCycleCounter = 0;   // cycles since last BMS read (survives deep sleep, resets on WDT/power loss)
RTC_DATA_ATTR static int      victronCycleCounter = 0;  // cycles since last Victron telemetry send (survives deep sleep)
RTC_DATA_ATTR static bool     rfHasDlQuality = false;
RTC_DATA_ATTR static bool     rfHasLinkCheck = false;
RTC_DATA_ATTR static float    rfLastDlRssi = 0.0f;       // dBm
RTC_DATA_ATTR static float    rfLastDlSnr = 0.0f;        // dB
RTC_DATA_ATTR static uint8_t  rfLastLinkMargin = 0;      // dB
RTC_DATA_ATTR static uint8_t  rfLastLinkGwCnt = 0;       // number of gateways
RTC_DATA_ATTR static uint8_t  rfLinkCheckCounter = 0;    // request LinkCheck every N uplinks
RTC_DATA_ATTR static struct {
    uint16_t cycleCount;    // cycles since last daily uplink
    float    totalLiters;   // total volume today (L)
    float    maxFlowLMin;   // maximum flow rate (L/min)
    float    minTempC;      // minimum temperature (°C, DS18B20[0])
    float    maxTempC;      // maximum temperature (°C, DS18B20[0])
} daily = { 0, 0.0f, 0.0f, 100.0f, -100.0f };
VeDirectFrameHandler victronParser;
Daly_BMS_UART        bmsParser(Serial2);

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================
static uint64_t hexToUint64(const char* s) { return strtoull(s, nullptr, 16); }

static void initNodeForRegion(const char* region)
{
    const LoRaWANBand_t* band = regionToBand(region);
    node = new (nodeStorage) LoRaWANNode(&radio, band);
}

static void hexToBytes(const char* s, uint8_t* out, size_t maxLen)
{
    for (size_t i = 0; i < strlen(s) && (i / 2) < maxLen; i += 2) {
        char b[] = { s[i], s[i + 1], '\0' };
        out[i / 2] = (uint8_t)strtol(b, nullptr, 16);
    }
}

// ============================================================================
// CONFIGURATION: NVS READ / WRITE
// ============================================================================
static Config loadConfig()
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

static void saveConfig(const Config& c)
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

// ============================================================================
// WIFI CONFIGURATION PORTAL
// ============================================================================
static Config runConfigPortal(const Config& cur)
{
    setCpuFrequencyMhz(240);
    display.displayOn();
    display.clear();
    display.drawString(0, 0, "Starting config portal...");
    display.display();

    char b_sleep[12], b_sample[12], b_send[12], b_k[12], b_dispcyc[6], b_cpu[6], b_region[8], b_debug[4];
    char b_bms[4], b_vic[4], b_hs[4], b_lv[4], b_int[6], b_vicint[6], b_fl[4], b_alt[6];
    char b_payfmt[4];
    itoa(cur.sleepSec,    b_sleep,  10);
    itoa(cur.samplingMs,  b_sample, 10);
    itoa(cur.continuousSendSec, b_send, 10);
    itoa(cur.kFactor,     b_k,      10);
    itoa(cur.displayOnCycles, b_dispcyc, 10);
    itoa(cur.cpuFreqMhz,  b_cpu,    10);
    itoa(cur.bmsMode,     b_bms,    10);
    itoa(cur.victronEn,   b_vic,    10);
    itoa(cur.highsideEn,  b_hs,     10);
    itoa(cur.levelEn,     b_lv,     10);
    itoa(cur.bmsInterval, b_int,    10);
    itoa(cur.victronInterval, b_vicint, 10);
    itoa(cur.payloadFormat, b_payfmt, 10);
    itoa(cur.flowEn,      b_fl,     10);
    itoa(cur.altitudeM,   b_alt,    10);
    itoa(cur.debugMode,   b_debug,  10);
    strlcpy(b_region, cur.region, sizeof(b_region));

    // Pre-fill LoRaWAN credential fields with current values so they are
    // visible and survive a portal re-open without re-entry.
    char b_dev[17]  = "";
    char b_join[17] = "";
    char b_key[33]  = "";
    if (cur.devEui  != 0) snprintf(b_dev,  sizeof(b_dev),  "%016llX", (unsigned long long)cur.devEui);
    if (cur.joinEui != 0) snprintf(b_join, sizeof(b_join), "%016llX", (unsigned long long)cur.joinEui);
    {
        bool anySet = false;
        for (int i = 0; i < 16; i++) if (cur.appKey[i]) { anySet = true; break; }
        if (anySet)
            for (int i = 0; i < 16; i++) snprintf(b_key + i * 2, 3, "%02X", cur.appKey[i]);
    }

    WiFiManagerParameter p_dev  ("dev",  "DevEUI (16 Hex)",                   b_dev,   17);
    WiFiManagerParameter p_join ("join", "JoinEUI (16 Hex)",                  b_join,  17);
    WiFiManagerParameter p_key  ("key",  "AppKey (32 Hex)",                   b_key,   33);
    WiFiManagerParameter p_reg  ("region", "LoRaWAN region (EU868,US915,AU915,AS923,IN865,KR920,CN470)", b_region, 8);
    WiFiManagerParameter p_sleep("sleep",   "Deep Sleep time (s) [0=continuous mode]", b_sleep,   6);
    WiFiManagerParameter p_timing("<div style='margin:0.35rem 0 0.6rem 0;padding:0.45rem;border:1px solid #ccc;border-radius:6px;'><strong>Timing note:</strong><br/>sleep &gt; 0: <code>sample</code> is used (measurement window in ms).<br/>sleep = 0: <code>sendint</code> is used (continuous cycle interval in s).</div>");
    WiFiManagerParameter p_samp ("sample",  "Sampling window (ms, only if sleep>0)", b_sample,  6);
    WiFiManagerParameter p_send ("sendint", "Continuous interval (s, only if sleep=0)", b_send,  6);
    WiFiManagerParameter p_k    ("kfactor", "K-factor (pulses/L)",               b_k,       6);
    WiFiManagerParameter p_dispcyc("dispcyc","Display-on cycles after button press (0=always on, sleep=0 only)", b_dispcyc, 6);
    WiFiManagerParameter p_cpu  ("cpufreq", "CPU (80/240 MHz)",                  b_cpu,     4);
    WiFiManagerParameter p_bms  ("bmsmode", "BMS (0=off, 1=DALY, 2=LiPo)",      b_bms,     2);
    WiFiManagerParameter p_vic  ("victornen","Victron (1=Yes)",                  b_vic,     2);
    WiFiManagerParameter p_hs   ("highsideen","HighSide/24V (1=Yes)",            b_hs,      2);
    WiFiManagerParameter p_lv   ("levelen", "4-20mA Sensor (1=Yes)",            b_lv,      2);
    WiFiManagerParameter p_fl   ("flowen",  "Flow sensor (open-collector, 1=Yes)", b_fl,      2);
    WiFiManagerParameter p_int  ("bmsint",  "DALY interval (cycles)",            b_int,     4);
    WiFiManagerParameter p_vicint("vicint", "Victron interval (cycles, 1=every)", b_vicint,  4);
    WiFiManagerParameter p_payfmt("payfmt", "Payload format (0=CayenneLPP, 1=binary)", b_payfmt, 2);
    WiFiManagerParameter p_alt  ("altitude","Altitude above sea level (m, QNH)", b_alt,     5);
    WiFiManagerParameter p_dbg  ("debugmode","Debug mode (0=No, 1=Yes)",         b_debug,   2);
    WiFiManagerParameter p_section("<div style='margin:0.5rem 0 0.75rem 0;'><h3>LoRa-Water-Node Parameter</h3><p style='margin:0 0 0.35rem 0;'>Firmware version: " FIRMWARE_VERSION "</p><p style='margin:0;'>Device settings are available on a separate page.</p></div>");

    WiFiManager wm;
    const char* menu[] = { "wifi", "info", "custom", "sep", "exit" };
    wm.setMenu(menu, 5);
    wm.setCustomMenuHTML("<div style='margin:0.75rem 0 1rem 0;'><div style='font-size:0.85rem;opacity:0.75;margin:0 0 0.25rem 0;'>Firmware version: " FIRMWARE_VERSION "</div><form action='/param' method='get'><button>LoRa-Water-Node Parameter</button></form><br/><form action='/decoder' method='get'><button>Payload decoder (JS)</button></form><br/><form action='/update' method='get'><button>Firmwareupdate</button></form></div>");

    // Register a custom /decoder route that serves the version-matched payload
    // decoder (embedded from chirpstack/decoder.js) as a downloadable JS file.
    wm.setWebServerCallback([&wm]() {
        wm.server->on("/decoder", [&wm]() {
            wm.server->sendHeader("Content-Disposition",
                                  "attachment; filename=\"water-node-decoder.js\"");
            wm.server->send_P(200, "application/javascript; charset=utf-8", DECODER_JS);
        });
    });

    wm.addParameter(&p_section);
    wm.addParameter(&p_dev);   wm.addParameter(&p_join);  wm.addParameter(&p_key); wm.addParameter(&p_reg);
    wm.addParameter(&p_sleep); wm.addParameter(&p_timing); wm.addParameter(&p_samp);  wm.addParameter(&p_send); wm.addParameter(&p_k);
    wm.addParameter(&p_dispcyc); wm.addParameter(&p_cpu);
    wm.addParameter(&p_bms);   wm.addParameter(&p_vic);
    wm.addParameter(&p_hs);    wm.addParameter(&p_lv);
    wm.addParameter(&p_fl);
    wm.addParameter(&p_int);   wm.addParameter(&p_vicint);
    wm.addParameter(&p_payfmt);
    wm.addParameter(&p_alt);   wm.addParameter(&p_dbg);
    wm.setConfigPortalTimeout(180);  // 3 minutes, then restart

    // Update display once the AP is up and the IP address is known
    wm.setAPCallback([](WiFiManager*) {
        String ip = WiFi.softAPIP().toString();
        display.clear();
        display.drawString(0,  0, "Config Portal");
        display.drawString(0, 11, "SSID: WaterNode");
        display.drawString(0, 22, "http://" + ip);
        display.drawString(0, 33, "OTA:  water-node");
        display.drawString(0, 44, "1. Connect to SSID");
        display.drawString(0, 55, "2. Open URL in browser");
        display.display();
    });

    // Ensure the TCP/IP stack and default event loop are initialized before
    // WiFiManager starts.  Safe to call even if already initialized –
    // ESP_ERR_INVALID_STATE is intentionally ignored.
    esp_netif_init();
    esp_event_loop_create_default();

    // ArduinoOTA running in parallel to the portal in its own task (firmware update without USB)
    ArduinoOTA.setHostname("water-node");
    ArduinoOTA.onStart([]()  { Serial.println("OTA: Start"); });
    ArduinoOTA.onEnd([]()    { Serial.println("OTA: Done"); });
    ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA Error %u\n", e); });
    ArduinoOTA.begin();
    TaskHandle_t otaHandle = nullptr;
    xTaskCreate([](void*) {
        for (;;) { ArduinoOTA.handle(); vTaskDelay(10 / portTICK_PERIOD_MS); }
    }, "OTA", 4096, nullptr, 1, &otaHandle);

    wm.startConfigPortal("WaterNode");

    if (otaHandle) { vTaskDelete(otaHandle); ArduinoOTA.end(); }

    Config c = cur;  // start from current values; preserves anything the user didn't touch
    c.sleepSec    = atoi(p_sleep.getValue());
    c.samplingMs  = atoi(p_samp.getValue());
    c.continuousSendSec = atoi(p_send.getValue());
    c.kFactor     = atoi(p_k.getValue());
    normalizeRegionInput(p_reg.getValue(), c.region, sizeof(c.region));
    c.displayOnCycles = atoi(p_dispcyc.getValue());
    c.cpuFreqMhz  = atoi(p_cpu.getValue());
    c.bmsMode     = atoi(p_bms.getValue());
    c.victronEn   = atoi(p_vic.getValue());
    c.highsideEn  = atoi(p_hs.getValue());
    c.levelEn     = atoi(p_lv.getValue());
    c.flowEn      = atoi(p_fl.getValue());
    c.bmsInterval = atoi(p_int.getValue());
    c.victronInterval = atoi(p_vicint.getValue());
    c.payloadFormat = (atoi(p_payfmt.getValue()) == 1) ? 1 : 0;
    c.altitudeM   = atoi(p_alt.getValue());
    c.debugMode   = (atoi(p_dbg.getValue()) == 1) ? 1 : 0;
    // Only update credentials when the user actually typed something valid;
    // an empty or too-short field keeps the previously stored value.
    if (strlen(p_dev.getValue())  >= 16) c.devEui  = hexToUint64(p_dev.getValue());
    if (strlen(p_join.getValue()) >= 16) c.joinEui = hexToUint64(p_join.getValue());
    if (strlen(p_key.getValue())  >= 32) hexToBytes(p_key.getValue(), c.appKey, 16);
    // Validate inputs – prevents division by zero and infinite loops
    c.kFactor     = max(1,    c.kFactor);
    c.sleepSec    = (c.sleepSec == 0) ? 0 : max(10, c.sleepSec);  // 0=continuous, otherwise min 10s
    c.samplingMs  = max(1000, c.samplingMs);
    c.continuousSendSec = max(10, c.continuousSendSec);
    c.altitudeM   = max(0, min(9999, c.altitudeM));
    c.cpuFreqMhz  = (c.cpuFreqMhz >= 160) ? 240 : 80;
    c.bmsInterval = max(1, c.bmsInterval);
    c.victronInterval = max(1, c.victronInterval);
    c.displayOnCycles = max(0, min(1000, c.displayOnCycles));  // 0 = always on
    return c;
}

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
        default:
            return false;
    }
}

static bool processParamDownlink(Config& cfg, const uint8_t* buf, size_t len, bool& requiresRestart)
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

// ============================================================================
// PCNT ABSTRACTION (ESP-IDF v4 / v5)
// ============================================================================
static void pcnt_init()
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    pcnt_unit_config_t unit_cfg = { .low_limit = -32768, .high_limit = 32767,
                                    .flags = { .accum_count = 1 } };
    if (pcnt_new_unit(&unit_cfg, &pcnt_unit) != ESP_OK) return;
    pcnt_chan_config_t chan_cfg = { .edge_gpio_num = PIN_FLOW_SENSOR, .level_gpio_num = -1 };
    if (pcnt_new_channel(pcnt_unit, &chan_cfg, &pcnt_chan) != ESP_OK) return;
    pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                            PCNT_CHANNEL_EDGE_ACTION_HOLD);
    pcnt_glitch_filter_config_t filter_cfg = { .max_glitch_ns = 12500 };
    pcnt_unit_set_glitch_filter(pcnt_unit, &filter_cfg);
    pcnt_unit_enable(pcnt_unit);
    pcnt_unit_clear_count(pcnt_unit);
    pcnt_unit_start(pcnt_unit);
#else
    pcnt_config_t cfg = {
        .pulse_gpio_num = PIN_FLOW_SENSOR, .ctrl_gpio_num = PCNT_PIN_NOT_USED,
        .lctrl_mode = PCNT_MODE_KEEP,      .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_INC,        .neg_mode = PCNT_COUNT_DIS,
        .counter_h_lim = 32767,            .counter_l_lim = -32768,
        .unit = PCNT_PORT_UNIT,            .channel = PCNT_CHANNEL_0
    };
    pcnt_unit_config(&cfg);
    pcnt_set_filter_value(PCNT_PORT_UNIT, 1000);
    pcnt_filter_enable(PCNT_PORT_UNIT);
    pcnt_counter_clear(PCNT_PORT_UNIT);
    pcnt_counter_resume(PCNT_PORT_UNIT);
#endif
}

// Non-destructive read: returns the current pulse count without stopping the counter
// (used for live display updates during the measurement window).
static int pcnt_read_peek()
{
    int count = 0;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    pcnt_unit_get_count(pcnt_unit, &count);
#else
    int16_t v = 0;
    pcnt_get_counter_value(PCNT_PORT_UNIT, &v);
    count = v;
#endif
    return count;
}

// Clears the pulse counter without stopping it. Used in continuous mode to
// define seamless measurement windows while counting stays active.
static void pcnt_clear_keep_running()
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    pcnt_unit_clear_count(pcnt_unit);
#else
    pcnt_counter_clear(PCNT_PORT_UNIT);
#endif
}

static int pcnt_read_and_stop()
{
    int count = 0;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    pcnt_unit_get_count(pcnt_unit, &count);
    pcnt_unit_stop(pcnt_unit);
#else
    int16_t v = 0;
    pcnt_get_counter_value(PCNT_PORT_UNIT, &v);
    pcnt_counter_pause(PCNT_PORT_UNIT);
    count = v;
#endif
    return count;
}

// ============================================================================
// SETUP
// ============================================================================
void setup()
{
    esp_reset_reason_t resetReason = esp_reset_reason();
    lastBootWasWDT = (resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT);

    // WDT: 120s timeout (LoRaWAN join + measurement + send; resets on hang)
    esp_task_wdt_init(120, true);
    esp_task_wdt_add(nullptr);

    Config cfg;
    bool showDisplay = true;
    esp_sleep_wakeup_cause_t wakeup;

    cfg = loadConfig();
    initNodeForRegion(cfg.region);
    setCpuFrequencyMhz(cfg.cpuFreqMhz);
    // Enable VEXT (GPIO36, active-LOW) before heltec_setup() so the display
    // already has power when the RST pulse is issued.
    heltec_ve(true);
    heltec_setup();
    wakeup = esp_sleep_get_wakeup_cause();

    // Automatic display power management (deep-sleep mode, sleepSec>0):
    // OFF on a normal timer wakeup, ON for a button (EXT0) wakeup or a fresh
    // boot. Not user-configurable — always the sensible choice for an
    // unattended battery-powered node. (Continuous mode, sleepSec==0, uses
    // its own cycle-based on/off logic further below.)
    showDisplay = (wakeup != ESP_SLEEP_WAKEUP_TIMER);
    showDisplay ? display.displayOn() : display.displayOff();

    // GPIO0 LOW during power-on triggers the bootloader, so the button cannot
    // be held at startup to open the config portal.  Instead, a 4-second hint
    // window is shown after boot; holding the button for 3 s within that
    // window opens the portal.
    // On EXT0 wakeup (button pressed during sleep) the hint is skipped and a
    // normal measurement cycle runs with the display enabled.
    bool freshBoot = (wakeup != ESP_SLEEP_WAKEUP_TIMER &&
                      wakeup != ESP_SLEEP_WAKEUP_EXT0);
    bool portalViaButton = false;

    if (freshBoot && cfg.devEui != 0 && !forceConfigPortal) {
        display.displayOn();
        display.clear();
        display.drawString(0,  0, "WaterNode");
        display.drawString(0, 16, "Hold 3s: Config");
        display.display();

        // 4-second window: holding the button for 3 s opens the config portal
        unsigned long windowStart = millis();
        unsigned long pressStart  = 0;
        while (millis() - windowStart < 4000) {
            if (digitalRead(PIN_USER_BUTTON) == LOW) {
                if (pressStart == 0) pressStart = millis();
                if (millis() - pressStart >= 3000) {
                    portalViaButton = true;
                    break;
                }
            } else {
                pressStart = 0;  // reset on release
            }
            delay(50);
        }
    }

    if (cfg.devEui == 0 || forceConfigPortal || portalViaButton) {
        showDisplay = true;
        display.displayOn();
        forceConfigPortal = false;
        display.clear();
        display.drawString(0, 0, "Starting config portal...");
        display.display();
        cfg = runConfigPortal(cfg);
        saveConfig(cfg);
        display.clear();
        display.drawString(0,  0, "Configuration saved!");
        display.drawString(0, 16, "Restarting...");
        display.display();
        delay(2000);
        ESP.restart();
    }

    if (sessionSaved &&
        (wakeup == ESP_SLEEP_WAKEUP_TIMER || wakeup == ESP_SLEEP_WAKEUP_EXT0)) {
        node->setBufferSession(lorawanSession);
    }
    node->beginOTAA(cfg.joinEui, cfg.devEui, cfg.appKey, cfg.appKey);

    // Latch user-button presses via a GPIO interrupt so the display can be
    // re-armed from any blocking section while awake (continuous mode). The
    // flag is cleared here so a press during boot/portal isn't carried in.
    pinMode(PIN_USER_BUTTON, INPUT_PULLUP);
    g_btnPressed = false;
    attachInterrupt(digitalPinToInterrupt(PIN_USER_BUTTON), onButtonISR, FALLING);

    bool buttonWakeCycle = (wakeup == ESP_SLEEP_WAKEUP_EXT0);
    bool firstCycle = true;
    bool skipSleepOnce = false;
    bool flowWindowPrimed = false;
    unsigned long flowWindowStartMs = 0;
    // Continuous mode only: number of remaining cycles the display stays on.
    // Initialized as if the boot itself were a "button press" (see showDisplay above).
    // displayOnCycles == 0 means "always on" (no countdown, no button needed).
    int remainingDisplayCycles = (cfg.sleepSec == 0 && showDisplay) ? cfg.displayOnCycles : 0;

    while (true) {
        bool runBms = false;
        bool sendVictron = false;
        bool ext0Cycle = firstCycle && buttonWakeCycle;

        // ---- Continuous mode: cycle-based display on/off ---------------
        // Deep-sleep mode already decided showDisplay once at boot (based on
        // wakeup cause) and it stays fixed for the whole cycle. In continuous
        // mode there is no deep sleep/wakeup to hook into, so the display is
        // kept on for `displayOnCycles` cycles and then turned off, until a
        // short button press re-arms it. displayOnCycles == 0 disables the
        // countdown entirely and keeps the display permanently on.
        if (cfg.sleepSec == 0) {
            if (cfg.displayOnCycles == 0) {
                showDisplay = true;
            } else {
                if (g_btnPressed) {
                    g_btnPressed = false;
                    remainingDisplayCycles = cfg.displayOnCycles;
                }
                showDisplay = (remainingDisplayCycles > 0);
                if (remainingDisplayCycles > 0) remainingDisplayCycles--;
            }
            showDisplay ? display.displayOn() : display.displayOff();
        }


        // ---- DALY BMS: interval control + S1 wakeup -------------------
        if (cfg.bmsMode == 1) {
            bmsCycleCounter++;
            if (bmsCycleCounter >= cfg.bmsInterval || ext0Cycle) {
                runBms = true;
                bmsCycleCounter = 0;
            }
            if (runBms) {
                pinMode(PIN_DALY_TRIGGER, OUTPUT);
                digitalWrite(PIN_DALY_TRIGGER, DALY_S1_ACTIVE);
                delay(DALY_WAKEUP_PULSE_MS);
                digitalWrite(PIN_DALY_TRIGGER, DALY_S1_INACTIVE);
                pinMode(PIN_DALY_TRIGGER, INPUT);
            }
        }

        // ---- Victron telemetry: interval control ----------------------
        // Victron is still read every cycle (cheap during the running
        // measurement window), but its channels are only appended to the
        // uplink every N cycles to reduce average payload size / airtime.
        // A button (EXT0) wakeup always forces a send.
        if (cfg.victronEn) {
            victronCycleCounter++;
            if (victronCycleCounter >= cfg.victronInterval || ext0Cycle) {
                sendVictron = true;
                victronCycleCounter = 0;
            }
        }

        // Release GPIO hold from deep sleep (otherwise digitalWrite has no effect!)
        gpio_hold_dis((gpio_num_t)PIN_OPTO_SWITCH);
        gpio_hold_dis((gpio_num_t)PIN_HIGH_SIDE_SWITCH);

        // ---- Power on peripherals --------------------------------------
        if (cfg.highsideEn) {
            // VN2222L->FQP27P06 (P-channel): HIGH = 24V DC-DC on (for 4-20 mA sensor)
            pinMode(PIN_HIGH_SIDE_SWITCH, OUTPUT);
            digitalWrite(PIN_HIGH_SIDE_SWITCH, HIGH);
        }

        // LP0701N3(1) (P-channel, direct drive): LOW = VCC3.3 switched on
        pinMode(PIN_OPTO_SWITCH, OUTPUT);
        digitalWrite(PIN_OPTO_SWITCH, LOW);

        if (showDisplay) {
            display.clear();
            display.drawString(0,  0, "LoRaWan-Water-Node");
            display.drawString(0, 12, "Measuring...");
            display.display();
        }

        // ---- Initialize sensors ----------------------------------------
        if (cfg.flowEn && (cfg.sleepSec > 0 || firstCycle)) pcnt_init();
        if (cfg.flowEn && cfg.sleepSec == 0 && !flowWindowPrimed) {
            pcnt_clear_keep_running();
            flowWindowStartMs = millis();
            flowWindowPrimed = true;
        }
        CustomI2C.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);

        if (cfg.victronEn)
            Serial1.begin(19200, SERIAL_8N1, PIN_VICTRON_RX, PIN_VICTRON_TX);

        if (cfg.bmsMode == 1) {
            Serial2.begin(9600, SERIAL_8N1, PIN_DALY_RX, PIN_DALY_TX);
            bmsParser.Init();
        }

        bool bmeOk = bme.begin(0x76, &CustomI2C);  // auto-detect: present if begin() returns true
        sensors.begin();
        int oneWireCount = sensors.getDeviceCount();  // 0–3, auto-detected

        delay(WAKEUP_DELAY_MS);

        // ---- Measurement window ----------------------------------------
        unsigned long measurementWindowMs = (cfg.sleepSec == 0)
                                            ? (unsigned long)cfg.continuousSendSec * 1000UL
                                            : (unsigned long)cfg.samplingMs;
        unsigned long tStart    = millis();
        int           adcCount  = 0;
        float         adcSum    = 0.0f;
        float         adcMin    =  1e9f;
        float         adcMax    = -1e9f;
        bool          victronOk = false;
        bool          bmsOk     = false;
        unsigned long lastLiveUpdate = 0;
        unsigned long lastAvgSampleMs = 0;

        // Continuous-mode averaging accumulators (window averages).
        float t1wireSum[3] = { 0.0f, 0.0f, 0.0f };
        int   t1wireSamples[3] = { 0, 0, 0 };

        float bmeTempSum = 0.0f, bmeHumSum = 0.0f, bmePresSum = 0.0f;
        int   bmeSamples = 0;

        float victronVSum = 0.0f, victronPSum = 0.0f, victronISum = 0.0f;
        int   victronVSamples = 0, victronPSamples = 0, victronISamples = 0;
        int   victronCSLatest = -1;
        float victronH20Latest = -1.0f;

        auto readVictronSnapshot = [&](float& outV, bool& hasV,
                                       float& outP, bool& hasP,
                                       float& outI, bool& hasI,
                                       int& outCS, bool& hasCS,
                                       float& outH20, bool& hasH20) {
            hasV = hasP = hasI = hasCS = hasH20 = false;
            outV = outP = outI = 0.0f;
            outCS = -1;
            outH20 = -1.0f;

            for (int i = 0; i < victronParser.veEnd; i++) {
                const char* n = victronParser.veName[i];
                const char* v = victronParser.veValue[i];
                if (strcmp(n, "V") == 0)   { outV = atof(v) / 1000.0f; hasV = true; }   // mV -> V
                if (strcmp(n, "PPV") == 0) { outP = atof(v);            hasP = true; }   // W
                if (strcmp(n, "I") == 0)   { outI = atof(v) / 1000.0f; hasI = true; }   // mA -> A
                if (strcmp(n, "CS") == 0)  { outCS = atoi(v);          hasCS = true; }   // 3=Bulk, 4=Abs, 5=Float
                if (strcmp(n, "H20") == 0) { outH20 = atof(v) / 100.0f;hasH20 = true; } // 0.01kWh -> kWh
            }
        };

        // Continuous mode: read DS18B20 asynchronously so live temperatures can
        // be shown during the (long) window without the ~750 ms blocking
        // conversion stalling ADC/serial sampling. Deep-sleep mode keeps the
        // single blocking read after the window (short window, lower power).
        constexpr unsigned long DS18_CONV_MS = 750;   // 12-bit conversion time
        float t1wireLive[3] = { DEVICE_DISCONNECTED_C, DEVICE_DISCONNECTED_C, DEVICE_DISCONNECTED_C };
        bool  ds18Async   = (cfg.sleepSec == 0 && oneWireCount > 0);
        bool  ds18Pending = false;
        unsigned long ds18ReqMs = 0;
        int flowDisplayStartCount = (cfg.flowEn && cfg.sleepSec == 0) ? pcnt_read_peek() : 0;
        if (ds18Async) {
            sensors.setWaitForConversion(false);  // non-blocking convert
            sensors.requestTemperatures();        // kick off first conversion
            ds18ReqMs   = millis();
            ds18Pending = true;
        }

        while (millis() - tStart < measurementWindowMs) {
            float adcMv = analogReadMilliVolts(PIN_ADC_SENSOR);  // calibrated, result in mV
            adcSum += adcMv;
            adcCount++;
            if (adcMv < adcMin) adcMin = adcMv;
            if (adcMv > adcMax) adcMax = adcMv;

            if (cfg.victronEn) {
                while (Serial1.available())
                    victronParser.rxData(Serial1.read());
                if (!victronOk && victronParser.veEnd > 0)
                    victronOk = true;
            }

            if (runBms && !bmsOk && bmsParser.update())
                bmsOk = true;

            // Continuous mode: a button press (latched by the ISR) re-arms the
            // display immediately, even in the middle of the measurement window.
            if (cfg.sleepSec == 0 && cfg.displayOnCycles != 0 && g_btnPressed) {
                g_btnPressed = false;
                remainingDisplayCycles = cfg.displayOnCycles;
                if (!showDisplay) { showDisplay = true; display.displayOn(); }
            }

            // Continuous mode: collect a completed async DS18B20 conversion and
            // immediately start the next one, so live temps stay fresh.
            if (ds18Async && ds18Pending && (millis() - ds18ReqMs) >= DS18_CONV_MS) {
                for (int i = 0; i < min(oneWireCount, 3); i++) {
                    t1wireLive[i] = sensors.getTempCByIndex(i);
                    if (t1wireLive[i] != DEVICE_DISCONNECTED_C) {
                        t1wireSum[i] += t1wireLive[i];
                        t1wireSamples[i]++;
                    }
                }
                sensors.requestTemperatures();  // start next conversion
                ds18ReqMs = millis();
            }

            // Continuous mode: sample slowly-changing telemetry once per second
            // and average over the whole measurement window.
            unsigned long nowMs = millis();
            if (cfg.sleepSec == 0 && (nowMs - lastAvgSampleMs) >= 1000) {
                lastAvgSampleMs = nowMs;

                if (bmeOk) {
                    float sTemp = bme.readTemperature();
                    float sHum  = bme.readHumidity();
                    float sPres = bme.seaLevelForAltitude(cfg.altitudeM, bme.readPressure()) / 100.0f;
                    bmeTempSum += sTemp;
                    bmeHumSum  += sHum;
                    bmePresSum += sPres;
                    bmeSamples++;
                }

                if (cfg.victronEn && victronOk) {
                    float sV, sP, sI, sH20;
                    int sCS;
                    bool hasV, hasP, hasI, hasCS, hasH20;
                    readVictronSnapshot(sV, hasV, sP, hasP, sI, hasI, sCS, hasCS, sH20, hasH20);

                    if (hasV) { victronVSum += sV; victronVSamples++; }
                    if (hasP) { victronPSum += sP; victronPSamples++; }
                    if (hasI) { victronISum += sI; victronISamples++; }
                    if (hasCS) victronCSLatest = sCS;
                    if (hasH20) victronH20Latest = sH20;
                }
            }

            // ---- Live display update (once per second) ------------------
            // Shows a countdown plus a running preview of the values being
            // sampled, so the user sees something meaningful while waiting.
            if (showDisplay && millis() - lastLiveUpdate >= 1000) {
                lastLiveUpdate = millis();
                unsigned long elapsedMs   = millis() - tStart;
                unsigned long remainingS  = (measurementWindowMs > elapsedMs)
                                            ? (measurementWindowMs - elapsedMs) / 1000 : 0;
                float elapsedSecLive = elapsedMs / 1000.0f;
                char lbuf[22];

                display.clear();
                snprintf(lbuf, sizeof(lbuf), "Measuring... %lus", remainingS);
                display.drawString(0, 0, lbuf);

                // Priority order: Flow, temperatures, Level, Victron, DALY.
                // 1) Flow
                if (cfg.flowEn && elapsedSecLive > 0.5f) {
                    int livePulses = pcnt_read_peek();
                    if (cfg.sleepSec == 0) livePulses -= flowDisplayStartCount;
                    float liveFlow = ((float)livePulses / (float)cfg.kFactor / elapsedSecLive) * 60.0f;
                    snprintf(lbuf, sizeof(lbuf), "Flow:%.1f L/min", liveFlow);
                    display.drawString(0, 11, lbuf);
                }

                // 2) DS18B20 temperatures (live async reading)
                if (oneWireCount > 0) {
                    String tLine = "T:";
                    for (int i = 0; i < min(oneWireCount, 3); i++) {
                        if (t1wireLive[i] != DEVICE_DISCONNECTED_C) {
                            char tb[8];
                            snprintf(tb, sizeof(tb), "%.1f ", t1wireLive[i]);
                            tLine += tb;
                        } else {
                            tLine += "-- ";
                        }
                    }
                    display.drawString(0, 22, tLine);
                }

                // 3) Level
                if (cfg.levelEn && adcCount > 0) {
                    float liveMa = ((adcSum / adcCount) / 1000.0f / ADC_SHUNT_OHM) * 1000.0f;
                    snprintf(lbuf, sizeof(lbuf), "Level:%.1f mA", liveMa);
                    display.drawString(0, 33, lbuf);
                }

                // 4) Victron reception status
                if (cfg.victronEn)
                    display.drawString(0, 44, victronOk ? "Victron: OK" : "Victron: ...");

                // 5) DALY BMS reception status
                if (runBms)
                    display.drawString(0, 55, bmsOk ? "BMS: OK" : "BMS: ...");

                display.display();
            }

            bool allDone = (!cfg.victronEn || victronOk) && (!runBms || bmsOk);
            // Exit early only in deep-sleep mode when serial data is available and
            // at least 5s of sampling has elapsed for a valid flow measurement.
            bool hasSerial = cfg.victronEn || runBms;
            if (cfg.sleepSec > 0 && hasSerial && allDone && (millis() - tStart) >= 5000) break;

            esp_task_wdt_reset();
            delay(10);
        }

        int   pulses     = 0;
        float elapsedSec = (millis() - tStart) / 1000.0f;
        if (cfg.flowEn) {
            if (cfg.sleepSec == 0) {
                unsigned long flowNowMs = millis();
                pulses = pcnt_read_peek();
                elapsedSec = (flowNowMs - flowWindowStartMs) / 1000.0f;
                flowWindowStartMs = flowNowMs;
                pcnt_clear_keep_running();
            } else {
                pulses = pcnt_read_and_stop();
            }
        }
        if (elapsedSec < 0.001f) elapsedSec = 0.001f;
        float flowLMin   = cfg.flowEn
                           ? ((float)pulses / (float)cfg.kFactor / elapsedSec) * 60.0f
                           : 0.0f;

    // ---- Merge measurement values (for LPP + display) ------------------
    // Discard outliers (min + max) when enough samples are available
    if (adcCount > 4) {
        adcSum   -= adcMin + adcMax;
        adcCount -= 2;
    }
    // adcSum is now in mV (thanks to analogReadMilliVolts), so /1000 for volts
    float currentMA = cfg.levelEn
                      ? ((adcSum / adcCount) / 1000.0f / ADC_SHUNT_OHM) * 1000.0f
                      : 0.0f;
    // 4-20mA plausibility check: < 3.5mA or > 20.5mA = error
    bool adcOutOfRange = cfg.levelEn && (currentMA < 3.5f || currentMA > 20.5f);

    float t1wire[3] = { DEVICE_DISCONNECTED_C, DEVICE_DISCONNECTED_C, DEVICE_DISCONNECTED_C };
    bool oneWireError = false;
    if (oneWireCount > 0) {
        if (ds18Async) {
            // Continuous mode: use window averages for DS18B20 (fall back to
            // latest live value if no valid sample was captured).
            sensors.setWaitForConversion(true);  // restore default for safety
            for (int i = 0; i < min(oneWireCount, 3); i++) {
                if (t1wireSamples[i] > 0) {
                    t1wire[i] = t1wireSum[i] / (float)t1wireSamples[i];
                } else {
                    t1wire[i] = t1wireLive[i];
                }
                if (t1wire[i] == DEVICE_DISCONNECTED_C) oneWireError = true;
            }
        } else {
            sensors.requestTemperatures();
            for (int i = 0; i < min(oneWireCount, 3); i++) {
                t1wire[i] = sensors.getTempCByIndex(i);
                if (t1wire[i] == DEVICE_DISCONNECTED_C) oneWireError = true;
            }
        }
    }

    // Accumulate daily statistics
    daily.cycleCount++;
    if (cfg.flowEn) {
        daily.totalLiters += flowLMin * (elapsedSec / 60.0f);
        if (flowLMin > daily.maxFlowLMin) daily.maxFlowLMin = flowLMin;
    }
    if (t1wire[0] != DEVICE_DISCONNECTED_C) {
        if (t1wire[0] < daily.minTempC) daily.minTempC = t1wire[0];
        if (t1wire[0] > daily.maxTempC) daily.maxTempC = t1wire[0];
    }

    float bmeTemp = 0.0f, bmeHum = 0.0f, bmePres = 0.0f;
    if (bmeOk) {
        if (cfg.sleepSec == 0 && bmeSamples > 0) {
            bmeTemp = bmeTempSum / (float)bmeSamples;
            bmeHum  = bmeHumSum  / (float)bmeSamples;
            bmePres = bmePresSum / (float)bmeSamples;
        } else {
            bmeTemp = bme.readTemperature();
            bmeHum  = bme.readHumidity();
            // seaLevelForAltitude converts absolute pressure to QNH (sea level equivalent).
            // When altitudeM=0 the result is identical to the absolute pressure.
            float rawPa = bme.readPressure();
            bmePres = bme.seaLevelForAltitude(cfg.altitudeM, rawPa) / 100.0f;  // Pa -> hPa
        }
    }

    float victronV = 0.0f, victronP = 0.0f, victronI = 0.0f;
    int   victronCS  = -1;
    float victronH20 = -1.0f;
    if (cfg.victronEn) {
        bool hasV, hasP, hasI, hasCS, hasH20;
        readVictronSnapshot(victronV, hasV, victronP, hasP, victronI, hasI,
                            victronCS, hasCS, victronH20, hasH20);

        if (!hasCS) victronCS = victronCSLatest;
        if (!hasH20) victronH20 = victronH20Latest;

        if (cfg.sleepSec == 0) {
            if (victronVSamples > 0) victronV = victronVSum / (float)victronVSamples;
            if (victronPSamples > 0) victronP = victronPSum / (float)victronPSamples;
            if (victronISamples > 0) victronI = victronISum / (float)victronISamples;
        }
    }

    // ---- Compute values shared by both payload formats -----------------
    debugCycleCounter++;

    // CH9 battery status: bmsMode 0=absent, 1=DALY status flag, 2=LiPo ADC
    bool  ch9Present = (cfg.bmsMode == 1 || cfg.bmsMode == 2);
    float ch9Value   = (cfg.bmsMode == 2) ? heltec_vbat()
                                          : (bmsOk ? 1.0f : 0.0f);

    // Diagnostic flags (bitmask)
    uint8_t statusFlags = 0;
    if (cfg.victronEn  && !victronOk)   statusFlags |= (1 << 0);  // Victron: no frame received
    if (runBms         && !bmsOk)       statusFlags |= (1 << 1);  // DALY: no response
    if (oneWireError)                   statusFlags |= (1 << 2);  // DS18B20 read error
    if (adcOutOfRange)                  statusFlags |= (1 << 3);  // 4-20mA out of range
    if (lastBootWasWDT)                 statusFlags |= (1 << 4);  // last reset was WDT

    float dbgHeapKib   = (float)ESP.getFreeHeap() / 1024.0f;
    float dbgUptimeH   = (float)millis() / 3600000.0f;
    bool  dbgHasDlQuality = rfHasDlQuality;
    float dbgDlRssi = rfLastDlRssi;
    float dbgDlSnr  = rfLastDlSnr;
    bool  dbgHasLinkCheck = rfHasLinkCheck;
    float dbgLinkMargin = (float)rfLastLinkMargin;
    float dbgLinkGwCnt  = (float)rfLastLinkGwCnt;

    // ---- Build payload: CayenneLPP (FPort 1) OR custom binary (FPort 3) -
    if (cfg.payloadFormat == 1) {
        // Custom binary: [ver][bitmask:4 LE][data...] (see BinField enum)
        uint32_t mask = 0;
        uint8_t  data[64];
        size_t   n = 0;
        auto putI16 = [&](float v, float scale) {
            int16_t x = (int16_t)lroundf(v * scale);
            data[n++] = (uint8_t)((uint16_t)x >> 8);
            data[n++] = (uint8_t)((uint16_t)x & 0xFF);
        };
        auto putU16 = [&](float v, float scale) {
            uint16_t x = (uint16_t)lroundf(v * scale);
            data[n++] = (uint8_t)(x >> 8);
            data[n++] = (uint8_t)(x & 0xFF);
        };
        auto putU8 = [&](uint8_t v) { data[n++] = v; };

        if (cfg.levelEn) { mask |= (1UL << BF_LEVEL); putI16(currentMA, 100.0f); }
        if (cfg.flowEn)  { mask |= (1UL << BF_FLOW);  putI16(flowLMin, 100.0f); }
        for (int i = 0; i < 3; i++)
            if (t1wire[i] != DEVICE_DISCONNECTED_C) {
                mask |= (1UL << (BF_TEMP1 + i)); putI16(t1wire[i], 10.0f);
            }
        if (bmeOk) {
            mask |= (1UL << BF_BME_TEMP); putI16(bmeTemp, 10.0f);
            mask |= (1UL << BF_BME_HUM);  putU8((uint8_t)lroundf(bmeHum * 2.0f));
            mask |= (1UL << BF_BME_PRES); putU16(bmePres, 10.0f);
        }
        if (ch9Present) { mask |= (1UL << BF_BATT_STATUS); putI16(ch9Value, 100.0f); }
        if (sendVictron && cfg.victronEn && victronV > 0.0f) {
            mask |= (1UL << BF_VIC_V); putI16(victronV, 100.0f);
            mask |= (1UL << BF_VIC_P); putI16(victronP, 100.0f);
            mask |= (1UL << BF_VIC_I); putI16(victronI, 100.0f);
        }
        if (sendVictron && victronCS  >= 0)    { mask |= (1UL << BF_VIC_CS);    putU8((uint8_t)victronCS); }
        if (sendVictron && victronH20 >= 0.0f) { mask |= (1UL << BF_VIC_YIELD); putI16(victronH20, 100.0f); }
        if (bmsOk) {
            mask |= (1UL << BF_DALY_V);        putI16(bmsParser.get.packVoltage, 100.0f);
            mask |= (1UL << BF_DALY_SOC);      putI16(bmsParser.get.packSOC, 100.0f);
            mask |= (1UL << BF_DALY_I);        putI16(bmsParser.get.packCurrent, 100.0f);
            mask |= (1UL << BF_DALY_TEMP);     putI16(bmsParser.get.tempAverage, 10.0f);
            mask |= (1UL << BF_DALY_CELLDIFF); putI16(bmsParser.get.cellDiff, 100.0f);
        }
        mask |= (1UL << BF_DIAG); putU8(statusFlags);
        if (cfg.debugMode == 1) {
            mask |= (1UL << BF_DBG_CYCLE);  putU8(debugCycleCounter);
            mask |= (1UL << BF_DBG_FAIL);   putU8(debugSendFailCounter);
            mask |= (1UL << BF_DBG_HEAP);   putI16(dbgHeapKib, 100.0f);
            mask |= (1UL << BF_DBG_UPTIME); putI16(dbgUptimeH, 100.0f);
            if (dbgHasDlQuality) {
                mask |= (1UL << BF_DBG_DL_RSSI); putI16(dbgDlRssi, 100.0f);
                mask |= (1UL << BF_DBG_DL_SNR);  putI16(dbgDlSnr, 100.0f);
            }
            if (dbgHasLinkCheck) {
                mask |= (1UL << BF_DBG_LINK_MARGIN); putI16(dbgLinkMargin, 100.0f);
                mask |= (1UL << BF_DBG_LINK_GW);     putU8((uint8_t)dbgLinkGwCnt);
            }
        }

        binLen = 0;
        binBuf[binLen++] = BIN_PROTO_VERSION;
        binBuf[binLen++] = (uint8_t)(mask & 0xFF);
        binBuf[binLen++] = (uint8_t)((mask >> 8) & 0xFF);
        binBuf[binLen++] = (uint8_t)((mask >> 16) & 0xFF);
        binBuf[binLen++] = (uint8_t)((mask >> 24) & 0xFF);
        memcpy(&binBuf[binLen], data, n);
        binLen += n;
    } else {
        // CayenneLPP (custom channel mapping, see chirpstack/decoder.js)
        lpp.reset();

        if (cfg.levelEn)
            lpp.addAnalogInput(1, currentMA);

        if (cfg.flowEn)
            lpp.addAnalogInput(2, flowLMin);

        for (int i = 0; i < 3; i++)
            if (t1wire[i] != DEVICE_DISCONNECTED_C)
                lpp.addTemperature(3 + i, t1wire[i]);

        if (bmeOk) {
            lpp.addTemperature       (6, bmeTemp);
            lpp.addRelativeHumidity  (7, bmeHum);
            lpp.addBarometricPressure(8, bmePres);
        }

        // CH9: bmsMode 0=off (no CH9), 1=DALY status, 2=LiPo ADC
        if (ch9Present)
            lpp.addAnalogInput(9, ch9Value);

        // Victron telemetry: contiguous channel block 10–14
        if (sendVictron && cfg.victronEn && victronV > 0.0f) {
            lpp.addAnalogInput(10, victronV);
            lpp.addAnalogInput(11, victronP);
            lpp.addAnalogInput(12, victronI);
        }
        if (sendVictron && victronCS  >= 0)    lpp.addAnalogInput(13, victronCS);
        if (sendVictron && victronH20 >= 0.0f) lpp.addAnalogInput(14, victronH20);

        // DALY BMS telemetry: contiguous channel block 15–19
        if (bmsOk) {
            lpp.addAnalogInput(15, bmsParser.get.packVoltage);
            lpp.addAnalogInput(16, bmsParser.get.packSOC);
            lpp.addAnalogInput(17, bmsParser.get.packCurrent);   // A, positive=charging, negative=discharging
            lpp.addTemperature (18, bmsParser.get.tempAverage);  // average battery temperature
            lpp.addAnalogInput (19, bmsParser.get.cellDiff);     // mV cell voltage spread (battery health)
        }

        // CH20: diagnostic flags (bitmask)
        lpp.addDigitalInput(20, statusFlags);

        if (cfg.debugMode == 1) {
            lpp.addDigitalInput(200, debugCycleCounter);
            lpp.addDigitalInput(202, debugSendFailCounter);
            lpp.addAnalogInput(203, dbgHeapKib);
            lpp.addAnalogInput(204, dbgUptimeH);
            if (dbgHasDlQuality) {
                lpp.addAnalogInput(205, dbgDlRssi);    // dBm
                lpp.addAnalogInput(206, dbgDlSnr);     // dB
            }
            if (dbgHasLinkCheck) {
                lpp.addAnalogInput(207, dbgLinkMargin);  // dB (gateway demod margin)
                lpp.addDigitalInput(208, (uint8_t)dbgLinkGwCnt);
            }
        }
    }

    // ---- Display: show measurements ------------------------------------
    // Shared renderer so the "measurements" screen and the "Sending uplink..."
    // screen show the same values in the same priority order (most important
    // first): Flow, DS18B20 temps, Level, BME280, RF quality, Victron, DALY,
    // then the rest.
    // Lines are drawn compactly from `y`; anything past the 64 px panel simply
    // isn't visible (lowest-priority values drop off first).
    auto drawMeasurementLines = [&](int y) {
        char buf[22];
        // 1) Flow
        if (cfg.flowEn) {
            snprintf(buf, sizeof(buf), "Flow:%.1f L/min", flowLMin);
            display.drawString(0, y, buf); y += 11;
        }
        // 2) DS18B20 temperatures (all three compactly on one line)
        String tLine = "T:";
        bool anyT = false;
        for (int i = 0; i < 3; i++) {
            if (t1wire[i] != DEVICE_DISCONNECTED_C) {
                char tmp[7];
                snprintf(tmp, sizeof(tmp), " %.1f", t1wire[i]);
                tLine += tmp;
                anyT = true;
            }
        }
        if (anyT) { display.drawString(0, y, tLine); y += 11; }
        // 3) Level
        if (cfg.levelEn) {
            snprintf(buf, sizeof(buf), "Level:%.1f mA", currentMA);
            display.drawString(0, y, buf); y += 11;
        }
        // 4) BME280
        if (bmeOk) {
            snprintf(buf, sizeof(buf), "BME:%.1fC %.0f%%RH", bmeTemp, bmeHum);
            display.drawString(0, y, buf); y += 11;
        }
        // 5) RF quality (for field commissioning), only when explicitly
        // configured via debugMode and values are available.
        if (cfg.debugMode == 1 && dbgHasDlQuality) {
            snprintf(buf, sizeof(buf), "RF:%.0fdBm %.1fdB", dbgDlRssi, dbgDlSnr);
            display.drawString(0, y, buf); y += 11;
        }
        if (cfg.debugMode == 1 && dbgHasLinkCheck) {
            snprintf(buf, sizeof(buf), "Link:%udB GW:%u",
                     (unsigned)rfLastLinkMargin, (unsigned)rfLastLinkGwCnt);
            display.drawString(0, y, buf); y += 11;
        }

        // 6) Victron
        if (cfg.victronEn && victronV > 0.0f) {
            snprintf(buf, sizeof(buf), "Vic:%.1fV %.0fW", victronV, victronP);
            display.drawString(0, y, buf); y += 11;
        }
        // 7) DALY BMS
        if (cfg.bmsMode == 1 && bmsOk) {
            snprintf(buf, sizeof(buf), "BMS:%.1fV SOC:%.0f%%",
                     bmsParser.get.packVoltage, bmsParser.get.packSOC);
            display.drawString(0, y, buf); y += 11;
        }
        // 8) rest: LiPo ADC battery voltage
        else if (cfg.bmsMode == 2) {
            snprintf(buf, sizeof(buf), "LiPo:%.2fV", heltec_vbat());
            display.drawString(0, y, buf); y += 11;
        }
    };

    if (showDisplay) {
        display.clear();
        drawMeasurementLines(0);
        display.display();
        delay(4000);  // show for 4 s, then uplink status
    }

    // ---- Send LoRaWAN uplink -------------------------------------------
    if (showDisplay) {
        display.clear();
        display.drawString(0, 0, "Sending uplink...");
        drawMeasurementLines(11);  // keep the measured values visible
        display.display();
    }

    int16_t state = node->activateOTAA();
    // Save session after successful join/restore
    if (state == RADIOLIB_ERR_NONE || state == RADIOLIB_LORAWAN_NEW_SESSION) {
        memcpy(lorawanSession, node->getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
        sessionSaved = true;
    }
    if (state == RADIOLIB_ERR_NONE || state == RADIOLIB_LORAWAN_NEW_SESSION) {
        uint8_t rxBuf[256] = {};
        size_t  rxLen = sizeof(rxBuf) - 1;

        // LinkCheck adds network-side RF quality (gateway demod margin + GW count).
        // Request sparingly to avoid unnecessary downlink load.
        constexpr uint8_t LINKCHECK_INTERVAL = 6;
        bool requestLinkCheck = cfg.debugMode == 1 || showDisplay;
        if (requestLinkCheck) {
            rfLinkCheckCounter++;
            if (rfLinkCheckCounter >= LINKCHECK_INTERVAL) {
                node->sendMacCommandReq(RADIOLIB_LORAWAN_MAC_LINK_CHECK);
                rfLinkCheckCounter = 0;
            }
        }

        // Choose payload buffer and FPort based on the configured format:
        //   CayenneLPP -> FPort 1 (existing decoder), custom binary -> FPort 3
        const uint8_t* txBuf  = (cfg.payloadFormat == 1) ? binBuf : lpp.getBuffer();
        size_t         txLen  = (cfg.payloadFormat == 1) ? binLen : lpp.getSize();
        uint8_t        txPort = (cfg.payloadFormat == 1) ? 3 : 1;
        int16_t sendState = node->sendReceive((uint8_t*)txBuf, txLen, txPort, rxBuf, &rxLen);

        if (sendState == RADIOLIB_ERR_NONE) {
            bool requiresRestart = false;
            Serial.println("[LoRaWAN] Send successful.");
            time_t now = time(nullptr);
            if (now > 1000000)
                Serial.printf("[Time] Synchronized: %ld\n", now);

            // If a downlink was received, packet RSSI/SNR of that RX window are
            // available directly from the radio.
            if (rxLen > 0) {
                rfLastDlRssi = radio.getRSSI();
                rfLastDlSnr  = radio.getSNR();
                rfHasDlQuality = true;
            }

            // Consume LinkCheckAns if present in this downlink.
            uint8_t linkMargin = 0, linkGwCnt = 0;
            if (node->getMacLinkCheckAns(&linkMargin, &linkGwCnt) == RADIOLIB_ERR_NONE) {
                rfLastLinkMargin = linkMargin;
                rfLastLinkGwCnt  = linkGwCnt;
                rfHasLinkCheck   = true;
            }

            if (rxLen > 0) {
                Serial.printf("[Downlink] %d bytes: ", rxLen);
                for (size_t i = 0; i < rxLen; i++) Serial.printf("%02X ", rxBuf[i]);
                Serial.println();

                if (processParamDownlink(cfg, rxBuf, rxLen, requiresRestart)) {
                    if (requiresRestart) {
                        Serial.println("[Downlink] Region changed, restarting.");
                        delay(500);
                        ESP.restart();
                    }
                } else {

                    // Single-byte commands on port 1
                    if (rxLen >= 1) {
                        switch (rxBuf[0]) {
                            case 0x01:  // Immediate measurement (skip sleep once)
                                skipSleepOnce = true;
                                Serial.println("[Downlink] Immediate measurement requested.");
                                break;
                            case 0x02:  // Open config portal (alias for 0x03, kept for compatibility)
                                sessionSaved = false;  // forces restart with portal
                                forceConfigPortal = true;
                                Serial.println("[Downlink] Config portal requested.");
                                break;
                            case 0x03:  // Open config portal immediately (after current cycle)
                                Serial.println("[Downlink] Config portal requested (immediate).");
                                forceConfigPortal = true;
                                sessionSaved = false;
                                break;
                            case 0x04:  // enable debug mode (persistent)
                                cfg.debugMode = 1;
                                saveConfig(cfg);
                                Serial.println("[Downlink] Debug mode enabled.");
                                break;
                            case 0x05:  // disable debug mode (persistent)
                                cfg.debugMode = 0;
                                saveConfig(cfg);
                                Serial.println("[Downlink] Debug mode disabled.");
                                break;
                            case 0xFF:  // Factory reset: clear NVS + restart
                                { Preferences p; p.begin("config", false); p.clear(); p.end(); }
                                sessionSaved = false;
                                forceConfigPortal = false;
                                Serial.println("[Downlink] Factory reset!");
                                delay(500);
                                ESP.restart();
                                break;
                        }
                    }
                }
            }

            // Daily uplink: every 288 cycles (24h at 5min sleep interval)
            constexpr uint16_t DAILY_CYCLES = 288;
            if (daily.cycleCount >= DAILY_CYCLES) {
                CayenneLPP lppDaily(30);
                lppDaily.addAnalogInput(1, daily.totalLiters);
                lppDaily.addAnalogInput(2, daily.maxFlowLMin);
                lppDaily.addTemperature (3, daily.minTempC);
                lppDaily.addTemperature (4, daily.maxTempC);
                node->sendReceive(lppDaily.getBuffer(), lppDaily.getSize(), 2, nullptr, nullptr);
                // Reset
                daily = { 0, 0.0f, 0.0f, 100.0f, -100.0f };
                Serial.println("[Daily uplink] sent and reset.");
            }
        } else {
            debugSendFailCounter++;
            Serial.printf("[LoRaWAN] Send error: %d\n", sendState);
        }
    } else {
        debugSendFailCounter++;
        Serial.printf("[LoRaWAN] OTAA error: %d\n", state);
    }

    // ---- End of cycle: deep sleep or continuous restart ----------------
    delay(4000);
    if (showDisplay) { display.clear(); display.display(); }

    // Downlink 0x03 (or 0x02): open config portal immediately instead of sleeping
    if (forceConfigPortal) {
        forceConfigPortal = false;
        display.displayOn();
        display.clear();
        display.drawString(0, 0, "Starting config portal...");
        display.display();
        cfg = runConfigPortal(cfg);
        saveConfig(cfg);
        display.clear();
        display.drawString(0,  0, "Configuration saved!");
        display.drawString(0, 16, "Restarting...");
        display.display();
        delay(2000);
        ESP.restart();
    }

        if (cfg.victronEn) { Serial1.end(); pinMode(PIN_VICTRON_RX, INPUT); }
        if (cfg.bmsMode == 1) { Serial2.end(); pinMode(PIN_DALY_RX, INPUT); }

        if (cfg.highsideEn) {
            digitalWrite(PIN_HIGH_SIDE_SWITCH, LOW);
            if (cfg.sleepSec > 0 && !skipSleepOnce) gpio_hold_en((gpio_num_t)PIN_HIGH_SIDE_SWITCH);
        }
        if (cfg.sleepSec > 0 || skipSleepOnce) {
            // PIN_OPTO_SWITCH HIGH: powers off DS18B20, HCPL-260L, ADuM121N1 (3.3V)
            // and flow sensor (5V via VCC3.3_SW → VN2222L → LP0701N3(2))
            digitalWrite(PIN_OPTO_SWITCH, HIGH);
            if (cfg.sleepSec > 0 && !skipSleepOnce) gpio_hold_en((gpio_num_t)PIN_OPTO_SWITCH);
        }

        if (cfg.sleepSec == 0 || skipSleepOnce) {
            skipSleepOnce = false;
            firstCycle = false;
            buttonWakeCycle = false;
            continue;
        }

        esp_task_wdt_delete(nullptr);

        // Deep-sleep button wake is handled by EXT0, not the ISR — detach first
        // so the run-loop interrupt and the RTC EXT0 config don't overlap.
        detachInterrupt(digitalPinToInterrupt(PIN_USER_BUTTON));

        // Wait for user button to be released to prevent an immediate EXT0 wakeup loop
        while (digitalRead(PIN_USER_BUTTON) == LOW) delay(10);
        delay(50);  // debounce

        esp_sleep_enable_timer_wakeup((uint64_t)cfg.sleepSec * 1000000ULL);
        esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_USER_BUTTON, 0);
        heltec_deep_sleep();
    }
}

void loop() {}
