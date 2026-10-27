# Windows recording audit — 2026-10-27

User report: saved Windows recordings stutter. Reviewed capture/source timing,
raw-video transfer, encoder configuration, audio alignment, pause/resume,
finalization/recovery, UI state, packaging and dependency audits. Debian's
capture implementation is unchanged; its unused import and build dependency
were cleaned up alongside shared UI styling.

## Findings and changes

- **Latest-frame sampling discarded usable motion frames.** Independent producer
  and writer clocks occasionally sampled the same image twice and skipped the
  next image. A maximum of three pending frames now absorbs short scheduling
  jitter. Overflow discards the oldest pending frame to keep latency and memory
  bounded. Five reusable buffers cover the queue, reader and active writer;
  referenced buffers are never overwritten. Static sources still repeat their
  last image to preserve recording duration.
- **The writer used an ordinary condition-variable timeout for cadence.** Windows
  scheduling could wake it late, followed by a burst of repeated frames. A high
  resolution waitable timer now paces the writer against steady-clock deadlines.
  A separate event wakes shutdown immediately; no busy waiting or system-wide
  timer-resolution change is used. Existing stalled-writer cancellation remains.
- **Frames repeatedly allocated and zeroed large BGRA vectors.** Buffers are
  allocated once per segment. H.264's existing YUV420 layout is used across both
  pipes: 3,110,400 bytes per 1080p frame instead of 8,294,400, a 62.5% reduction
  per transfer. Both raw pipes request 1 MiB buffering instead of the system
  default. Odd output dimensions are rejected before starting capture.
- **Scaling occurred on the CPU after GPU download.** The Windows capture source
  now sizes an aspect-preserving, black-padded canvas on the GPU, including window
  resizes. Capture-rate headroom avoids unnecessary undersampling when monitor
  refresh is not a multiple of recording FPS. Output remains at the selected FPS.
  FFmpeg describes `max_framerate` as a cap on compositor frames, not a stable
  output clock: [official filter reference](https://ffmpeg.org/ffmpeg-filters.html#gfxcapture).
  The bundled 9.0.2 filter options were checked directly; newer-only `dup_frames`
  support is not assumed.
- **Unused code:** removed retired capture-source card/dot CSS and its color
  variable, unused custom COM helpers, a Node event import and an unused C++
  include. Removed provisional audio-trim calculations that were overwritten
  after video startup; the actual capture-start alignment remains.
- **Build-tool advisories:** updated Vite 8.3.2 to 8.3.5 and the Debian packaging
  dependency's locked Undici 7.30.0 to 7.30.1. These are build tools, not a newly
  introduced recording dependency. No application dependency major version changed.

## Measurements and regression coverage

Moving-frame tests use generated changing luminance, the production native
bridge, real H.264 encoding and decoded frame analysis. They never capture a
desktop or audio device. A half-second at each end is excluded from the repeated
frame measurement. Local hardware: RTX 2060 SUPER, i5-10400, Windows.

| Same generated-source hardware test | Before | After |
| --- | ---: | ---: |
| 1080p, 60 FPS: repeated decoded frames | 31 / 221 (14.0%) | 11 / 222 (5.0%) |
| 1080p, 120 FPS: repeated decoded frames | 191 / 444 (43.0%) | 23 / 443 (5.2%) |

The software 720p/60 run measured 0 / 222 repeated frames. Runs last about 4.7
seconds including startup and are scheduling-sensitive, not universal performance
guarantees. The synthetic producer can itself deliver frames unevenly. CI uses
a tolerant software threshold; an optional stricter hardware run checks both
60 and 120 FPS on a real GPU.

An additional owned Win32 moving-window test exercised the actual Windows
Graphics Capture path on the local 200 Hz display: 0 / 222 repeated decoded
frames at 1080p/60 and 43 / 444 (9.7%) at 1080p/120. This captures only the
generated test window. It includes compositor and window-paint timing, so its
figures are distinct from the same-source before/after comparison above.

Validation completed locally: native Release/installer build, hardware capture
and all 16 bitrate encode/decode presets, software/hardware cadence, native
lifecycle, settings/output, pipeline/recovery, updater/startup, branding and the
15 shared UI tests. Debian core tests pass (8 tests; the physical desktop suite
is skipped on Windows). Both full npm dependency audits report zero advisories
at audit time. The release workflow also runs the isolated Debian desktop and
package-install tests before publication.

Additional coverage includes selected-window capture behind another window,
resize/black-bar decode checks, static-source/closed-source shutdown, invalid
dimensions, a producer with a non-reading encoder, RF64/audio timing, pause/resume
publication, long encoder drain, corrupt/missing media recovery, output collisions,
settings, native lifecycle, updater/startup, shared React state and release builds.
The blocked-writer fixture now exceeds the enlarged pipe capacity so it continues
to exercise cancellation rather than a write that fits in the buffer.

## Limits

These checks cannot remove stutter already present in the selected application,
create motion above the source refresh rate, or guarantee a GPU encoder can keep
up while a game saturates the GPU. A long physical gameplay recording and AMD/Intel
hardware still need their own checks. The user's existing recording files and
preferences were not altered. Instant Replay remains removed.
