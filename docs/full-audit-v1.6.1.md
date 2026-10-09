# CamCord 1.6.1 audit

Reviewed native application/window lifecycle, WebView bridge, source enumeration,
area selector, capture producer/encoder bridge, child-process lifetime, WASAPI/RF64,
pause/resume and MP4 finalization, output/settings persistence, hardware probing,
updater/startup, React controls, locked dependencies, packaging and release CI.

## Fixed findings

1. **Blocked raw-video writer could hang Stop before its timeout took effect.**
   The writer previously joined before encoder EOF/wait, so a non-reading encoder
   prevented shutdown indefinitely. A stuck individual frame now triggers the
   recording health check after 30 seconds. Shutdown gives an in-flight frame
   time to finish, then cooperatively cancels the dedicated synchronous pipe
   writer and waits for that thread before closing its handle. Failed encoders
   get bounded cleanup, return failure and retain recovery files. Healthy encoded
   output still drains without a fixed finalization time limit.
   Tests exercise both the child-process helper and the complete synthetic
   producer/bridge/non-reading-encoder lifecycle; normal writer and >15-second
   healthy drain tests remain green.

2. **Native messages were parsed by substring searches.** Malformed JSON,
   nested commands/keys and escaped strings could be misread; numeric overflow
   must not narrow into a valid preset. Native commands now use the existing
   nlohmann JSON dependency, root command fields and an explicit settings object,
   UTF-8/UTF-16 conversion, strict field types and integer bounds. Tests cover
   malformed/trailing JSON, nested commands, Unicode escapes, huge integers,
   fractional/string values and preserving valid settings.

3. **Close during a folder/area modal loop could destroy its owner while the
   selection callback was still using it.** Close now waits for selection to end;
   shutdown is declined while a selector is open. Queued folder requests cannot
   open after a close request or during update installation. Native tests send
   actual window messages to hidden, isolated fixtures, without starting WebView,
   touching preferences, requesting updates or recording user content.

4. **Install & restart remained enabled during area selection.** The frontend
   now mirrors the native installation guard and accounts for pending selections.
   Its tooltip explains why installation is unavailable.

5. **Malformed nested host state could crash React source controls/alerts.**
   The bridge now rejects bad catalog entries, field shapes, display strings,
   boolean preferences and notice severity before merging them. Regression tests
   and production-browser checks verify that the UI stays on its last valid state.

Pipe cancellation follows [Microsoft's synchronization guidance](https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations):
request cancellation, prevent subsequent writes, retry the race between checks
and calls, and join before closing/reusing handles. Only the dedicated raw-input
writer is cancelled; valid encoder finalization is not cut short.

## Verification on the development PC

- Release build: no compiler warnings/errors; all version manifests agree.
- React unit suite: 12 passing tests.
- Settings/output: isolated Unicode, ACL, persistence and strict host-message tests.
- Pipeline: RF64/silence, fragmented MP4, paused segment/audio concat, video-only,
  corrupt/missing media, failed stop and output collision/recovery cases pass.
- Source suite: bounds/identity/geometry, complete stalled bridge, real covered
  test-window updates, static-window stop and closed-window stop pass.
- All 16 bitrate choices encode/decode with software libx264 and NVIDIA NVENC.
- Real synthetic-window NVIDIA test: 1080p, 120 FPS, 100 Mbps.
- Updater/startup: version/hash/tamper/cache/cancellation/install guards and isolated
  registry opt-in/out pass. No actual installer was launched by updater tests.
- Production UI: 1040×740, 760×600, 480×640, 360×640; no horizontal overflow or
  browser errors. Missing-source, saving controls, 100 Mbps warning, selection
  installation lock and malformed nested state verified. The 27-minute UI test
  uses a mocked elapsed state, not a 27-minute physical recording.
- Taskbar/setup icon coverage passes. Isolated setup installs all matching UI
  assets and uninstalls successfully; the existing installation is unchanged.
- Full and production-only `npm audit`: zero reported advisories at audit time.

## Boundaries

This is a code audit plus targeted regression testing, not a guarantee that every
possible bug is eliminated. AMD/Intel hardware, multiple physical monitors,
other audio devices, slow/network/removable drives, Windows shutdown under actual
recording, protected/DRM windows and extended soak recordings need additional
device coverage. High FPS/bitrate performance depends on CPU/GPU/drive capacity.
Native pipe cancellation cannot guarantee recovery from arbitrary faulty OS or
storage drivers; recovery files are preserved on failures. Linux/macOS remain
unsupported. The UI changes preserve the existing design and lazy component
split using frontend-design and vercel-react-best-practices guidance.
