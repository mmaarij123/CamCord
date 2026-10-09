# Bitrate feature audit — 1.5.0

## Behavior

- Auto preserves the previous resolution/FPS targets and remains the default for existing installations.
- Manual presets: 2, 4, 6, 8, 10, 12, 16, 20, 24, 32, 40, 50, 64, 75 and 100 Mbps.
- Manual choices survive settings reload and resolution/FPS changes. Unsupported or corrupt saved values become Auto.
- Every encoder uses the same target/peak/buffer calculation. Peak is target × 1.3, capped at 100 Mbps, including the 100 Mbps preset.
- Storage estimates use decimal MB/GB and include one final 192 kbps AAC track when either audio source is enabled. Actual VBR size differs; temporary recording/finalization files need extra space.
- A warning icon and text appear on menu options and selected presets at 32 Mbps or higher. Auto and a nearby manual preset are recommended according to resolution/FPS.

## Verification

- UI unit tests cover every preset, invalid input, Auto mappings, peak cap, manual persistence and storage arithmetic.
- Native tests encode and decode synthetic videos at all 16 choices with software H.264. Local NVIDIA NVENC testing also passed all choices at 1080p/60 FPS on an RTX 2060 SUPER. AMD/Intel flag generation is tested, but their hardware was not available locally.
- Isolated settings tests cover every preset, missing legacy keys, corrupted values, Unicode paths, failed saves and preservation of existing settings.
- Browser interaction checks cover every menu option and its native message, high-storage warning, keyboard selection, Auto adaptation, manual preservation and disabled controls during preparation, recording, pause, saving and update installation.
- Visual checks at 1040×740, 760×560, 480×640 and 360×640 found no horizontal overflow. Dropdowns use MUI's portal rather than being clipped by the scrolling settings panel; labels, warning text and focus indicators remain visible.
- The bitrate control is code-split so the recorder's initial JS stays about 445 kB instead of exceeding 500 kB; packaged offline loading is verified. This follows React best-practices guidance while preserving the existing dark/coral design and form-control conventions.
- Recording finalization/recovery and updater/startup regression suites passed locally. Tests use synthetic media and isolated preferences, not the user's screen, microphone or Windows startup entry.
- Isolated setup installation/uninstallation passed. Every installed UI asset matches the staged build, including the separately loaded bitrate component. The user's installed CamCord and settings were not modified.

No blocking issue remained in the changed feature after fixes. Malformed fractional JSON settings are no longer silently truncated to a valid integer. High bitrates are target values, not a guarantee of constant output size or real-time performance on every PC.
