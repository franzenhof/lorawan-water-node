// Pulse counters (ESP32 PCNT, legacy ESP-IDF v4 driver) with a 32-bit
// software extension of the 16-bit hardware counter. One PCNT unit per input:
// the flow sensor (J4 OC1) and the 2nd pulse input (J4 OC2).
#pragma once
#include <stdint.h>

enum PulseInput : uint8_t { PULSE_FLOW = 0, PULSE_2 = 1 };

void     pcnt_init(PulseInput in);
uint32_t pcnt_read_peek(PulseInput in);
void     pcnt_clear_keep_running(PulseInput in);
uint32_t pcnt_read_and_stop(PulseInput in);
