const WAVE_HEIGHTS = [18, 30, 22, 45, 34, 58, 27, 70, 42, 82, 55, 92, 38, 74, 48, 88, 60, 96, 52, 78, 44, 68, 36, 62, 28, 50, 22, 39, 18, 28, 16, 22];

export function BrandMark({ size = 34 }) {
  return (
    <svg width={size} height={size} viewBox="0 0 36 36" role="img" aria-label="CamCord mark">
      <rect width="36" height="36" rx="11" fill="#f2675f" />
      <path d="M11.2 12.4a3 3 0 0 1 3-3h7.1a3 3 0 0 1 3 3v11.2a3 3 0 0 1-3 3h-7.1a3 3 0 0 1-3-3V12.4Z" fill="#170b0b" />
      <path d="m24.1 15.2 3.7-2.1c.7-.4 1.5.1 1.5.9v8c0 .8-.8 1.3-1.5.9l-3.7-2.1v-5.6Z" fill="#170b0b" />
      <circle cx="17.8" cy="18" r="3.1" fill="#f5f2ed" />
    </svg>
  );
}

export function SettingRow({ icon, label, description, action }) {
  return (
    <div className="setting-row">
      <div className="setting-icon" aria-hidden="true">{icon}</div>
      <div className="setting-copy"><strong>{label}</strong><span>{description}</span></div>
      <div className="setting-action">{action}</div>
    </div>
  );
}

// A recording activity decoration, not an audio input level meter.
export function Waveform({ active, paused }) {
  return (
    <div className={`waveform ${active ? 'is-active' : ''} ${paused ? 'is-paused' : ''}`} aria-hidden="true">
      {WAVE_HEIGHTS.map((height, index) => (
        <span key={index} style={{ '--bar-height': `${height}%`, '--delay': `${(index % 9) * -0.13}s` }} />
      ))}
    </div>
  );
}
