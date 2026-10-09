# CamCord

A lightweight screen recorder for every capture you imagine. Available on Windows, with Linux and macOS coming soon.

## Features

- **Capture source:** record a chosen display, an individual window, or a mouse-selected area within a display. Window capture works behind other windows.
- **Recording quality:** choose 480p, 720p or 1080p.
- **Frame rates:** record at 15, 30 or 60 FPS, with 120 FPS at 1080p on supported hardware.
- **Video bitrate:** Auto, or choose 2, 4, 6, 8, 10, 12, 16, 20, 24, 32, 40, 50, 64, 75 or 100 Mbps. See estimated storage use; 32 Mbps and higher show a storage warning. Your choice is remembered.
- **Audio options:** capture system audio, your microphone, both, or video only.
- **Pause and resume:** take breaks and save the recording as one video.
- **Custom save location:** choose a folder or drive inside the app; CamCord remembers your preferences.
- **Hardware acceleration:** automatically uses supported NVIDIA, AMD or Intel encoders, with a software fallback.
- **Background saving:** shows a saving status while longer recordings are being finalized.
- **MP4 videos:** recordings are saved locally without watermarks and can be made offline after installation.
- **Simple dark interface:** easy controls for recording, settings and opening saved files.
- **Automatic updates:** checks GitHub releases and downloads verified updates; install and restart when you're ready.
- **Windows startup:** enable or disable launching CamCord at sign-in from App settings.

## Platforms

Currently available for **Windows only**: Windows 10 version 2004 or later, or Windows 11 (64-bit).

**Linux and macOS — coming soon.**

## How to install

1. Open the [latest CamCord release](https://github.com/mmaarij123/CamCord/releases/latest).
2. Under **Assets**, download **CamCord-Setup.exe**, or use the [direct setup download](https://github.com/mmaarij123/CamCord/releases/latest/download/CamCord-Setup.exe).
3. Run the downloaded setup and follow the installation steps.
4. Open **CamCord** from the Start menu and start recording.

**Keep your internet connection active during installation.** Setup automatically downloads the required components. After installation, you can record offline.

In **App settings**, switch **Launch at Windows startup** on or off. Startup opens CamCord minimized without starting a recording. **Automatic updates** checks on launch and every six hours; you can also use **Check now**. Updates install through **Install & restart** after you've stopped and saved your recording.

Version 1.3.1 and earlier need this setup installed once to receive the in-app updater. Future updates require publishing a new stable GitHub release with a higher version and its setup file; pushing source changes alone does not update installed apps.

For window recording, keep the selected window restored: closing, hiding or minimizing it stops and saves the completed portion. System audio still records all desktop playback, not only the selected window. Source selections reset to the primary display when CamCord restarts; your quality, bitrate and other settings are preserved. Protected apps may refuse capture.

## License

CamCord is licensed under the [MIT License](LICENSE). Third-party components retain their [own licenses](THIRD_PARTY_NOTICES.md).
