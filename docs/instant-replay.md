# Instant Replay (v1.8.0)

## Behavior

Opt-in rolling Screen/Window capture, 30/60/90/120/180/240/300-second presets, six Ctrl+Shift / Ctrl+Alt F8–F10 global shortcut presets, in-app Save clip and Open last clip controls. The native host owns capture and shortcuts; React only sends commands and receives small status messages. Replay is always off at launch; only duration/shortcut are persisted. Starting at login never starts capture.

Replay takes a fixed quality/source/audio snapshot when enabled. Disable before changing these preferences. Normal recording is independent; its pause does not pause replay. One clip export can run at a time, without blocking recording controls or queuing extra hotkey presses. Close waits for export, stops replay, then finishes saving normal recording. Update installation is blocked until both recording and replay are stopped.

## Pipeline and bounds

- Continuous encoder writes two-second keyframe-aligned Matroska chunks; encoder/source are not restarted at boundaries.
- Windows WASAPI packets are split into RF64 chunks by sample count without reopening the audio device. Headers close before `.done` markers appear; silent periods advance the capture clock too.
- Debian's independent Pulse FFmpeg processes write PCM WAV segments with timestamp catalogs; audio cannot block the raw-video encoder's input queue.
- Bounded catalogs retain 162 closed entries. Retention removes expired chunks, including closed entries that aged out of the catalog after slow snapshot copying. The requested window includes up to 20 seconds of housekeeping margin and an open chunk, not an unlimited backlog.
- A hotkey records an endpoint immediately. Export waits for the next completed chunks (bounded 15-second readiness wait), pins/copies immutable inputs, then trims video/audio to that endpoint. It excludes later frames even though capture continues. Software H.264 export gives accurate trimming between keyframes and may be slow for long/high-resolution clips.
- Clip audio is mixed into a finite, clock-aligned PCM track before video/AAC export. No infinite audio padding source is needed for replay chunks; removing that combined concat/padding path fixed the intermittent Windows CI export stall. Both backends use the finite mix, including video-only and single-audio branches.
- NTFS/hard-link-capable Linux filesystems share snapshot data without copying. Other filesystems copy owned chunks. Conservative free-space checks allow for snapshot/export overhead; low remaining space stops replay with recovery retained.
- Healthy Windows exports explicitly bypass the child helper's default 30-second timeout; the asynchronous worker keeps the UI responsive. Failed exports retain their private snapshot directory. Final MP4 publication never overwrites an existing file. Disabling clears only the app-created buffer, not completed clips or failed export snapshots.
- Windows session directories are created exclusively with a process-wide atomic serial. Concurrent manual-recording and clip workers cannot accidentally share a timestamp-only folder or delete one another's media; a 40-worker concurrency test covers this boundary.

## Verification

Local Windows C++ synthetic and Linux backend synthetic tests exercised every 30-second–5-minute preset, dual audio, exact duration, post-hotkey frame exclusion, unique publication, expired-buffer cleanup, corrupt-input recovery and apostrophe/Urdu/emoji output paths. Windows PCM splitting, preferences and hotkey normalization are tested without recording the user's desktop or microphone. UI tests cover malformed replay state, partial updates, bounded presets and update-install guards.

Browser fixtures checked duration/shortcut choices, enable/save commands, active status, timer, frozen capture settings, manual recording availability during clip export, disabled duplicate-save controls, and no horizontal overflow at 1040×740, 760×600, 620×600 and 360×640. No page errors were observed.

Debian CI [38256067088](https://github.com/mmaarij123/CamCord/actions/runs/38256067088) passed actual installed Xvfb replay: native toggle, OS-registered Ctrl+Shift+F8 injected into that isolated virtual display, decodable saved clip while replay stayed active, and disable/shortcut cleanup. That first combined run exposed the Windows 30-second export timeout, which was fixed before release. The final release pipeline repeats the full platform suites; test results must be checked at the final release SHA, not inferred from this initial run.

Combined CI [38261236266](https://github.com/mmaarij123/CamCord/actions/runs/38261236266) passed both platforms after the finite-audio correction, all seven native/Linux duration presets, the 40-worker directory test, Windows global shortcut registration/cleanup, UI/normal-recording regression suites, actual installed Debian hotkey/clip tests and clean Debian 12/13 dependency checks. Synthetic fixture exports have a two-minute diagnostic limit and the CI step is bounded; production Windows clip exports retain their unlimited healthy-drain default.

## Limits

Physical GNOME/KDE Wayland replay/global shortcuts and individual GPU/driver/exclusive-fullscreen game combinations are not validated here. Wayland needs working ScreenCast and GlobalShortcuts portals and desktop permissions; desktop-reserved/conflicting shortcuts show a failure rather than silently stealing a binding. It is not NVIDIA's driver-level game capture. Running manual recording alongside replay uses extra encoding resources, and Wayland may ask for a second source approval.

Tests use generated tiny-frame media for the full five-minute duration matrix, not physical five-minute high-motion 1080p/120-FPS captures. CI's installed Linux smoke test uses an isolated root-owned container with an explicit test-only no-sandbox invocation; production remains normal-user/sandboxed. Suspend/logout, unusual removable/network filesystems, hardware saturation and anti-cheat environments require physical-machine validation.

## API references

[FFmpeg segment muxer](https://ffmpeg.org/ffmpeg-formats.html#segment_002c-stream_005fsegment_002c-ssegment), [Windows RegisterHotKey](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey), and [Electron globalShortcut / Wayland portal](https://www.electronjs.org/docs/latest/api/global-shortcut) define the segment and shortcut contracts used by these backends.
