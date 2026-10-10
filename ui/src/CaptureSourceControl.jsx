import IconButton from '@mui/material/IconButton';
import MenuItem from '@mui/material/MenuItem';
import Select from '@mui/material/Select';
import ToggleButton from '@mui/material/ToggleButton';
import ToggleButtonGroup from '@mui/material/ToggleButtonGroup';
import Tooltip from '@mui/material/Tooltip';
import RefreshRounded from '@mui/icons-material/RefreshRounded';

export default function CaptureSourceControl({ source, sources, disabled, onAction }) {
  const windows = source.kind === 'window';
  const entries = sources.filter((entry) => entry.kind === (windows ? 'window' : 'display'));
  const selectedId = source.ready && entries.some((entry) => entry.id === source.id) ? source.id : '';
  const label = windows ? 'Window' : 'Display';
  return (
    <section className="source-settings" aria-label="Capture source">
      <div className="source-heading"><span className="field-label" id="source-mode-label">Capture source</span>
        <Tooltip title="Refresh displays and windows"><IconButton size="small" disabled={disabled} aria-label="Refresh capture sources" onClick={() => onAction({ type: 'refreshSources' })}><RefreshRounded /></IconButton></Tooltip>
      </div>
      <ToggleButtonGroup className="source-mode-picker fps-picker" value={source.kind} exclusive fullWidth disabled={disabled}
        aria-labelledby="source-mode-label" onChange={(_, kind) => kind && onAction({ type: 'sourceMode', kind })}>
        <ToggleButton value="display" aria-label="Record screen">Screen</ToggleButton>
        <ToggleButton value="window" aria-label="Record window">Window</ToggleButton>
      </ToggleButtonGroup>
      <span className="field-label source-detail-label" id="source-detail-label">{label}</span>
      <Select className="bitrate-picker source-picker" id="capture-source-select" labelId="source-detail-label" fullWidth size="small"
        displayEmpty value={selectedId} disabled={disabled || entries.length === 0}
        inputProps={{ 'aria-describedby': 'source-description' }}
        renderValue={(id) => entries.find((entry) => entry.id === id)?.label || `Choose a ${label.toLowerCase()}`}
        MenuProps={{ slotProps: { paper: { className: 'source-menu bitrate-menu', style: { maxHeight: 320, maxWidth: 480 } } } }}
        onChange={(event) => onAction({ type: 'selectSource', id: event.target.value })}>
        <MenuItem disabled value="">Choose a {label.toLowerCase()}</MenuItem>
        {entries.map((entry) => <MenuItem key={entry.id} value={entry.id} title={entry.label}><span className="source-option-label">{entry.label}</span></MenuItem>)}
      </Select>
      <p className="field-hint source-description" id="source-description">
        {windows ? entries.length ? 'Only this window is captured, even behind other windows. Keep it restored; closing or minimizing stops and saves.' : 'No available windows. Restore a window and refresh the list.'
          : 'Capture the full selected display. Your cursor stays hidden.'}
      </p>
      {!source.ready ? <p className="source-required" role="status">Choose an available {label.toLowerCase()} before recording.</p> : null}
      {windows ? <span className="field-hint source-audio-hint">System audio still includes all desktop playback.</span> : null}
    </section>
  );
}
