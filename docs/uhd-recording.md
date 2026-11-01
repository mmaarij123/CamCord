# 2K and 4K recording — 1.9.2

Added 2560 × 1440 (2K / QHD) and 3840 × 2160 (4K / UHD) profiles to the shared
React interface, Windows host/settings and Debian recorder/capture helper. Both
profiles support 15, 30 and 60 FPS. Switching resolution or restoring preferences
preserves supported frame rates; 120 FPS falls back to 60 outside 1080p.
Existing 480p/720p and 1080p profiles remain;
1080p retains its supported-hardware 120 FPS option.

Auto video targets at 15/30/60 FPS are 12/20/32 Mbps for 1440p and 24/40/64 Mbps
for 2160p, with the existing 100 Mbps manual ceiling and high-storage warnings.
Width, frame-rate choices and
bitrate estimates are shared between the UI and Debian; native helpers apply
equivalent normalization. Windows encoder probes now use the selected dimensions
before choosing a hardware or software encoder.

The five resolution buttons use a three-column grid, including at narrow window
sizes. The high-resolution frame-rate row shows all three options and explains
the GPU/storage cost.

## Windows capture changes

An owned 640 × 360 moving-window test revealed that enlarging each capture frame
to 4K before readback/pipe transfer could severely reduce source throughput.
Capture now uses a fixed, aspect-preserving canvas sufficient for the selected
source's initial dimensions, capped at the requested output resolution. Smaller
sources are enlarged once at encoding, preserving the requested file dimensions
without inflating every capture transfer. Window resizing keeps this internal
canvas fixed; begin a new recording to select a larger initial source canvas.

The bridge and encoder agree on an NV12 4:2:0 layout, converted from the source's
BGRA frames using the existing software conversion path.
NVIDIA/AMD/Intel hardware encoders receive their supported NV12 input, while the
existing software encoder output remains standard 8-bit H.264 4:2:0.

## Checks

- Shared UI tests cover both resolutions, dimensions, 15/30/60 FPS preservation,
  restored state, Auto bitrates, storage and manual-bitrate preservation.
- Native settings tests cover host messages and isolated INI save/load for both
  new profiles, including inputs that request unsupported frame rates.
  Rapid-save tests exposed transient Windows replacement locks; settings now
  use exclusive, unique temporary files and a bounded retry before preserving
  the old preferences on failure. A controlled short-lived reader exercises
  this path, including Windows returning access denied for a rename lock.
- Debian core tests encode/decode generated 1440p and 2160p H.264 and inspect
  dimensions and average frame rate with FFprobe at all three frame rates.
  The isolated X11 integration suite also records all six combinations through
  the actual capture helper.
  Debian recording explicitly uses constant frame-rate output; an uneven-input
  regression test verifies that the final MP4 retains its 60 FPS timeline.
- The optional native `capture-performance-test.ps1 -Hardware -Uhd -LiveWindow`
  checks actual selected-window capture, H.264 file dimensions/FPS, decode and
  cadence. On the local RTX 2060 SUPER / i5-10400 system, the owned moving-window
  test measured 0 / 222 repeated frames at both 1440p/60 and 2160p/60 after the
  smaller-source transfer fix. A full-size generated-source bridge test also
  encoded both profiles successfully.

These are short controlled tests, including an upscaled owned window, not a
physical 4K monitor/gameplay benchmark or a guarantee under arbitrary GPU load.
Sources smaller than the output are upscaled; a 4K file alone does not add native
4K detail. Long recordings and other vendor/driver combinations need their own
performance checks. Recording begins only through the normal explicit controls;
Instant Replay remains removed.
