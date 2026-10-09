import presets from './bitrate-presets.json' with { type: 'json' };

export const BITRATE_PRESETS = presets;
export const HIGH_STORAGE_MBPS = 32;

export function normalizeBitrate(value) {
  return Number.isInteger(value) && BITRATE_PRESETS.includes(value) ? value : 0;
}

export function videoBitrateKbps(settings) {
  const manual = normalizeBitrate(settings.bitrateMbps);
  if (manual) return manual * 1000;
  if (settings.height === 480) return settings.fps >= 60 ? 5000 : settings.fps <= 15 ? 2500 : 3500;
  if (settings.height === 720) return settings.fps >= 60 ? 8000 : settings.fps <= 15 ? 4000 : 5500;
  if (settings.fps >= 120) return 24000;
  if (settings.fps >= 60) return 14000;
  return settings.fps <= 15 ? 6000 : 9000;
}

export function videoPeakMbps(settings) {
  return Math.min(videoBitrateKbps(settings) * 1.3 / 1000, 100);
}

export function suggestedBitrate(settings) {
  const automatic = videoBitrateKbps({ ...settings, bitrateMbps: 0 }) / 1000;
  return BITRATE_PRESETS.find((rate) => rate >= automatic);
}

export function storageEstimate(settings) {
  // Decimal MB/GB, including the single final AAC track (not temporary WAVs).
  const mbps = videoBitrateKbps(settings) / 1000 + (settings.systemAudio || settings.microphone ? 0.192 : 0);
  return { mbPerMinute: mbps * 60 / 8, gbPerHour: mbps * 3600 / 8 / 1000 };
}
