# CamCord

A lightweight screen recorder for every capture you imagine. Available on Windows and Debian Linux. macOS is coming soon.

## Features

- **Capture source:** record a chosen display or an individual window. Window capture works behind other windows.
- **Recording quality:** choose 480p, 720p or 1080p.
- **Frame rates:** record at 15, 30 or 60 FPS, with 120 FPS at 1080p on supported hardware.
- **Video bitrate:** Auto, or choose 2, 4, 6, 8, 10, 12, 16, 20, 24, 32, 40, 50, 64, 75 or 100 Mbps. See estimated storage use; 32 Mbps and higher show a storage warning. Your choice is remembered.
- **Audio options:** capture system audio, your microphone, both, or video only.
- **Pause and resume:** take breaks and save the recording as one video.
- **Instant Replay:** enable background capture and save the previous 30 seconds to 5 minutes with a global shortcut, like an instant-replay clip. Configurable duration and shortcut presets, buffer progress and a Save clip button are inside settings.
- **Custom save location:** choose a folder or drive inside the app; CamCord remembers your preferences.
- **Hardware acceleration:** automatically selects a supported hardware encoder, with a software fallback. Availability depends on your operating system, GPU and drivers.
- **Background saving:** shows a saving status while longer recordings are being finalized.
- **MP4 videos:** recordings are saved locally without watermarks and can be made offline after installation.
- **Simple dark interface:** easy controls for recording, settings and opening saved files.
- **Automatic updates:** checks GitHub releases and downloads verified updates; install and restart when you're ready.
- **Startup:** enable or disable launching CamCord at sign-in from App settings; it never starts recording automatically.

## Platforms

**Windows:** Windows 10 version 2004 or later, or Windows 11 (64-bit).

**Debian Linux:** Debian 12 or 13, **amd64 / x86-64**, with a graphical desktop. X11 capture is integration-tested; Wayland uses your desktop's ScreenCast permission picker. Physical GNOME/KDE Wayland and individual GPU/driver combinations still need broader testing. ARM packages and other Linux distributions are not provided yet.

**macOS — coming soon.**

## How to install

### Windows

1. Open the [latest CamCord release](https://github.com/mmaarij123/CamCord/releases/latest).
2. Under **Assets**, download **CamCord-Setup.exe**, or use the [direct setup download](https://github.com/mmaarij123/CamCord/releases/latest/download/CamCord-Setup.exe).
3. Run the downloaded setup and follow the installation steps.
4. Open **CamCord** from the Start menu and start recording.

**Keep your internet connection active during installation.** Setup automatically downloads the required components. After installation, you can record offline.

In **App settings**, switch **Launch at Windows startup** on or off. Startup opens CamCord minimized without starting a recording. **Automatic updates** checks on launch and every six hours; you can also use **Check now**. Updates install through **Install & restart** after you've stopped and saved your recording.

Version 1.3.1 and earlier need this setup installed once to receive the in-app updater.

### Debian Linux

1. Download [CamCord-Debian-amd64.deb](https://github.com/mmaarij123/CamCord/releases/latest/download/CamCord-Debian-amd64.deb) from the [latest release](https://github.com/mmaarij123/CamCord/releases/latest).
2. Open a terminal in the download folder and install the package and its dependencies:

   ```sh
   sudo apt install ./CamCord-Debian-amd64.deb
   ```

3. Open **CamCord** from your applications menu. You can also run `/opt/camcord/camcord`. Run the app as your normal user, **not with sudo**.

An internet connection is needed to install missing dependencies. Recording works offline afterward. System audio needs your desktop's PulseAudio-compatible sound server (PulseAudio or PipeWire-Pulse).

On **Wayland**, install your desktop's matching portal backend if it is missing (`xdg-desktop-portal-gnome`, `xdg-desktop-portal-kde`, or the backend recommended by your desktop). Choose **Screen** or **Window** in CamCord, then approve the actual source in your desktop's picker when recording starts. Keep the recorded window restored; minimized-window behavior depends on the compositor. On X11, a closed, hidden or minimized source stops capture and saves the completed portion.

**Launch at login** is optional in App settings. **Automatic updates** checks for new Debian releases; **Install & restart** asks for your system's administrator authentication and is available only when recording has stopped.

### Instant Replay / clipping

In **Instant Replay** settings, select **30 seconds, 1, 1.5, 2, 3, 4 or 5 minutes**, choose a shortcut, and turn **Rolling capture** on. The default shortcut is **Ctrl+Shift+F8**. Press it while CamCord is open (including minimized) to save the preceding clip as an MP4 in your selected recording folder. **Save clip** does the same from the app. If the buffer is not full yet, only the available time is saved.

Replay uses the selected Screen/Window, resolution, FPS, bitrate and audio toggles. It can continue alongside manual recording, and manual pause does not pause replay. It retains short video/audio chunks on the selected drive, not several minutes of raw frames in RAM. Higher bitrate/duration consumes more storage; saving requires additional free space and software clip encoding can take time. The buffer continues while one clip saves; further save requests wait until that save finishes. There is no queued backlog.

Turn replay off before changing capture settings, duration, shortcut or folder, or installing an update. Turning it off/closing CamCord clears unsaved chunks; completed clips remain. Duration and shortcut are remembered, but **Replay starts off on every launch**, including startup/login, to avoid silently capturing your desktop or microphone.

On Wayland, replay requires ScreenCast permission and a desktop supporting the GlobalShortcuts portal. Your desktop may ask you to approve the shortcut. If a shortcut is already reserved, choose another preset. A second permission picker may appear when you also start manual recording. Protected apps, exclusive fullscreen games and compositor restrictions can prevent capture/shortcuts; this is not an NVIDIA driver-level game-capture integration.

### General notes

System audio records all desktop playback, not only the selected window. Capture-source permissions/selections are not remembered across restarts; quality, bitrate, save location and other settings are preserved. Protected apps may refuse capture.

In-app updates require a new stable GitHub release with a higher version and the matching platform installer. Pushing source changes alone does not update installed apps.

## License

CamCord is licensed under the [MIT License](LICENSE). Third-party components retain their [own licenses](THIRD_PARTY_NOTICES.md).
