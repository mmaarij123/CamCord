# CamCord code audit — 2026-10-08

Reviewed the Win32/WebView2 host, React interface and bridge, WASAPI audio writer, capture process, encoder selection, pause/resume and finalization, output folders, settings persistence, dependency staging, installer and GitHub release workflow.

## Findings fixed in 1.3.1

| Finding | Effect | Correction |
| --- | --- | --- |
| Encoder shutdown used a 15-second limit | A slow but valid encoder drain could be terminated and the save reported failed | Capture shutdown waits on the existing background worker; explicit limits still apply to capability probes |
| The pipeline test changed directories before resolving its FFmpeg argument | The documented relative-path command and GitHub release tests failed to start FFmpeg | Resolve the engine path before switching to the isolated test build folder |
| Engine discovery used MAX_PATH-sized buffers | Long application or PATH directories could be truncated or miss the engine | Size the buffers correctly, check lengths and regular files, and enable Windows long-path support |
| Installer version could differ from the executable | An old executable could be distributed under a new setup version | Verify the source, requested installer version and built executable version agree before compilation |
| Source validation hardcoded one version and stripped trailing version digits | Later patch releases could fail validation or hide a version mismatch | Compare exact package, resource, installer and manifest versions |

## Verification

- Native Release and React production builds succeed.
- The settings/output integration suite passes with isolated Urdu, CJK and emoji folders, write-denied destinations, atomic-save failures and session data on the chosen output drive.
- The recording suite covers RF64 decode/timing, interrupted fragmented MP4, silent/audible pause segments, video-only output, corrupt or missing video/audio, stop failure, output collision and recovery preservation.
- New regressions exercise an encoder drain longer than 15 seconds and engine discovery through a path longer than MAX_PATH.
- UI state tests pass; npm reports no known production dependency vulnerabilities at audit time.
- Packaging rejects a deliberately mismatched version.

## Limits

Generated media tests do not record the desktop or microphone. A real 27-minute recording, physical device disconnect, each vendor's hardware encoder and a full audio file larger than 4 GiB were not tested. The asynchronous save path is verified by code review and the long-drain regression; these checks do not establish that every hardware or driver combination is defect-free.

The installer downloads the pinned FFmpeg build from upstream during installation and requires internet access. It retains the original per-user installation identity. The application and setup are unsigned.
