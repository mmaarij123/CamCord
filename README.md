# CamCord

A Windows screen recorder with a React + Material UI interface and native C++ capture engine. CamCord records the primary display to H.264/AAC MP4, supports system audio and microphone capture, and lets you choose the recording folder or drive from inside the app.

## Install

Download **CamCord-Setup.exe** from this repository's Releases page and run it. Installation is per-user and normally does not require administrator access. **Internet is required during installation:** Setup automatically downloads the FFmpeg recording engine (about 110 MB) directly from its upstream distributor, verifies both the archive and executable SHA-256 checksums, and installs it beside CamCord. No manual dependency steps are needed. Recordings work locally after setup.

**Windows 10 version 2004 (build 19041) or later, or Windows 11, x64 is required.** Microsoft Edge WebView2 is also required. If it is missing, Setup installs it through Microsoft's signed bootstrapper. Setup checks that dependency installation succeeded before continuing. No Node.js, Python, Visual Studio or separate C++ redistributable is needed to run the installed app.

The application and installer are currently unsigned. Windows may display its normal publisher or SmartScreen prompts; obtain the setup from this repository's release and compare its SHA-256 checksum when provided.

## Record

1. Select resolution, frame rate, system audio and microphone settings.
2. Under **Save location**, use **Change** to choose a folder or drive. CamCord remembers your choice; the default is `Videos\CamCord Captures`.
3. Start recording. Pause and resume when needed, then stop.
4. Wait for **Saving capture** to finish. Longer recordings can take a few minutes to finalize. Keep the app open until the saved confirmation appears.

Recordings stay on your computer. There are no watermarks or in-video overlays. The mouse pointer is not included. System audio captures the default Windows playback device; microphone capture uses the default recording device.

## Features

- 480p, 720p and 1080p, with aspect-ratio-preserving scaling and letterboxing.
- 15, 30 and 60 FPS; 120 FPS at 1080p when supported.
- NVIDIA NVENC, AMD AMF and Intel Quick Sync probing, then software H.264 fallback.
- Native WASAPI system audio and optional microphone audio.
- Pause/resume segments, background finalization and visible saving status.
- Folder and quality settings saved per-user under `%LOCALAPPDATA%\CamCord`.
- Failed finalization retains session material for recovery instead of silently discarding it.

## Build from source

Build requirements:

- Visual Studio 2022 or later, or Build Tools, with **Desktop development with C++** and a Windows 10/11 SDK.
- Node.js 22.12 or later with npm.
- Windows PowerShell 5.1 or PowerShell 7.
- [Inno Setup 6.5 or later](https://jrsoftware.org/isdl.php) to build the installer, including its ZIP extraction support.
- Internet access for the initial dependency downloads.

From the project directory:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-release.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1 -SkipBuild
```

The scripts install the exact frontend lockfile, download checksum-pinned FFmpeg and WebView2 SDK packages, build the React assets and native x64 app, and stage their licenses. The project uses the installed Visual Studio default toolset and links the C++ runtime and WebView2 loader statically.

Outputs:

```text
bin\Release\CamCord.exe
bin\Release\ffmpeg.exe
bin\Release\ui\
bin\Release\licenses\
dist\CamCord-Setup.exe
```

Keep the entire runtime folder together when running without the installer. To build a usable Debug folder, run `scripts\build-release.ps1 -Configuration Debug`. After dependencies and UI have been prepared, either configuration can also be built in Visual Studio; its post-build step stages the same runtime assets.

`scripts\package-release.ps1` builds the public setup. `-SkipBuild` reuses an already verified Release build. The public installer does not embed FFmpeg; each recipient downloads the pinned package from upstream during installation. Local developer builds contain FFmpeg for running and testing the app. Do not upload the development runtime folder as a release package without separately handling all dependency redistribution requirements. Read [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for upstream licenses and source links.

## Development checks

```powershell
python tests\validate_project.py
tests\settings-test.ps1
tests\pipeline-test.ps1 -Ffmpeg .\bin\Release\ffmpeg.exe
cd ui
npm test
npm run build
```

The native integration tests use generated media and isolated temporary folders. They do not record the desktop or microphone and do not read or change the user's CamCord preferences.

The GitHub Actions workflow builds the installer and checksum on manual dispatch and version tags. A tag matching the source version publishes a GitHub release; manually dispatched builds only upload workflow artifacts. Build jobs have read-only repository permissions; only the tag-release job can write releases.

## Troubleshooting

- **WebView2 is unavailable:** install the [Evergreen Runtime from Microsoft](https://developer.microsoft.com/microsoft-edge/webview2/) and rerun Setup.
- **ffmpeg.exe is missing:** keep it beside CamCord.exe, or rerun Setup. Developers can run `scripts\setup-ffmpeg.ps1`.
- **Desktop capture fails:** check your display driver and local Windows desktop session. Screen capture can be unavailable on locked, disconnected or protected desktops.
- **120 FPS falls back to 60 FPS:** the encoder or display could not pass the capability check.
- **Audio is missing:** ensure the chosen default playback or recording endpoint is available and that Windows microphone permission is enabled.
- **Saving fails:** follow the error shown in the app and keep the session folder. Temporary video/audio/log files may allow recovery; do not clear them before recovery is complete.
- **A chosen drive is unavailable:** reconnect it or choose another writable save folder before starting.

## Source layout

| Area | Responsibility |
| --- | --- |
| `ui/` | React and Material UI interface, native message bridge |
| `src/MainWindow.*` | Win32 host, WebView2, folder picker, background operations |
| `src/RecordingManager.*` | Capture sessions, pause/resume, muxing and recovery |
| `src/AudioCaptureEngine.*` | Native WASAPI audio capture |
| `src/CaptureEngine.*` | FFmpeg Desktop Duplication capture |
| `src/OutputManager.*` | Output folders, space checks and filenames |
| `scripts/` | Dependency verification, builds, notices and packaging |
| `installer/` | Per-user Inno Setup package |

Third-party components retain their original licenses. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the notices shipped with the app.

The latest code review, corrected bugs and verification limits are documented in [AUDIT.md](AUDIT.md).
