#pragma once

// Lightweight telemetry — sends a single anonymous ping at startup
// to track daily active users. No personal data is collected.

// Call once after RW init (non-blocking, spawns a background thread)
void TelemetrySendPing(void);
