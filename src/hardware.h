// Shared by every module. Pins and fixed hardware constants of the
// lorawan-water-node PCB v1.0 (Heltec WiFi LoRa 32 V3).
#pragma once
#include <Arduino.h>

// ============================================================================
// PIN ASSIGNMENT
// ============================================================================
// Every input, pulse input and output sits on an RTC GPIO (0-21): inputs can
// wake the chip from deep sleep, outputs are held per pin through deep sleep.
// UARTs, 1-Wire and I2C use the remaining (non-RTC) pins.
constexpr int PIN_USER_BUTTON      =  0;  // After boot: hold 3 s → config portal; during sleep: EXT0 wakeup
constexpr int PIN_OUT3             =  1;  // J17 OUT3 low-side switch (3 A), HIGH = on (driver input pull-down)
                                          //   GPIO1 also carries the Heltec VBAT divider -> output only
constexpr int PIN_OUT2             =  2;  // J17 OUT2, same as OUT3
constexpr int PIN_DIN1             =  3;  // J18 IN1 via Schmitt trigger, inverted: terminal high = GPIO LOW
constexpr int PIN_DIN2             =  4;  // J18 IN2, same as IN1
constexpr int PIN_FLOW_SENSOR      =  5;  // J4 OC1 flow sensor pulses via Schmitt trigger (pulse = HIGH)
constexpr int PIN_PULSE2           =  6;  // J4 OC2, 2nd pulse input, same as OC1
constexpr int PIN_DIN3             =  7;  // J18 IN3, same as IN1
constexpr int PIN_OUT1             = 19;  // J17 OUT1, same as OUT3 (board order: OUT1 | OUT2 | OUT3 = GPIO19 | 2 | 1)
constexpr int PIN_HW_ID            = 20;  // revision ID divider from VCC3.3 switched (ADC2), v1.0: 0.5 x 3V3
constexpr int PIN_VICTRON_RX       = 33;  // VE.Direct UART RX (from Victron TX)
constexpr int PIN_VICTRON_TX       = 34;  // VE.Direct UART TX
constexpr int PIN_OPTO_SWITCH      = 38;  // VCC3.3 switched + 5V flow sensor rail, LOW = on:
                                          //   GPIO38 → AO3401A gate (direct)
                                          //   VCC3.3_SW → AO3400A → AO3401A → flow sensor 5V rail
                                          //   (the N-MOSFET re-inverts so the 5V rail also turns on when GPIO38 LOW)
                                          //   VCC3.3_SW also powers the I2C domain (BME280, ADS1115) and
                                          //   switches the VIN divider of the ADS1115
constexpr int PIN_ONE_WIRE         = 39;  // DS18B20 1-Wire bus
constexpr int PIN_DALY_RX          = 40;  // DALY BMS UART RX
constexpr int PIN_I2C_SDA          = 41;  // BME280, ADS1115
constexpr int PIN_I2C_SCL          = 42;  // BME280, ADS1115
constexpr int PIN_DALY_TRIGGER     = 45;  // DALY BMS S1 wakeup (via JP5 + PC817, inverted); strapping pin, pull-down
constexpr int PIN_HIGH_SIDE_SWITCH = 47;  // HIGH = on: 5 V -> MT3608 boost (22 V supply of the 4-20 mA loop)
constexpr int PIN_DALY_TX          = 48;  // DALY BMS UART TX

// PC817 inverted: GPIO HIGH -> LED conducts -> S1 pulled LOW -> BMS wakes up
constexpr int DALY_S1_ACTIVE   = HIGH;
constexpr int DALY_S1_INACTIVE = LOW;

// ============================================================================
// ADS1115 (I2C 0x48, on VCC3.3 switched)
// ============================================================================
constexpr uint8_t ADS_ADDR      = 0x48;
constexpr uint8_t ADS_CH_VIN    = 0;      // VIN via 100k / 10k (switched by Q8)
constexpr uint8_t ADS_CH_LEVEL  = 1;      // 4-20 mA shunt R4 via 1k
constexpr uint8_t ADS_CH_AI1    = 2;      // J18 AI1, 0-10 V via 100k / 47k
constexpr uint8_t ADS_CH_AI2    = 3;      // J18 AI2, same as AI1
constexpr float   VIN_DIVIDER   = 11.0f;              // (100k + 10k) / 10k
constexpr float   AI_DIVIDER    = 147.0f / 47.0f;     // (100k + 47k) / 47k

// ============================================================================
// CONSTANTS
// ============================================================================
constexpr uint32_t WAKEUP_DELAY_MS      = 2000;
constexpr uint32_t DALY_WAKEUP_PULSE_MS =  200;
constexpr float    ADC_SHUNT_OHM        = 120.0f;  // R4 = 120 Ohm (max 20mA -> 2.4V)
constexpr float    LOOP_OVERCURRENT_MV  = 2760.0f; // 23 mA: shorted / faulty loop -> boost off (protects the 5 V rail)
// Hardware revision ID on GPIO20: divider from VCC3.3 switched (prototype: none, v1.0: 100k / 100k)
constexpr uint32_t HW_ID_V10_MIN_MV     = 1300;
constexpr uint32_t HW_ID_V10_MAX_MV     = 2000;
