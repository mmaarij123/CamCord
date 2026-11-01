# CamCord 1.9.2 audit — 2026-11-02

Reviewed the maintained Windows C++ host/capture/audio/process/settings/updater
code, shared React UI/state/bridge/quality controls, Debian Electron/Python
capture/recording/process/settings/updater code, build/installer scripts,
dependency locks, tests, manifests, release workflows and supporting docs.
Generated dependencies/build outputs are checked through builds, dependency
audits and package tests; they are not manually rewritten. The unrelated,
untracked `instagram-cleanup` folder is outside this project audit.

## Corrections

- Removed the 2K/4K 60-only restriction from the UI, native normalizer and Python
  helper. All resolutions support 15/30/60; 120 remains restricted to 1080p.
  Native host messages, INI reload, Debian JSON preferences and UI state retain
  each supported choice. Unsupported rates still normalize to 60.
- Auto video targets now follow FPS. At 15/30/60, 2K uses 12/20/32 Mbps and 4K
  uses 24/40/64 Mbps. Manual bitrates and the 100 Mbps ceiling are preserved.
- Windows periodic update checks no longer discard a downloaded, verified setup
  while it awaits the user's install action. An isolated lifecycle regression
  sets an expired check deadline and verifies no network task replaces it.
- A fresh Debian update check clears old release/file metadata, preventing a
  failed request from starting a download using a previous check's result.
- Failed Debian encoder writes release their pinned frame in `finally`.
  A failing-writer regression verifies no retained buffer is left behind.
- Removed unused Python/C++ imports, unused CSS variables, the obsolete
  high-resolution-only FPS array, redundant FPS validation, duplicate Windows
  source enumeration and a redundant exception type. Existing capture recovery,
  queue limits and removed-feature rejection tests remain useful and retained.
- Setup removes only its generated `ui` and `licenses` folders before replacing
  them, preventing old hashed bundles/notices from accumulating. The isolated
  install/upgrade/uninstall test verifies exact shipped UI hashes and preserves
  preference/recording markers outside these generated folders. This test also
  runs in release CI. The deprecated installer file-copy alias was updated.
- Debian's build-only ASAR dependencies used vulnerable glob/minimatch versions.
  Scoped overrides pin glob 14.0.3 and minimatch 11.0.3. Real archive
  creation/extraction checks nested, hidden, Unicode and unpacked files.
  Upstream advisories: [glob](https://github.com/advisories/GHSA-3g3m-2fjr-6pw4)
  and [minimatch](https://github.com/advisories/GHSA-g3hv-75wg-m249).

## Local verification

- Native Release and Windows setup builds; source/version/manifest validation.
- 17 shared UI tests; 12 Debian core tests (desktop test skipped on Windows).
- Real H.264 encode, FFprobe dimensions/FPS and decode for all six UHD profiles.
- Windows owned moving-window capture at both resolutions and 15/30/60 FPS:
  no repeated measured frames at 15/30, and 1/222 at 60 for each resolution.
- Source validation, occlusion, resizing/black bars, static/closed sources and
  bounded stalled-pipe shutdown; all 16 hardware bitrate encode/decode presets.
- RF64/audio timing, pause/resume, long encoder drain, corrupt/missing media,
  publication collisions and recovery preservation.
- Unicode settings/folders, transient reader locks, denied writes, updater,
  startup, native modal/close lifecycle, icons and isolated setup upgrades.
- Browser checks at 1040×740, 760×600 and 360×640: all six choices, selected
  state, quality readout, automatic bitrate, no horizontal overflow/page errors.
- Full npm audits of UI and Debian dependency trees: zero known advisories.

The release workflow separately builds Debian 12, exercises its isolated X11,
audio, portal protocol and packaged UI tests, and checks installation/dependencies
and uninstall on clean Debian 12 and 13 before publishing both installers.

## Practical limits

Tests use generated media and owned test windows; they do not record the user's
desktop/audio or modify their installed preferences. Short controlled captures
do not establish performance for long gameplay, a physical native-4K source,
every GPU/driver or physical GNOME/KDE Wayland session. Existing recordings are
untouched. This review does not claim that all possible bugs have been eliminated.
