#include "flow_counter.h"
#include <Arduino.h>
#include <driver/pcnt.h>
#include "hardware.h"

static constexpr pcnt_unit_t PCNT_UNITS[2] = { PCNT_UNIT_0, PCNT_UNIT_1 };
static constexpr int         PCNT_PINS[2]  = { PIN_FLOW_SENSOR, PIN_PULSE2 };

// ============================================================================
// PCNT (flow / counter pulses)
// ============================================================================
// The hardware counter is only 16 bit: it resets to 0 when it reaches
// PCNT_H_LIM. An H_LIM event interrupt counts those wraps so a long
// continuous-mode window (e.g. K=450, 300 s, >14.6 L/min) isn't undercounted.
constexpr int16_t PCNT_H_LIM = 32767;
static volatile uint32_t pcntOverflows[2] = { 0, 0 };

static void IRAM_ATTR pcnt_overflow_isr(void* arg)
{
    uint32_t in = (uint32_t)arg;
    uint32_t status = 0;
    pcnt_get_event_status(PCNT_UNITS[in], &status);
    if (status & PCNT_EVT_H_LIM) pcntOverflows[in]++;
}

// Counts rising edges: the Schmitt trigger inverts, so a pulse is HIGH and
// each pulse has exactly one rising edge.
void pcnt_init(PulseInput in)
{
    pcnt_unit_t unit = PCNT_UNITS[in];
    pcnt_config_t cfg = {
        .pulse_gpio_num = PCNT_PINS[in],   .ctrl_gpio_num = PCNT_PIN_NOT_USED,
        .lctrl_mode = PCNT_MODE_KEEP,      .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_INC,        .neg_mode = PCNT_COUNT_DIS,
        .counter_h_lim = PCNT_H_LIM,       .counter_l_lim = -32768,
        .unit = unit,                      .channel = PCNT_CHANNEL_0
    };
    pcnt_unit_config(&cfg);
    pcnt_set_filter_value(unit, 1000);
    pcnt_filter_enable(unit);

    static bool serviceInstalled = false;
    static bool handlerAdded[2]  = { false, false };
    if (!serviceInstalled) {
        pcnt_isr_service_install(0);
        serviceInstalled = true;
    }
    if (!handlerAdded[in]) {
        pcnt_isr_handler_add(unit, pcnt_overflow_isr, (void*)(uint32_t)in);
        handlerAdded[in] = true;
    }
    pcnt_event_enable(unit, PCNT_EVT_H_LIM);

    pcnt_counter_pause(unit);
    pcnt_counter_clear(unit);
    pcntOverflows[in] = 0;
    pcnt_counter_resume(unit);
}

// Non-destructive read: total pulses since the last clear, including wraps.
// Re-reads if a wrap happened between reading the overflow count and the
// counter value.
uint32_t pcnt_read_peek(PulseInput in)
{
    uint32_t ovBefore, ovAfter;
    int16_t  v = 0;
    do {
        ovBefore = pcntOverflows[in];
        pcnt_get_counter_value(PCNT_UNITS[in], &v);
        ovAfter = pcntOverflows[in];
    } while (ovBefore != ovAfter);
    return ovAfter * (uint32_t)PCNT_H_LIM + (uint32_t)max<int16_t>(v, 0);
}

// Clears the pulse counter without stopping it. Used in continuous mode to
// define seamless measurement windows while counting stays active.
void pcnt_clear_keep_running(PulseInput in)
{
    pcnt_counter_clear(PCNT_UNITS[in]);
    pcntOverflows[in] = 0;
}

uint32_t pcnt_read_and_stop(PulseInput in)
{
    pcnt_counter_pause(PCNT_UNITS[in]);
    return pcnt_read_peek(in);
}
