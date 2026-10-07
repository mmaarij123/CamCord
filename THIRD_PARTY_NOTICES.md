# Third-party software in CamCord

CamCord uses separately licensed components. The installed `licenses` directory contains their original license and attribution files. These notices do not replace those licenses.

## FFmpeg

The public CamCord installer does **not** embed or redistribute an FFmpeg executable. During installation it downloads the unmodified **FFmpeg 9.0.2 essentials** Windows x64 static build directly from [Gyan Doshi](https://www.gyan.dev/ffmpeg/builds/), licensed under **GNU GPL version 3**, and verifies its archive and executable checksums. CamCord invokes the downloaded `ffmpeg.exe` as a separate command-line program. FFmpeg is a trademark of Fabrice Bellard, originator of the FFmpeg project.

- Binary package: https://github.com/GyanD/codexffmpeg/releases/download/9.0.2/ffmpeg-9.0.2-essentials_build.zip
- Binary package SHA-256: `60f467265b1e312373dbcd92200c2618a74850f98d3d078e94296bb3fa2047ba`
- Matching FFmpeg source commit: https://github.com/FFmpeg/FFmpeg/commit/946fcce07b
- Matching FFmpeg source archive: https://github.com/FFmpeg/FFmpeg/archive/946fcce07b.tar.gz
- FFmpeg source archive SHA-256: `0aa2b1de2a5698b20a23e93d539a9a8e82ca0117496c5bdf05d198805f42bb3b`
- Full upstream build configuration, enabled libraries and library versions: `licenses/FFmpeg-build.txt`.
- Original GPL text: `licenses/FFmpeg-GPLv3.txt`.

The FFmpeg source link above covers FFmpeg itself. External libraries statically linked by the upstream distributor have their own source and licensing requirements. Refer to the upstream build documentation and source distribution before redistributing downloaded or repackaged builds. CamCord's development scripts download FFmpeg locally for development and tests; those local binaries are excluded from the public source repository and public installer.

## React, Material UI, Emotion and dependencies

The React user interface uses React and React DOM (Meta Platforms, Inc. and affiliates), Material UI (Material UI SAS), Emotion, and the production packages recorded in `ui/package-lock.json`. Their original license files are copied from the locked packages into `licenses/frontend/`; `licenses/FRONTEND-NOTICES.txt` lists versions and license identifiers. Most JavaScript dependencies use MIT or similarly permissive licenses; each original file is authoritative.

## Fonts

Bundled Fontsource packages, when present in the UI, carry the fonts' own license terms. Their original SIL Open Font License or other font license text is included alongside the corresponding package in `licenses/frontend/`. Fonts are served from the installed app, not a remote font service.

## Microsoft Edge WebView2

CamCord statically links the WebView2 SDK loader from **Microsoft.Web.WebView2 1.0.3485.44**. Microsoft copyright, SDK license and notices are included in `licenses/`. The Evergreen Runtime is separately licensed Microsoft software. The setup bundles Microsoft's signed bootstrapper to install the runtime if missing. Microsoft services the Evergreen Runtime independently.

- SDK: https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.3485.44
- Runtime and terms: https://developer.microsoft.com/microsoft-edge/webview2/
