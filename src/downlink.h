// Parameter downlink (0x10 + [paramId][int16 BE] tuples) and output downlink
// (0x20), shared with the README "Downlink commands" table.
#pragma once
#include "config.h"

// Applies all recognised parameters, persists them, and returns true if any
// was applied. requiresRestart is set when the LoRaWAN region changed.
bool processParamDownlink(Config& cfg, const uint8_t* buf, size_t len, bool& requiresRestart);

// Output downlink (0x20 + [output 1|2|3][state 0|1] pairs). Updates outMask
// (bit 0 = OUT1, bit 1 = OUT2, bit 2 = OUT3) and returns true for a valid command; an
// invalid pair rejects the whole command and leaves outMask unchanged.
bool processOutputDownlink(const uint8_t* buf, size_t len, uint8_t& outMask);
