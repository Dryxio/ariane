#pragma once

// Auto-updater for Ariane
// Checks GitHub Releases for the current channel (master or PE)
// and offers to download + apply the update.

// Call once after the window is created (non-blocking, spawns a thread)
void UpdaterCheckForUpdate(void);

// Call every frame from the GUI — renders the update popup if an update is available
void UpdaterDrawGui(void);
