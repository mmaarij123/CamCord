# Changelog

## 1.3.0 — 2026-10-08

- Rebuilt the interface in React and Material UI with a responsive dark/coral design.
- Added an in-app folder picker for choosing any writable recording folder or drive.
- Moved recording session data onto the selected output drive, avoiding hidden dependence on the system drive.
- Added asynchronous start, pause, resume and finalization states so long recordings remain responsive while saving.
- Added Unicode-safe, atomic preference storage and retained the last confirmed saved recording only after successful publication.
- Hardened pause/resume audio layout, WASAPI timing, RF64 audio, fragmented capture recovery and final media validation.
- Preserved recovery files on capture or finalization failures instead of presenting damaged output as successfully saved.
- Excluded the CamCord window from capture when Windows supports it and reports the actual status in the UI.
- Added a per-user installer with verified dependency downloads, full frontend notices and automated release builds.
- Added isolated settings/output tests, UI state tests and synthetic recording/finalization regression tests.

### Compatibility

Windows 10 version 2004 (build 19041) or later, or Windows 11, x64. Internet access is required while running Setup so it can fetch and verify the pinned FFmpeg build; recording is local afterward.
