#include "board_io.h"
#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <soc/usb_serial_jtag_reg.h>
#include "hardware.h"

// ============================================================================
// SWITCHED OUTPUTS (J17)
// ============================================================================
static constexpr int OUT_PINS[3] = { PIN_OUT1, PIN_OUT2, PIN_OUT3 };
static constexpr uint8_t OUT_MASK = 0x07;

uint8_t outputsLoad()
{
    Preferences p;
    if (!p.begin("outputs", true)) return 0;  // namespace not created yet = all off
    uint8_t mask = p.getUChar("state", 0) & OUT_MASK;
    p.end();
    return mask;
}

void outputsSave(uint8_t mask)
{
    Preferences p;
    p.begin("outputs", false);
    p.putUChar("state", mask & OUT_MASK);
    p.end();
}

void outputsApply(uint8_t mask)
{
    // GPIO19/20 are USB D-/D+ of the internal USB-Serial-JTAG PHY. The Heltec
    // V3 uses the CP2102 for USB, so detach the PHY from the pads - otherwise
    // GPIO19 (OUT1) does not follow the GPIO matrix and GPIO20 (HW ID) can't be read.
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
    // Set the level while the pad may still be held from deep sleep, then
    // release the hold: the output never drops in between.
    for (int i = 0; i < 3; i++) {
        pinMode(OUT_PINS[i], OUTPUT);
        digitalWrite(OUT_PINS[i], (mask & (1 << i)) ? HIGH : LOW);
        gpio_hold_dis((gpio_num_t)OUT_PINS[i]);
    }
}

void outputsPrepareSleep()
{
    // OUT1-OUT3 are RTC GPIOs: their pad hold lasts through deep sleep on its
    // own, without latching the other pads (no gpio_deep_sleep_hold_en).
    for (int pin : OUT_PINS) gpio_hold_en((gpio_num_t)pin);
}

// ============================================================================
// DIGITAL INPUTS (J18)
// ============================================================================
static constexpr int DIN_PINS[3] = { PIN_DIN1, PIN_DIN2, PIN_DIN3 };

void digitalInputsBegin()
{
    // Push-pull Schmitt-trigger outputs - no internal pull needed.
    for (int pin : DIN_PINS) pinMode(pin, INPUT);
}

uint8_t digitalInputsRead()
{
    // The Schmitt trigger inverts: terminal high = GPIO LOW.
    uint8_t mask = 0;
    for (int i = 0; i < 3; i++)
        if (digitalRead(DIN_PINS[i]) == LOW) mask |= (1 << i);
    return mask;
}

// ============================================================================
// DEEP-SLEEP WAKE (button + inputs)
// ============================================================================
// Pin armed on EXT0 for the last sleep (button or one input), to tell the
// two apart after the wake.
RTC_DATA_ATTR static int8_t ext0Pin = -1;

uint8_t sleepArmWake(uint8_t inputMask, bool button)
{
    // Wake on change = wake on the level opposite to the current one. Pins
    // that are HIGH now wake on LOW (like the idle-HIGH button), pins that
    // are LOW now wake on HIGH.
    uint64_t wakeLow = 0, wakeHigh = 0;
    int nLow = 0, nHigh = 0, lastLow = -1, lastHigh = -1;
    for (int i = 0; i < 3; i++) {
        if (!(inputMask & (1 << i))) continue;
        int pin = DIN_PINS[i];
        pinMode(pin, INPUT);
        if (digitalRead(pin) == HIGH) { wakeLow  |= 1ULL << pin; nLow++;  lastLow  = pin; }
        else                          { wakeHigh |= 1ULL << pin; nHigh++; lastHigh = pin; }
    }

    uint64_t ext1 = 0;
    // Mode 0 is "any low" on the ESP32-S3 (IDF 4.4 still names it ALL_LOW,
    // after the ESP32 behaviour; IDF 5 renamed it ESP_EXT1_WAKEUP_ANY_LOW).
    const esp_sleep_ext1_wakeup_mode_t ANY_LOW = (esp_sleep_ext1_wakeup_mode_t)0;
    esp_sleep_ext1_wakeup_mode_t mode = ANY_LOW;
    ext0Pin = -1;
    int ext0Level = 0;
    bool buttonArmed = button;
    if (nHigh == 0) {                      // all wake on LOW: button on EXT0
        ext1 = wakeLow;
        if (button) ext0Pin = PIN_USER_BUTTON;
    } else if (nLow == 0) {                // all wake on HIGH: button on EXT0
        ext1 = wakeHigh; mode = ESP_EXT1_WAKEUP_ANY_HIGH;
        if (button) ext0Pin = PIN_USER_BUTTON;
    } else if (nHigh == 1) {               // button joins the LOW group
        ext1 = wakeLow | (button ? 1ULL << PIN_USER_BUTTON : 0);
        ext0Pin = lastHigh; ext0Level = 1;
    } else {                               // 2 x HIGH + 1 x LOW: no room for the button
        ext1 = wakeHigh; mode = ESP_EXT1_WAKEUP_ANY_HIGH;
        ext0Pin = lastLow;
        buttonArmed = false;
    }

    if (ext0Pin >= 0) esp_sleep_enable_ext0_wakeup((gpio_num_t)ext0Pin, ext0Level);
    if (ext1) esp_sleep_enable_ext1_wakeup(ext1, mode);
    if (button && !buttonArmed)
        Serial.println("[Sleep] Inputs at mixed levels - button wake disabled this cycle.");
    return inputMask & 0x07;
}

WakeSource sleepWakeSource()
{
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER: return WAKE_TIMER;
        case ESP_SLEEP_WAKEUP_EXT0:  return ext0Pin == PIN_USER_BUTTON ? WAKE_BUTTON : WAKE_INPUT;
        case ESP_SLEEP_WAKEUP_EXT1:
            return (esp_sleep_get_ext1_wakeup_status() & (1ULL << PIN_USER_BUTTON)) ? WAKE_BUTTON : WAKE_INPUT;
        default: return WAKE_RESET;
    }
}

// ============================================================================
// ADS1115 (VIN, 4-20 mA, AI1/AI2)
// ============================================================================
static Adafruit_ADS1115 ads;
static bool adsOk = false;

bool analogBegin(TwoWire& wire)
{
    adsOk = ads.begin(ADS_ADDR, &wire);
    if (adsOk) {
        ads.setGain(GAIN_ONE);                     // +/-4.096 V, inputs stay below 3.3 V
        ads.setDataRate(RATE_ADS1115_860SPS);      // single shot in ~1.2 ms
    } else {
        Serial.println("[ADS] ADS1115 not found - analog inputs disabled");
    }
    return adsOk;
}

static float readVolts(uint8_t ch)
{
    if (!adsOk) return NAN;
    return ads.computeVolts(ads.readADC_SingleEnded(ch));
}

float analogLevelMv()      { return readVolts(ADS_CH_LEVEL) * 1000.0f; }
float analogVinV()         { return readVolts(ADS_CH_VIN) * VIN_DIVIDER; }
float analogInputV(int n)  { return readVolts(n == 2 ? ADS_CH_AI2 : ADS_CH_AI1) * AI_DIVIDER; }
