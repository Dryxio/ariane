#pragma once

// Native, main-thread control primitives for local visual agent loops. The
// engine owns scene safety and rendering; CLI/MCP concerns stay out-of-process.
void AgentBridgeUpdate(void);
void AgentBridgeCaptureAfterWorldRender(void);
