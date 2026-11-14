#include "portal.h"
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <SSD1306Wire.h>
#include <esp_task_wdt.h>
#include <esp_netif.h>
#include <esp_event.h>
#include "generated_version.h"
#include "decoder_js.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

// Defined by heltec_unofficial.h, which may only be included once (main.cpp):
// it defines its globals in the header.
extern SSD1306Wire display;

// ============================================================================
// WIFI CONFIGURATION PORTAL
// ============================================================================
Config runConfigPortal(Config& cur)
{
    // Unsubscribe from the task watchdog before entering the portal.
    // esp_task_wdt_add(nullptr) runs very early in setup(), before either
    // call site of this function, so every invocation — including the
    // automatic first-boot one — runs with the 120 s watchdog already
    // armed. WiFiManager's startConfigPortal() blocks here for as long as
    // the user takes to fill in the form, without ever calling
    // esp_task_wdt_reset() (portal HTTP handling doesn't touch it) — so a
    // user taking longer than 120 s would get the watchdog resetting the
    // device mid-session, before saveConfig() is ever reached. Every path
    // out of this function's caller leads to ESP.restart() regardless of
    // outcome, so there's no need to re-subscribe afterward.
    esp_task_wdt_delete(nullptr);

    // Frozen copy of the config as it was when the portal was opened. cur
    // is a reference to the caller's cfg, which the immediate-persist save
    // in setSaveParamsCallback below reassigns — buildConfig() uses this
    // snapshot as its baseline instead, so a second param-only save in the
    // same session doesn't build on top of the first save's result.
    const Config curSnapshot = cur;

    setCpuFrequencyMhz(240);
    display.displayOn();
    display.clear();
    display.drawString(0, 0, "Starting config portal...");
    display.display();

    char b_sleep[12], b_sample[12], b_send[12], b_k[12], b_dispcyc[6], b_cpu[6], b_region[8], b_debug[4];
    char b_bms[4], b_vic[4], b_hs[4], b_lv[4], b_int[6], b_vicint[6], b_fl[4], b_alt[6];
    char b_payfmt[4], b_p2[4], b_k2[12], b_din[4], b_ai[4], b_dwake[4];
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
    itoa(cur.pulse2Mode,  b_p2,     10);
    itoa(cur.kFactor2,    b_k2,     10);
    itoa(cur.dinEn,       b_din,    10);
    itoa(cur.aiEn,        b_ai,     10);
    itoa(cur.dinWake,     b_dwake,  10);
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

    // Field lengths leave room for separators (e.g. 70:B3:D5:...), which
    // parseHexBytes() strips.
    WiFiManagerParameter p_dev  ("dev",  "DevEUI (16 Hex)",                   b_dev,   24);
    WiFiManagerParameter p_join ("join", "JoinEUI (16 Hex)",                  b_join,  24);
    WiFiManagerParameter p_key  ("key",  "AppKey (32 Hex)",                   b_key,   48);
    WiFiManagerParameter p_reg  ("region", "LoRaWAN region (EU868,US915,AU915,AS923,IN865,KR920,CN470)", b_region, 8);
    WiFiManagerParameter p_sleep("sleep",   "Deep Sleep time (s) [0=continuous mode]", b_sleep,   6);
    WiFiManagerParameter p_timing("<div style='margin:0.35rem 0 0.6rem 0;padding:0.45rem;border:1px solid #ccc;border-radius:6px;'><strong>Timing note:</strong><br/>sleep &gt; 0: <code>sample</code> is used (measurement window in ms).<br/>sleep = 0: <code>sendint</code> is used (continuous cycle interval in s).</div>");
    WiFiManagerParameter p_samp ("sample",  "Sampling window (ms, only if sleep>0)", b_sample,  6);
    WiFiManagerParameter p_send ("sendint", "Continuous interval (s, only if sleep=0)", b_send,  6);
    WiFiManagerParameter p_k    ("kfactor", "K-factor (pulses/L)",               b_k,       6);
    WiFiManagerParameter p_dispcyc("dispcyc","Display-on cycles after button press (0=always on, sleep=0 only)", b_dispcyc, 6);
    WiFiManagerParameter p_cpu  ("cpufreq", "CPU (80/240 MHz)",                  b_cpu,     4);
    WiFiManagerParameter p_bms  ("bmsmode", "Battery (0=off, 1=DALY BMS, 2=VIN voltage)", b_bms,     2);
    WiFiManagerParameter p_vic  ("victornen","Victron (1=Yes)",                  b_vic,     2);
    WiFiManagerParameter p_hs   ("highsideen","HighSide/24V (1=Yes)",            b_hs,      2);
    WiFiManagerParameter p_lv   ("levelen", "4-20mA Sensor (1=Yes)",            b_lv,      2);
    WiFiManagerParameter p_fl   ("flowen",  "Flow sensor (open-collector, 1=Yes)", b_fl,      2);
    WiFiManagerParameter p_p2   ("p2mode",  "Pulse input 2 (0=off, 1=2nd flow sensor, 2=counter)", b_p2, 2);
    WiFiManagerParameter p_k2   ("kfactor2","K-factor pulse input 2 (pulses/L, flow mode only)", b_k2, 6);
    WiFiManagerParameter p_din  ("dinen",   "Digital inputs IN1-IN3 (1=Yes)", b_din, 2);
    WiFiManagerParameter p_ai   ("aien",    "Analog inputs AI1/AI2 0-10 V (1=Yes)", b_ai, 2);
    WiFiManagerParameter p_dwake("dinwake", "Wake on input change (0=off, 1=IN1, 2=IN2, 4=IN3, sum, e.g. 7=all; JP9 at 1-2)", b_dwake, 2);
    WiFiManagerParameter p_int  ("bmsint",  "DALY interval (cycles)",            b_int,     4);
    WiFiManagerParameter p_vicint("vicint", "Victron interval (cycles, 1=every)", b_vicint,  4);
    WiFiManagerParameter p_payfmt("payfmt", "Payload format (0=CayenneLPP, 1=binary)", b_payfmt, 2);
    WiFiManagerParameter p_alt  ("altitude","Altitude above sea level (m, QNH)", b_alt,     5);
    WiFiManagerParameter p_dbg  ("debugmode","Debug mode (0=No, 1=Yes)",         b_debug,   2);
    WiFiManagerParameter p_section("<div style='margin:0.5rem 0 0.75rem 0;'><h3>LoRa-Water-Node Parameter</h3><p style='margin:0 0 0.35rem 0;'>Firmware version: " FIRMWARE_VERSION "</p><p style='margin:0;'>Device settings are available on a separate page.</p></div>");

    // Builds a full Config from the current state of every WiFiManagerParameter.
    // Shared by the immediate-persist save below and the function's own
    // return value, so there's exactly one place that knows how to translate
    // the portal's form fields into a Config.
    auto buildConfig = [&]() -> Config {
        Config c = curSnapshot;  // preserves anything the user didn't touch
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
        c.pulse2Mode  = constrain(atoi(p_p2.getValue()), 0, 2);
        c.kFactor2    = atoi(p_k2.getValue());
        c.dinEn       = (atoi(p_din.getValue()) == 1) ? 1 : 0;
        c.aiEn        = (atoi(p_ai.getValue()) == 1) ? 1 : 0;
        c.dinWake     = constrain(atoi(p_dwake.getValue()), 0, 7);
        c.bmsInterval = atoi(p_int.getValue());
        c.victronInterval = atoi(p_vicint.getValue());
        c.payloadFormat = (atoi(p_payfmt.getValue()) == 1) ? 1 : 0;
        c.altitudeM   = atoi(p_alt.getValue());
        c.debugMode   = (atoi(p_dbg.getValue()) == 1) ? 1 : 0;
        // Only update credentials when the field holds a complete, valid hex
        // value; an empty or invalid field keeps the previously stored value.
        if (*p_dev.getValue()  && !parseEui(p_dev.getValue(), c.devEui))
            Serial.printf("[Portal] Invalid DevEUI '%s' ignored\n", p_dev.getValue());
        if (*p_join.getValue() && !parseEui(p_join.getValue(), c.joinEui))
            Serial.printf("[Portal] Invalid JoinEUI '%s' ignored\n", p_join.getValue());
        if (*p_key.getValue()  && !parseHexBytes(p_key.getValue(), c.appKey, 16))
            Serial.println("[Portal] Invalid AppKey ignored");
        // Validate inputs – prevents division by zero and infinite loops
        c.kFactor     = max(1,    c.kFactor);
        c.kFactor2    = max(1,    c.kFactor2);
        c.sleepSec    = (c.sleepSec == 0) ? 0 : max(10, c.sleepSec);  // 0=continuous, otherwise min 10s
        c.samplingMs  = max(1000, c.samplingMs);
        c.continuousSendSec = max(10, c.continuousSendSec);
        c.altitudeM   = max(0, min(9999, c.altitudeM));
        c.cpuFreqMhz  = (c.cpuFreqMhz >= 160) ? 240 : 80;
        c.bmsInterval = max(1, c.bmsInterval);
        c.victronInterval = max(1, c.victronInterval);
        c.displayOnCycles = max(0, min(1000, c.displayOnCycles));  // 0 = always on
        return c;
    };

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

    // The "LoRa-Water-Node Parameter" page (/param) is a separate form from
    // "Configure WiFi", and startConfigPortal() only returns once THAT one
    // is submitted (WiFiManager's own connect flag is only set from its
    // WiFi-save handler) — a user who only needs to change parameters (the
    // common case) would otherwise see WiFiManager's own "Saved"
    // confirmation, consider themselves done, and never have anything
    // written to NVS at all unless they also submit the WiFi page. Persist
    // immediately here instead, and end the session right away too —
    // otherwise the portal/AP would be left running indefinitely with no
    // further indication anything happened. stopConfigPortal() just sets an
    // internal abort flag; the blocking startConfigPortal() loop checks it
    // and returns on its next iteration (this callback still runs to
    // completion first, so the "Saved" confirmation page still reaches the
    // browser).
    wm.setSaveParamsCallback([&wm, &cur, &buildConfig]() {
        cur = buildConfig();
        saveConfig(cur);
        wm.stopConfigPortal();
    });

    wm.addParameter(&p_section);
    wm.addParameter(&p_dev);   wm.addParameter(&p_join);  wm.addParameter(&p_key); wm.addParameter(&p_reg);
    wm.addParameter(&p_sleep); wm.addParameter(&p_timing); wm.addParameter(&p_samp);  wm.addParameter(&p_send); wm.addParameter(&p_k);
    wm.addParameter(&p_dispcyc); wm.addParameter(&p_cpu);
    wm.addParameter(&p_bms);   wm.addParameter(&p_vic);
    wm.addParameter(&p_hs);    wm.addParameter(&p_lv);
    wm.addParameter(&p_fl);
    wm.addParameter(&p_p2);    wm.addParameter(&p_k2);    wm.addParameter(&p_din);   wm.addParameter(&p_ai);   wm.addParameter(&p_dwake);
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

    return buildConfig();
}
