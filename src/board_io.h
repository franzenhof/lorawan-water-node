// The v1.0 I/O block: three switched outputs (J17), three digital inputs
// (J18) and the ADS1115 analog inputs.
#pragma once
#include <stdint.h>

class TwoWire;

// Switched outputs, bit 0 = OUT1, bit 1 = OUT2, bit 2 = OUT3. The commanded state is kept
// in NVS, so it survives deep sleep, resets and power loss.
uint8_t outputsLoad();
void    outputsSave(uint8_t mask);
// Drives the outputs and releases a deep-sleep pad hold without a glitch.
void    outputsApply(uint8_t mask);
// Holds the outputs at their current level through deep sleep.
void    outputsPrepareSleep();

// Digital inputs, bit 0 = IN1, bit 1 = IN2, bit 2 = IN3. 1 = terminal high
// (open input with the pull-up, or a voltage applied), 0 = pulled to GND
// (e.g. closed dry contact).
void    digitalInputsBegin();
uint8_t digitalInputsRead();

// Deep-sleep wake sources: the user button (LOW) and every input in
// inputMask (bit 0 = IN1) on any level change. The ESP32-S3 only has one
// EXT0 pin plus one EXT1 group with a common level, so all four can't
// always be covered at once: inputs win over the button, the rest is
// logged. Returns the inputs actually armed. Needs JP9 at 1-2.
uint8_t sleepArmWake(uint8_t inputMask, bool button);

enum WakeSource { WAKE_RESET, WAKE_TIMER, WAKE_BUTTON, WAKE_INPUT };
// Why the chip left deep sleep (WAKE_RESET = power-on or any reset).
WakeSource sleepWakeSource();

// ADS1115 on the switched 3.3 V I2C domain. analogBegin() needs VCC3.3
// switched on (GPIO38 LOW) and the bus started; it returns false if the ADC
// doesn't answer, after which every read returns NAN.
bool  analogBegin(TwoWire& wire);
float analogLevelMv();      // 4-20 mA shunt voltage, mV
float analogVinV();         // supply voltage at J3, V
float analogInputV(int n);  // J18 AI1 (n = 1) / AI2 (n = 2), 0-10 V
