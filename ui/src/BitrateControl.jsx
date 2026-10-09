import Select from '@mui/material/Select';
import MenuItem from '@mui/material/MenuItem';
import WarningAmberRounded from '@mui/icons-material/WarningAmberRounded';
import { BITRATE_PRESETS, HIGH_STORAGE_MBPS, videoBitrateKbps, videoPeakMbps, suggestedBitrate, storageEstimate } from './recording-quality.js';

export default function BitrateControl({ settings, disabled, onChange }) {
  const bitrateMbps = videoBitrateKbps(settings) / 1000;
  const suggestedMbps = suggestedBitrate(settings);
  const storage = storageEstimate(settings);
  return (
    <div className="settings-group">
      <span className="field-label" id="bitrate-label">Video bitrate</span>
      <Select className="bitrate-picker" labelId="bitrate-label" id="bitrate-select" fullWidth size="small"
        value={settings.bitrateMbps} disabled={disabled}
        onChange={(event) => onChange({ bitrateMbps: Number(event.target.value) })}
        inputProps={{ 'aria-describedby': 'bitrate-description bitrate-storage' }}
        renderValue={(value) => value === 0 ? `Auto · ${bitrateMbps} Mbps` : `${value} Mbps`}
        MenuProps={{ slotProps: { paper: { className: 'bitrate-menu', style: { maxHeight: 320 } } } }}>
        {BITRATE_PRESETS.map((rate) => (
          <MenuItem key={rate} value={rate} aria-label={rate === 0 ? 'Auto, recommended'
            : `${rate} Mbps${rate >= HIGH_STORAGE_MBPS ? ', high storage' : rate === suggestedMbps ? ', suggested' : ''}`}>
            <span className="bitrate-option-value">{rate === 0 ? 'Auto' : `${rate} Mbps`}</span>
            {rate >= HIGH_STORAGE_MBPS ? <span className="bitrate-option-note is-warning"><WarningAmberRounded aria-hidden="true" />High storage</span>
              : rate === 0 ? <span className="bitrate-option-note">Recommended</span>
                : rate === suggestedMbps ? <span className="bitrate-option-note">Suggested</span> : null}
          </MenuItem>
        ))}
      </Select>
      <span className="field-hint" id="bitrate-description">{settings.bitrateMbps === 0
        ? 'Auto balances quality and size for your resolution and FPS.'
        : `Manual target · up to ${Number(videoPeakMbps(settings).toFixed(2))} Mbps peak. Higher settings may need faster hardware.`}</span>
      <div className="bitrate-storage" id="bitrate-storage">
        <div className="storage-estimate"><span>Estimated size</span><strong>≈ {Math.round(storage.mbPerMinute)} MB/min</strong><span>{storage.gbPerHour.toFixed(1)} GB/hour</span></div>
        {bitrateMbps >= HIGH_STORAGE_MBPS ? <p className="storage-warning" role="status"><WarningAmberRounded aria-hidden="true" />Consumes more storage</p> : null}
        <span className="field-hint storage-note">Actual size varies. Saving also needs temporary space.</span>
      </div>
    </div>
  );
}
