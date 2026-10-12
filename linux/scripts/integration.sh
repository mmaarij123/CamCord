#!/bin/bash
set -euo pipefail
cd /workspace
dpkg -i linux/out/CamCord-Debian-amd64.deb
test "$(stat -c %a /opt/camcord/chrome-sandbox)" = 4755
test "$(stat -c %U /opt/camcord/chrome-sandbox)" = root
dbus-run-session -- bash -euo pipefail -c '
  export DISPLAY=:99
  Xvfb :99 -screen 0 1280x720x24 -nolisten tcp >/tmp/camcord-xvfb.log 2>&1 &
  sleep 1
  openbox >/tmp/camcord-openbox.log 2>&1 &
  sleep 1
  picom --backend xrender >/tmp/camcord-compositor.log 2>&1 &
  pulseaudio --start --exit-idle-time=-1
  pactl load-module module-null-sink sink_name=camcord_test >/dev/null
  pactl load-module module-null-sink sink_name=camcord_mic >/dev/null
  pactl load-module module-remap-source master=camcord_mic.monitor source_name=camcord_test_mic >/dev/null
  pactl set-default-sink camcord_test
  pactl set-default-source camcord_test_mic
  export CAMCORD_DESKTOP_TEST=1
  cd linux
  npm test
  python3 tests/portal_test.py
  export CAMCORD_SMOKE_TEST=1 CAMCORD_TEST_DATA=/tmp/camcord-ui-smoke
  export CAMCORD_TEST_SHORTCUT_HELPER=/workspace/linux/tests/shortcut.py
  # Root exists only inside this disposable fixture container. Real installs
  # retain the root-owned SUID sandbox and run as the logged-in desktop user.
  /opt/camcord/camcord --no-sandbox --smoke-test
'
dpkg -r camcord
test ! -e /opt/camcord/camcord
echo 'PASS: Debian package metadata, isolated install, X11/Pulse recording, React UI and uninstall'
