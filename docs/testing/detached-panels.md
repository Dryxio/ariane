# Windows detachable panels — tester checklist

This build enables native detached panels on Windows / Direct3D9. Drag the title
bar of **Window > View** beyond the main window, onto a second monitor. Drag it
fully back inside the main window to reattach. Other tool panels use the same
infrastructure. macOS/Linux keep their existing single-window behavior.

Use **Window > Bring panels back to main window** to recover misplaced panels.
Window positions are saved in the editor's existing `imgui.ini`.

## Acceptance checks

- Drag View, Time & Weather, Object Info and Object Browser out and back. Resize
  and close a detached panel, then reopen it from Window. Closing a panel must
  not close the editor.
- Change weather/rendering settings and numeric object properties in detached
  panels. Verify the main scene updates and text editing does not move the camera
  or trigger editor shortcuts. Check Ctrl+A/C/V and scrolling in text fields.
- Check Object Browser thumbnails and the rotating model preview on both screens.
- Move the main window to a different monitor, including one to the left or above
  the primary monitor. Test translation/rotation, water gizmos, rectangle
  selection, placement/brush markers and notification positions.
- Test monitors at 100%, 125%, 150% or 200% scaling. Text should remain readable,
  and pointer hit targets should match their visible positions after moving.
- Repeatedly resize/maximize/restore the main window with panels detached. Alt+Tab
  out and back, minimize/restore, and lock/unlock Windows. Look for crashes,
  frozen panels, missing text, corrupted previews or changes in scene rendering.
- Restart with detached panels saved; disconnect/reconnect the second display and
  try Bring panels back. Confirm every panel can be recovered.
- Close Ariane both via File > Exit and the native close button with panels open.

Report Windows version, GPU/driver, monitor arrangement/scaling, game, executable
SHA-256, reproduction steps, and a screenshot/video for each failure. A clean CI
build and static review do not replace these real multi-monitor checks.

## Implementation and build

Ariane's workflow pins the matching librw commit. That dependency contains ImGui
v1.92.2b-docking, the official Win32/DX9 backends with a librw texture-ID bridge,
and a device-resource release hook used before resets. The app enables these
backends only for its Windows D3D9 target. Use the `LIBRW_REF` from
`.github/workflows/build-euryopa.yml` when building locally.

PR/manual Actions runs produce the `ariane-windows-d3d9` artifact without creating
a public release. The ordinary release version is intentionally unchanged.
