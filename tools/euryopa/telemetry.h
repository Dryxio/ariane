#pragma once

// Lightweight telemetry — sends a single anonymous ping at startup
// to track daily active users. No personal data is collected.

// Returns whether anonymous telemetry is currently enabled.
bool TelemetryIsEnabled(void);

// Persists the telemetry setting across restarts.
void TelemetrySetEnabled(bool enabled);

// Call once after RW init (non-blocking, spawns a background thread)
// if telemetry is enabled.
void TelemetrySendPing(void);
