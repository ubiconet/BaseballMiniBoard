#pragma once

#include <Arduino.h>
#include <WebServer.h>

// Network service — Wi-Fi provisioning state machine, captive setup portal,
// NVS-backed preferences, and development (Arduino)OTA. Fully generic: the
// selected profile injects its branding and optional manual-control routes.

struct NetworkBranding {
  const char* deviceName;  // portal <title>/heading
  const char* apSsid;      // fallback provisioning AP network name
  const char* hostname;    // base hostname; a per-device suffix is appended
  const char* const* countLedLabels;  // seven labels for the display-test page
  const char* const* matrixLabels;    // three labels in physical order
};

using PortalRouteRegistrar = void (*)(WebServer& portal);

void startNetworkServices(const NetworkBranding& branding,
                          PortalRouteRegistrar registerManualRoutes);
void startNetworkTask();
// Runs on the main loop; performs any pending display work owned by that core.
void handleNetworkDisplay();

bool isOnline();
// True while the device runs its own setup AP with no usable saved Wi-Fi —
// the setup portal remains available in that state.
bool isProvisioning();
