# Capture source audit — 1.6.0

Historical report: Area capture was removed in 1.6.2 at the user's request.
Current versions offer Screen and Window only.

## Features and safety

- Screen, Window and Area modes. Each connected display has an explicit backend identity; windows are selected by exact HWND/process/thread/class identity, never title regex.
- Windows Graphics Capture supplies source pixels. The selected window remains separate from overlapping windows. Area selection uses physical, display-relative coordinates, including displays at negative desktop positions.
- Native drag selector clamps to one selected display, supports reverse drags, requires at least 16×16 pixels, and preserves an existing selection on Esc/right-click/invalid drag. Display changes cancel the picker. Instructions scale with display DPI.
- Sources are session-only. Restart selects the current primary display instead of reusing stale window/monitor handles. Video quality, bitrate, audio and save-folder preferences remain persisted.
- Switching Screen ↔ Area retains the explicitly chosen display; changing the area display invalidates the previous rectangle and requires a new drag.
- Source changes are blocked while recording, paused, saving or selecting an area. Start is blocked until a valid source is selected. Missing, closed, hidden, minimized or disconnected sources require an explicit re-selection; they never silently fall back to desktop capture.
- Window destruction events invalidate selection even if Windows later recycles a handle. System audio remains full desktop playback, not per-window audio. Protected apps may refuse capture.

## Saving-path fix discovered during testing

FFmpeg's direct static-window capture could wait indefinitely for another source frame when quitting. Capture now uses a separate unencoded BGRA producer and a bounded latest-frame cache. The producer uses passthrough timing to avoid queuing duplicated old frames; a clocked writer repeats static frames to preserve video duration. Stopping cancels only that raw producer; the video encoder receives clean pipe EOF and drains without a production timeout. Encoded MP4 data is never force-terminated. Audio trim/elapsed timing starts with the actual encoded-video stream.

## Verification

- Native tests: monitor identity, window identity/closed/hidden rejection, reverse/outside/minimum/overflow area geometry and synthetic crop/scale encoding/decode.
- Local real-window test: generated blue content changed to green behind a red cover. The final decoded video remained green, proving live selected-window capture rather than desktop crop or a frozen initial frame. Starting while already occluded and closing the source also produced decodable recordings. No user's desktop or microphone was captured.
- The same real-window test passed both software H.264 and local NVIDIA NVENC at 1080p/120 FPS with a 100 Mbps target. AMD/Intel live hardware was not available; their shared encoding flags remain covered by bitrate tests.
- UI tests: modes, explicit display/window selection, empty-source states, unavailable-source re-selection, area selection/cancel state, Start gate and source locks during starting/recording/pause/saving.
- Visual checks at 1040×740, 760×600, 480×640 and 360×640 showed no horizontal overflow. Menus escape the scrolling panel through MUI portals. Existing dark/coral identity and keyboard-accessible controls are preserved using frontend-design/React guidance.
- Existing recording finalization/recovery, settings, startup/updater and bitrate regression suites are retained and run before releases.

Live tests use temporary synthetic windows only. Full desktop capture and multi-monitor hardware/physical drag interactions are not recorded by the automated test suite; their source mapping and geometry are validated separately. Minimized/hidden windows stop and save; this release does not promise background capture of minimized or protected windows.
