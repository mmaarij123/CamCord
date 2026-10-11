# Debian port: v1.7.0 verification

## Scope

CamCord's Debian amd64 package reuses the Windows React/Material UI, styles, logo, fonts and shared bitrate logic. The Linux backend is separate: Electron owns trusted commands and preferences, a Python/GStreamer helper captures X11 or approved Wayland streams, and child FFmpeg processes encode and finalize MP4 files. Video frames never travel through the renderer.

Screen and Window are the only capture modes. Resolution presets, bitrate presets through 100 Mbps with storage warnings, audio toggles, pause/resume, custom output folders, background saving, verified updates and optional login startup are retained. Linux encoder probing covers NVENC, QSV, VAAPI and software H.264. Hardware-specific success is not claimed without the relevant GPU/driver.

## Automated evidence

GitHub Actions run [38155907725](https://github.com/mmaarij123/CamCord/actions/runs/38155907725) passed the Debian build and integration gates on October 11, 2026:

- Core tests: bounded fragmented-frame buffers, shared bitrate flags, strict settings, Unicode folders, atomic preferences, isolated opt-in startup, stable release metadata, cached/streamed update hash checks, oversize/tamper rejection and cancelled-partial cleanup.
- Actual X11 capture: changing an owned window behind another window, pause/resume, system audio plus microphone, audible tone with leading silence, full MP4 decoding, source-close recovery, stale-source rejection and display capture.
- Isolated ScreenCast D-Bus fixture: screen/window types, hidden cursor, request routing/cancellation, Unix file descriptor handoff and portal v6 stable PipeWire serial targeting.
- Publication: no-overwrite atomic rename fallback, output collisions, apostrophe paths and retained corrupt media for recovery.
- Installed Electron smoke test: shared React UI, native-ready handshake, Screen/Window controls, Linux startup label and a persisted FPS-setting action.
- Clean Debian 12 and 13 dependency installs, runtime library resolution, sandbox ownership/mode and package removal.

The disposable CI container uses a virtual desktop and virtual audio devices, never a user's screen or microphone. Its root-only Electron smoke invocation explicitly disables the sandbox; the installed production application does not do this. The package installs the standard root-owned mode-4755 Electron sandbox helper and is intended to run as a normal user.

The tag release pipeline repeats these Linux gates and Windows validation, UI, settings, branding, all-bitrate synthetic encoding, source, recording recovery, startup/updater and native lifecycle tests before publication.

Shared UI tests pass locally (14 tests). Browser fixture checks at 1040×740, 760×600 and 360×640 found no horizontal overflow; Linux/Windows startup text, portal instructions and source-change control locking were checked. These fixtures verify UI behavior, not physical-device capture. Production npm dependency audits reported no known advisories at the time of verification; this is not a guarantee against undiscovered vulnerabilities.

## Fixes found during implementation

- Corrected XComposite pixmap visual masks for valid window frames.
- Replaced repeated whole-frame concatenation with bounded reusable frame buffers.
- Separated live audio capture from video encoding to eliminate blocked/stale video under dual-audio input.
- Close encoder input before waiting for shutdown; preserve healthy long-running encoded-data drains.
- Mix paused PCM segments before one final AAC encode to avoid per-segment priming overlap.
- Pad leading audio silence using wall-clock timestamps and verify an actual audible signal.
- Serialize asynchronous source changes and acknowledge unchanged native states to avoid stuck UI controls.
- Validate update metadata before any download request, verify SHA-256 again before installation, retain files on failed finalization and reject overwriting existing recordings.

## Security and ownership boundaries

The renderer has context isolation, no Node access, sandbox enabled, no external navigation/new windows and denied web permissions. Its preload exposes only the narrow CamCord bridge; IPC accepts the owning main frame only. Capture and FFmpeg run as the user, without shell interpolation. Administrator authentication is used only for the user-requested APT update installation. Startup opens the app but never begins capture.

Capture work uses a unique session directory inside the selected output folder. Failed sessions retain logs/media for recovery. Successful publication is atomic and does not overwrite an existing destination. Update downloads are limited in size and restricted to the exact platform asset in a higher stable release of this repository.

## Remaining validation boundaries

No physical Debian GNOME/KDE Wayland desktop or dedicated GPU was available for this audit. The portal wire protocol is tested, but a simulated service does not prove real compositor permissions, stream rendering, pause behavior or every driver. Wayland window minimization is compositor-dependent; the UI/README explicitly asks users to keep the recorded window restored. Linux hardware support, suspend/logout, removable-drive filesystem variation and 27-minute recordings still require physical-machine testing. Background finalization is asynchronous and responsive by architecture, not a claim that every long-recording/device combination has been exercised.

## Reference contracts

- [ScreenCast portal API](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html): permissions, source types, hidden cursor and PipeWire descriptors/serials.
- [FFmpeg device documentation](https://ffmpeg.org/ffmpeg-devices.html#pulse): PulseAudio inputs and wall-clock timestamps.
- [Linux rename API](https://man7.org/linux/man-pages/man2/rename.2.html): `RENAME_NOREPLACE` atomic publication without overwriting.
- [Electron security checklist](https://www.electronjs.org/docs/latest/tutorial/security): renderer isolation and navigation/permission restrictions.
