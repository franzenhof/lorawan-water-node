// ============================================================================
// LORAWAN WATER NODE FOR CHIRPSTACK (HELTEC V3)
//
// Auto-detected:     1-Wire (DS18B20 x3), BME280
// Runtime flags:     flowEn, levelEn, victronEn, bmsMode, highsideEn,
//                    pulse2Mode, dinEn, aiEn (WiFi portal)
// PCB v1.0 I/Os:     2nd pulse input, 3 digital inputs, 2 switched outputs,
//                    ADS1115 (VIN, 4-20 mA, 2 x 0-10 V)
// Defaults:          DEFAULT_* below; can be overridden with build_flags
//                    (platformio.ini sets none - one build covers all variants)
// ============================================================================

// This flag MUST be placed before heltec_unofficial
#define HELTEC_LORA_MODE 1

#include <Arduino.h>
#include <heltec_unofficial.h>
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
#include <new>
#include "generated_version.h"
#include "hardware.h"
#include "config.h"
#include "downlink.h"
#include "portal.h"
#include "flow_counter.h"
#include "board_io.h"
#include "payload.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

// ============================================================================
// GLOBALS
// ============================================================================
alignas(LoRaWANNode) static uint8_t nodeStorage[sizeof(LoRaWANNode)];
LoRaWANNode*         node = nullptr;
TwoWire              CustomI2C = TwoWire(1);
Adafruit_BME280      bme;
OneWire              oneWire(PIN_ONE_WIRE);
DallasTemperature    sensors(&oneWire);
// Full configuration incl. debug fields is ~105 bytes (see README "Payload
// size"); EU868 DR0-DR2 only carry 51 bytes, so large configs need DR3+.
CayenneLPP        lpp(128);

static uint8_t binBuf[BIN_MAX_SIZE];
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
RTC_DATA_ATTR static DailyStats daily = DAILY_RESET;
RTC_DATA_ATTR static uint32_t pulse2Total = 0;       // 2nd pulse input, counter mode (resets on power loss/reset)
// System time (s) at the previous cycle's daily accounting. The RTC keeps
// system time running through deep sleep, so the difference is the real
// cycle length including the sleep phase. 0 = unknown (after power loss).
RTC_DATA_ATTR static int64_t lastCycleTimeSec = 0;
// Input-change wakes in a row without a timer wake in between. A chattering
// contact would otherwise send one uplink after the other: after
// INPUT_WAKE_MAX of them the next sleep runs on the timer alone.
RTC_DATA_ATTR static uint8_t  inputWakeStreak = 0;
static constexpr uint8_t      INPUT_WAKE_MAX = 3;
VeDirectFrameHandler victronParser;
Daly_BMS_UART        bmsParser(Serial2);

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================
static void initNodeForRegion(const char* region)
{
    const LoRaWANBand_t* band = regionToBand(region);
    node = new (nodeStorage) LoRaWANNode(&radio, band);
}

// heltec_deep_sleep() without its pinMode(VBAT_ADC = GPIO1, INPUT): GPIO1 is
// OUT3 on PCB v1.0 and must keep its held output level through deep sleep.
static void nodeDeepSleep()
{
    display.displayOff();
    // A radio that was not touched since boot ignores sleep() without a
    // begin() first and draws ~800 uA more (see heltec_deep_sleep()).
    radio.begin();
    radio.sleep(false);
    heltec_ve(false);
    heltec_led(0);
    for (int pin : { (int)VBAT_CTRL, (int)DIO1, (int)RST_LoRa, (int)BUSY_LoRa, (int)SS, (int)MISO,
                     (int)MOSI, (int)SCK, (int)SDA_OLED, (int)SCL_OLED, (int)RST_OLED })
        pinMode(pin, INPUT);
    esp_deep_sleep_start();
}

// Hardware revision from the divider on GPIO20 (needs VCC3.3 switched on and the USB pad detached,
// both done before the first measurement window). Logged once after boot.
static void logHardwareId()
{
    pinMode(PIN_HW_ID, INPUT);
    uint32_t mv = analogReadMilliVolts(PIN_HW_ID);
    const char* rev = mv < 400 ? "prototype (no divider)"
                    : (mv >= HW_ID_V10_MIN_MV && mv <= HW_ID_V10_MAX_MV) ? "v1.0" : "unknown";
    Serial.printf("[HW] revision ID %u mV -> %s\n", (unsigned)mv, rev);
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
    WakeSource wakeup;

    cfg = loadConfig();
    initNodeForRegion(cfg.region);
    setCpuFrequencyMhz(cfg.cpuFreqMhz);
    // Enable VEXT (GPIO36, active-LOW) before heltec_setup() so the display
    // already has power when the RST pulse is issued.
    heltec_ve(true);
    heltec_setup();
    wakeup = sleepWakeSource();
    inputWakeStreak = (wakeup == WAKE_INPUT) ? inputWakeStreak + 1 : 0;
    if (wakeup == WAKE_INPUT) Serial.println("[Wake] Digital input changed.");

    // Switched outputs: restored right after a reset instead of only after
    // the portal hint (after deep sleep they are still held at this point).
    uint8_t outState = outputsLoad();  // bit 0 = OUT1, bit 1 = OUT2, bit 2 = OUT3
    outputsApply(outState);

    // Automatic display power management (deep-sleep mode, sleepSec>0):
    // OFF on a timer or input wakeup, ON for a button wakeup or a fresh
    // boot. Not user-configurable — always the sensible choice for an
    // unattended battery-powered node. (Continuous mode, sleepSec==0, uses
    // its own cycle-based on/off logic further below.)
    showDisplay = (wakeup == WAKE_BUTTON || wakeup == WAKE_RESET);
    showDisplay ? display.displayOn() : display.displayOff();

    // GPIO0 LOW during power-on triggers the bootloader, so the button cannot
    // be held at startup to open the config portal.  Instead, a 4-second hint
    // window is shown after boot; holding the button for 3 s within that
    // window opens the portal.
    // On a button wakeup (pressed during sleep) the hint is skipped and a
    // normal measurement cycle runs with the display enabled.
    bool freshBoot = (wakeup == WAKE_RESET);
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

    // RadioLib 7.x order matters: beginOTAA() clears nonces and session, so
    // both buffers are restored *after* it. Nonces (DevNonce/JoinNonce) come
    // from NVS so they survive power loss - a reused DevNonce is rejected by
    // a spec-compliant network server. The session (keys, frame counters)
    // lives in RTC RAM and is only valid after a deep-sleep wake; RadioLib
    // only accepts it when the nonces buffer it was created with is restored
    // first.
    node->beginOTAA(cfg.joinEui, cfg.devEui, cfg.appKey, cfg.appKey);
    {
        uint8_t nonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
        Preferences p;
        bool haveNonces = p.begin("lorawan", true) &&
                          p.getBytes("nonces", nonces, sizeof(nonces)) == sizeof(nonces);
        p.end();
        if (haveNonces) {
            int16_t st = node->setBufferNonces(nonces);
            Serial.printf("[LoRaWAN] Nonces restore: %d\n", st);
            if (st == RADIOLIB_ERR_NONE && sessionSaved &&
                wakeup != WAKE_RESET) {
                st = node->setBufferSession(lorawanSession);
                Serial.printf("[LoRaWAN] Session restore: %d\n", st);
            }
        }
    }

    // Latch user-button presses via a GPIO interrupt so the display can be
    // re-armed from any blocking section while awake (continuous mode). The
    // flag is cleared here so a press during boot/portal isn't carried in.
    pinMode(PIN_USER_BUTTON, INPUT_PULLUP);
    g_btnPressed = false;
    attachInterrupt(digitalPinToInterrupt(PIN_USER_BUTTON), onButtonISR, FALLING);

    bool buttonWakeCycle = (wakeup == WAKE_BUTTON);
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
            // AO3400A -> AO3401A (P-channel): HIGH = 22 V boost on (for the 4-20 mA sensor)
            pinMode(PIN_HIGH_SIDE_SWITCH, OUTPUT);
            digitalWrite(PIN_HIGH_SIDE_SWITCH, HIGH);
        }

        // LP0701N3(1) (P-channel, direct drive): LOW = VCC3.3 switched on
        pinMode(PIN_OPTO_SWITCH, OUTPUT);
        digitalWrite(PIN_OPTO_SWITCH, LOW);

        // ---- I/O block ---------------------------------------------------
        // Outputs keep the last downlink state; after deep sleep this also
        // releases the pad hold. Digital inputs are sampled at the end of
        // the measurement window.
        bool pulse2On = cfg.pulse2Mode != 0;
        bool anyPulse = cfg.flowEn || pulse2On;
        outputsApply(outState);
        if (cfg.dinEn) digitalInputsBegin();

        if (showDisplay) {
            display.clear();
            display.drawString(0,  0, "LoRaWan-Water-Node");
            display.drawString(0, 12, "Measuring...");
            display.display();
        }

        // ---- Initialize sensors ----------------------------------------
        if (cfg.flowEn && (cfg.sleepSec > 0 || firstCycle)) pcnt_init(PULSE_FLOW);
        if (pulse2On   && (cfg.sleepSec > 0 || firstCycle)) pcnt_init(PULSE_2);
        if (anyPulse && cfg.sleepSec == 0 && !flowWindowPrimed) {
            if (cfg.flowEn) pcnt_clear_keep_running(PULSE_FLOW);
            if (pulse2On)   pcnt_clear_keep_running(PULSE_2);
            flowWindowStartMs = millis();
            flowWindowPrimed = true;
        }
        CustomI2C.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
        bool adsOk = analogBegin(CustomI2C);   // VCC3.3 switched is on (GPIO38 LOW above)

        if (cfg.victronEn) {
            Serial1.begin(19200, SERIAL_8N1, PIN_VICTRON_RX, PIN_VICTRON_TX);
            // Forget the previous window's frame: the handler never clears its
            // public buffer itself, so in continuous mode an unplugged VE.Direct
            // cable would otherwise keep re-sending the last values forever.
            // Names must be cleared too - frameEndEvent() compares against one
            // entry past veEnd when merging a new frame.
            memset(victronParser.veName,  0, sizeof(victronParser.veName));
            memset(victronParser.veValue, 0, sizeof(victronParser.veValue));
            victronParser.veEnd = 0;
        }

        if (cfg.bmsMode == 1) {
            Serial2.begin(9600, SERIAL_8N1, PIN_DALY_RX, PIN_DALY_TX);
            bmsParser.Init();
        }

        // auto-detect: present if begin() returns true; 0x76 (SDO to GND) or 0x77 (most breakout boards)
        bool bmeOk = bme.begin(0x76, &CustomI2C) || bme.begin(0x77, &CustomI2C);
        if (firstCycle) logHardwareId();
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
        uint32_t flowDisplayStartCount = (cfg.flowEn && cfg.sleepSec == 0) ? pcnt_read_peek(PULSE_FLOW) : 0;
        if (ds18Async) {
            sensors.setWaitForConversion(false);  // non-blocking convert
            sensors.requestTemperatures();        // kick off first conversion
            ds18ReqMs   = millis();
            ds18Pending = true;
        }

        bool loopFault = false;
        while (millis() - tStart < measurementWindowMs) {
            if (cfg.levelEn && adsOk && !loopFault) {
                float adcMv = analogLevelMv();   // ADS1115 AIN1, shunt voltage in mV
                if (cfg.highsideEn && adcMv > LOOP_OVERCURRENT_MV) {
                    // > 23 mA: shorted cable or faulty sensor - switch the boost off instead of
                    // loading the 5 V rail until the 50 mA PTC trips; reported as out of range
                    digitalWrite(PIN_HIGH_SIDE_SWITCH, LOW);
                    loopFault = true;
                    Serial.printf("[Level] loop current %.1f mA > 23 mA - 22 V boost switched off\n", adcMv / ADC_SHUNT_OHM);
                }
                adcSum += adcMv;
                adcCount++;
                if (adcMv < adcMin) adcMin = adcMv;
                if (adcMv > adcMax) adcMax = adcMv;
            }

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
                    uint32_t livePulses = pcnt_read_peek(PULSE_FLOW);
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

        uint32_t pulses  = 0, pulses2 = 0;
        float elapsedSec = (millis() - tStart) / 1000.0f;
        if (anyPulse) {
            if (cfg.sleepSec == 0) {
                unsigned long flowNowMs = millis();
                if (cfg.flowEn) pulses  = pcnt_read_peek(PULSE_FLOW);
                if (pulse2On)   pulses2 = pcnt_read_peek(PULSE_2);
                elapsedSec = (flowNowMs - flowWindowStartMs) / 1000.0f;
                flowWindowStartMs = flowNowMs;
                if (cfg.flowEn) pcnt_clear_keep_running(PULSE_FLOW);
                if (pulse2On)   pcnt_clear_keep_running(PULSE_2);
            } else {
                if (cfg.flowEn) pulses  = pcnt_read_and_stop(PULSE_FLOW);
                if (pulse2On)   pulses2 = pcnt_read_and_stop(PULSE_2);
            }
        }
        if (elapsedSec < 0.001f) elapsedSec = 0.001f;
        float flowLMin   = cfg.flowEn
                           ? ((float)pulses / (float)cfg.kFactor / elapsedSec) * 60.0f
                           : 0.0f;
        // 2nd pulse input: flow rate like the main sensor, or a running pulse
        // count. In deep-sleep mode both only see the measurement window - the
        // input stage is unpowered while sleeping.
        float flow2LMin = (pulse2On && cfg.pulse2Mode == 1)
                          ? ((float)pulses2 / (float)cfg.kFactor2 / elapsedSec) * 60.0f
                          : 0.0f;
        if (pulse2On && cfg.pulse2Mode == 2) pulse2Total += pulses2;
        uint8_t dinMask = cfg.dinEn ? digitalInputsRead() : 0;
        // Supply voltage (bmsMode 2) and the 0-10 V inputs: 4 ADS1115 samples each
        auto avgAnalog = [](float (*fn)(int), int arg) {
            float sum = 0.0f;
            for (int i = 0; i < 4; i++) sum += fn(arg);
            return sum / 4.0f;
        };
        float vinV = (cfg.bmsMode == 2) ? avgAnalog([](int) { return analogVinV(); }, 0) : NAN;
        float ai1V = cfg.aiEn ? avgAnalog(analogInputV, 1) : NAN;
        float ai2V = cfg.aiEn ? avgAnalog(analogInputV, 2) : NAN;

        // ---- Merge measurement values (for LPP + display) ------------------
        // Discard outliers (min + max) when enough samples are available
        if (adcCount > 4) {
            adcSum   -= adcMin + adcMax;
            adcCount -= 2;
        }
        // adcSum is in mV, so /1000 for volts
        float currentMA = (cfg.levelEn && adcCount > 0)
                          ? ((adcSum / adcCount) / 1000.0f / ADC_SHUNT_OHM) * 1000.0f
                          : 0.0f;
        // 4-20mA plausibility check: < 3.5mA or > 20.5mA = error
        bool adcOutOfRange = cfg.levelEn && (loopFault || currentMA < 3.5f || currentMA > 20.5f);

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

        // Accumulate daily statistics.
        // Real time covered by this cycle: continuous mode counts without gaps
        // (elapsedSec is the whole flow window). In deep-sleep mode the flow
        // sensor is unpowered while sleeping, so only the short sampling window
        // is measured - the window's flow rate is extrapolated over the whole
        // cycle (sleep + awake time), which makes the daily volume an estimate.
        struct timeval tvNow;
        gettimeofday(&tvNow, nullptr);
        int64_t nowSec = tvNow.tv_sec;
        float cycleSec = elapsedSec;
        if (cfg.sleepSec > 0) {
            int64_t sinceLast = nowSec - lastCycleTimeSec;
            bool plausible = lastCycleTimeSec > 0 && sinceLast > 0 &&
                             sinceLast < 4LL * (cfg.sleepSec + 600);
            cycleSec = plausible ? (float)sinceLast : (float)cfg.sleepSec + elapsedSec;
        }
        lastCycleTimeSec = nowSec;

        daily.cycleCount++;
        daily.elapsedSec += (uint32_t)lroundf(cycleSec);
        if (cfg.flowEn) {
            daily.totalLiters += (cfg.sleepSec == 0)
                                 ? (float)pulses / (float)cfg.kFactor
                                 : flowLMin * (cycleSec / 60.0f);
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

        // CH9 battery status: bmsMode 0=absent, 1=DALY status flag, 2=VIN voltage (ADS1115)
        bool  ch9Present = cfg.bmsMode == 1 || (cfg.bmsMode == 2 && !isnan(vinV));
        float ch9Value   = (cfg.bmsMode == 2) ? vinV
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
        Measurements m;
        m.hasLevel = cfg.levelEn;              m.levelMa = currentMA;
        m.hasFlow  = cfg.flowEn;               m.flowLMin = flowLMin;
        m.hasFlow2  = pulse2On && cfg.pulse2Mode == 1;  m.flow2LMin = flow2LMin;
        m.hasCount2 = pulse2On && cfg.pulse2Mode == 2;  m.count2 = pulse2Total;
        m.hasDin = cfg.dinEn;                  m.dinMask = dinMask;
        m.hasOut = true;                       m.outMask = outState;
        m.hasAi  = cfg.aiEn && !isnan(ai1V);   m.ai1V = ai1V;  m.ai2V = ai2V;
        for (int i = 0; i < 3; i++) {
            m.hasTemp[i] = (t1wire[i] != DEVICE_DISCONNECTED_C);
            m.tempC[i]   = t1wire[i];
        }
        m.hasBme = bmeOk;
        m.bmeTempC = bmeTemp;  m.bmeHumPct = bmeHum;  m.bmePresHpa = bmePres;
        m.hasBattery = ch9Present;             m.battery = ch9Value;
        m.hasVictron = sendVictron && cfg.victronEn && victronV > 0.0f;
        m.victronV = victronV;  m.victronPvW = victronP;  m.victronA = victronI;
        m.hasVictronCs    = sendVictron && victronCS >= 0;      m.victronCs = (uint8_t)victronCS;
        m.hasVictronYield = sendVictron && victronH20 >= 0.0f;  m.victronYieldKwh = victronH20;
        m.hasBms = bmsOk;
        if (bmsOk) {
            m.bmsV = bmsParser.get.packVoltage;     m.bmsSocPct = bmsParser.get.packSOC;
            m.bmsA = bmsParser.get.packCurrent;     m.bmsTempC  = bmsParser.get.tempAverage;
            m.bmsCellDiffMv = bmsParser.get.cellDiff;
        }
        m.statusFlags = statusFlags;
        m.debug = (cfg.debugMode == 1);
        m.dbgCycle = debugCycleCounter;        m.dbgSendFail = debugSendFailCounter;
        m.dbgHeapKib = dbgHeapKib;             m.dbgUptimeH = dbgUptimeH;
        m.hasDlQuality = dbgHasDlQuality;      m.dlRssiDbm = dbgDlRssi;  m.dlSnrDb = dbgDlSnr;
        m.hasLinkCheck = dbgHasLinkCheck;      m.linkMarginDb = dbgLinkMargin;
        m.linkGwCount = (uint8_t)dbgLinkGwCnt;

        if (cfg.payloadFormat == 1) binLen = encodeBinary(m, binBuf);
        else                        encodeLpp(m, lpp);

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
            if (m.hasFlow2) {
                snprintf(buf, sizeof(buf), "Flow2:%.1f L/min", flow2LMin);
                display.drawString(0, y, buf); y += 11;
            } else if (m.hasCount2) {
                snprintf(buf, sizeof(buf), "Count2:%lu", (unsigned long)pulse2Total);
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
            // 8) rest: supply voltage (ADS1115)
            else if (cfg.bmsMode == 2 && !isnan(vinV)) {
                snprintf(buf, sizeof(buf), "VIN:%.2fV", vinV);
                display.drawString(0, y, buf); y += 11;
            }
            // 9) digital inputs / switched outputs
            if (m.hasDin)
                snprintf(buf, sizeof(buf), "IN:%u%u%u OUT:%u%u%u", dinMask & 1, (dinMask >> 1) & 1, (dinMask >> 2) & 1,
                         outState & 1, (outState >> 1) & 1, (outState >> 2) & 1);
            else
                snprintf(buf, sizeof(buf), "OUT:%u%u%u", outState & 1, (outState >> 1) & 1, (outState >> 2) & 1);
            display.drawString(0, y, buf); y += 11;
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

        // ERR_NONE: already active (continuous mode), SESSION_RESTORED: RTC
        // session accepted after deep sleep, NEW_SESSION: fresh OTAA join.
        int16_t state = node->activateOTAA();
        // Every join attempt (successful or not) advances DevNonce - persist it,
        // otherwise the next power cycle would reuse nonces the network has seen.
        if (state != RADIOLIB_ERR_NONE && state != RADIOLIB_LORAWAN_SESSION_RESTORED) {
            Preferences p;
            if (p.begin("lorawan", false)) {
                p.putBytes("nonces", node->getBufferNonces(), RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
                p.end();
            }
        }
        if (state == RADIOLIB_ERR_NONE || state == RADIOLIB_LORAWAN_NEW_SESSION ||
            state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
            Serial.printf("[LoRaWAN] %s\n", state == RADIOLIB_LORAWAN_NEW_SESSION ? "Joined (new session)."
                                          : state == RADIOLIB_LORAWAN_SESSION_RESTORED ? "Session restored." : "Session active.");
            uint8_t rxBuf[256] = {};
            // RadioLib only writes the length when a downlink actually arrived -
            // must start at 0, not at the buffer size.
            size_t  rxLen = 0;

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

            // RadioLib 7.x: 0 = sent, no downlink; 1/2 = downlink received in
            // RX1/RX2; negative = error.
            if (sendState >= RADIOLIB_ERR_NONE) {
                bool requiresRestart = false;
                Serial.println("[LoRaWAN] Send successful.");
                time_t now = time(nullptr);
                if (now > 1000000)
                    Serial.printf("[Time] Synchronized: %ld\n", now);

                // If a downlink was received (also MAC-only, e.g. LinkCheckAns),
                // packet RSSI/SNR of that RX window are available from the radio.
                if (sendState > 0) {
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

                    uint8_t newOut = outState;
                    if (processParamDownlink(cfg, rxBuf, rxLen, requiresRestart)) {
                        if (requiresRestart) {
                            Serial.println("[Downlink] Region changed, restarting.");
                            delay(500);
                            ESP.restart();
                        }
                    } else if (processOutputDownlink(rxBuf, rxLen, newOut)) {
                        // Switched outputs: applied at once, persisted in NVS and
                        // held through deep sleep. The next cycle runs right away
                        // (deep-sleep mode) so the uplink confirms the new state.
                        if (newOut != outState) outputsSave(newOut);
                        outState = newOut;
                        outputsApply(outState);
                        Serial.printf("[Downlink] Outputs: OUT1=%u OUT2=%u OUT3=%u\n",
                                      outState & 1, (outState >> 1) & 1, (outState >> 2) & 1);
                        skipSleepOnce = true;  // confirm the new state with an immediate uplink
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
                                    { Preferences p; p.begin("outputs", false); p.clear(); p.end(); }  // outputs off
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

                // Daily uplink once 24 h of real time have been accumulated
                // (cycle counts drift: 288 x (300 s sleep + awake time) > 24 h).
                if (daily.elapsedSec >= 24UL * 3600UL) {
                    CayenneLPP lppDaily(30);
                    encodeDailyLpp(daily, lppDaily);
                    // Overload without downlink buffers - passing nullptr for
                    // dataDown/lenDown makes RadioLib reject the call outright.
                    int16_t dailyState = node->sendReceive(lppDaily.getBuffer(), lppDaily.getSize(), 2);
                    if (dailyState >= RADIOLIB_ERR_NONE) {
                        daily = DAILY_RESET;
                        Serial.println("[Daily uplink] sent and reset.");
                    } else {
                        // Keep the statistics - retried after the next regular uplink.
                        Serial.printf("[Daily uplink] send error: %d, retrying next cycle\n", dailyState);
                    }
                }
            } else {
                debugSendFailCounter++;
                Serial.printf("[LoRaWAN] Send error: %d\n", sendState);
            }

            // Snapshot the session *after* sending: the frame counters have
            // advanced, and restoring an older FCntUp after deep sleep would make
            // the network server drop the next uplinks as replays. Not when a
            // downlink (0x02/0x03) just asked for the portal - that path
            // deliberately drops the session.
            if (!forceConfigPortal) {
                memcpy(lorawanSession, node->getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
                sessionSaved = true;
            }
        } else {
            debugSendFailCounter++;
            Serial.printf("[LoRaWAN] OTAA error: %d\n", state);
        }

        // ---- End of cycle: deep sleep or continuous restart ----------------
        // Keep the "Sending uplink..." screen readable; without a display there
        // is nothing to wait for (an unattended timer wake would just burn 4 s).
        if (showDisplay) { delay(4000); display.clear(); display.display(); }

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
            // Release every pin that drives into the switched domain first: a HIGH I2C, 1-Wire or
            // UART TX line would back-feed the unpowered sensors / isolators through their ESD diodes.
            CustomI2C.end();
            for (int pin : { PIN_I2C_SDA, PIN_I2C_SCL, PIN_ONE_WIRE, PIN_VICTRON_TX, PIN_DALY_TX }) pinMode(pin, INPUT);
            // PIN_OPTO_SWITCH HIGH: powers off DS18B20, pulse/digital input stages (if JP9/JP10 = switched), I2C domain, ADuM121N1 (3.3V)
            // and flow sensor (5V via VCC3.3_SW → TN2106N3-G → LP0701N3(2))
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

        // Wait for user button to be released to prevent an immediate EXT0
        // wakeup loop - but not forever: the watchdog is already detached, so
        // a stuck button or moisture on GPIO0 would otherwise keep the node
        // awake indefinitely. If it is still low after 10 s, sleep on the
        // timer alone and skip the button wake source for this cycle.
        unsigned long releaseWaitStart = millis();
        while (digitalRead(PIN_USER_BUTTON) == LOW && millis() - releaseWaitStart < 10000) delay(10);
        bool buttonReleased = digitalRead(PIN_USER_BUTTON) == HIGH;
        delay(50);  // debounce

        esp_sleep_enable_timer_wakeup((uint64_t)cfg.sleepSec * 1000000ULL);
        if (!buttonReleased) Serial.println("[Sleep] User button stuck low - button wake disabled this cycle.");
        // Wake on input changes (dinWake, JP9 at 1-2) unless the last
        // INPUT_WAKE_MAX wakes all came from the inputs.
        uint8_t wakeInputs = (cfg.dinEn && inputWakeStreak < INPUT_WAKE_MAX) ? cfg.dinWake : 0;
        if (cfg.dinEn && cfg.dinWake && !wakeInputs)
            Serial.println("[Sleep] Inputs keep changing - input wake paused for one timer cycle.");
        sleepArmWake(wakeInputs, buttonReleased);
        // Keep the switched outputs at their level through deep sleep.
        outputsPrepareSleep();
        nodeDeepSleep();
    }
}

void loop() {}
