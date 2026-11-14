// WiFiManager configuration portal (SSID "WaterNode") with OTA and the
// /decoder download route.
#pragma once
#include "config.h"

// Blocks until the portal is closed (save, timeout or abort) and returns the
// resulting configuration. The caller persists it and restarts.
Config runConfigPortal(Config& cur);
