#pragma once

// Ariane version — bump this when releasing updates.
// Keep this monotonic for the updater: after 1.29, use 1.30, not 1.3.
#define ARIANE_VERSION "1.32"

// Update channel — set at build time via preprocessor define
// Defaults to "master" if not set by the build system
#ifndef ARIANE_CHANNEL
#define ARIANE_CHANNEL "master"
#endif

// Display name for the channel (shown in title bar / update popup)
#define ARIANE_CHANNEL_DISPLAY (strcmp(ARIANE_CHANNEL, "PE") == 0 ? "FLA" : ARIANE_CHANNEL)
