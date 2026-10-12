FROM node:22-bookworm-slim
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates python3 python3-gi gir1.2-gstreamer-1.0 gir1.2-gst-plugins-base-1.0 \
    gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-pipewire \
    ffmpeg pulseaudio pulseaudio-utils xdg-desktop-portal xdg-utils pkexec \
    libgtk-3-0 libnss3 libxss1 libasound2 libgbm1 libdrm2 libx11-6 libxcomposite1 libxrandr2 \
    libatk-bridge2.0-0 libatspi2.0-0 libxkbcommon0 libcups2 libxtst6 xvfb xauth openbox picom dbus-x11 \
    && apt-get clean
WORKDIR /workspace
COPY linux/package*.json linux/
RUN cd linux && npm ci --ignore-scripts --no-audit --no-fund
COPY ui ui
COPY linux linux
COPY scripts/collect-licenses.mjs scripts/collect-licenses.mjs
COPY LICENSE README.md THIRD_PARTY_NOTICES.md ./
RUN cd linux && npm test && npm run package
CMD ["bash", "linux/scripts/integration.sh"]
