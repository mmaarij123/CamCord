const WAVE_HEIGHTS = [18, 30, 22, 45, 34, 58, 27, 70, 42, 82, 55, 92, 38, 74, 48, 88, 60, 96, 52, 78, 44, 68, 36, 62, 28, 50, 22, 39, 18, 28, 16, 22];

export function BrandMark({ size = 40 }) {
  return (
    <img src="/camcord-logo.png" width={size} height={size} alt="CamCord mark" draggable={false} />
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
