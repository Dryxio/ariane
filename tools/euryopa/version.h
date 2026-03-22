#pragma once

// Ariane version — bump this when releasing updates
#define ARIANE_VERSION "1.0.0"

// Update channel — set at build time via preprocessor define
// Defaults to "master" if not set by the build system
#ifndef ARIANE_CHANNEL
#define ARIANE_CHANNEL "master"
#endif
