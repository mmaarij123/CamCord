import Button from '@mui/material/Button';
import Switch from '@mui/material/Switch';
import Select from '@mui/material/Select';
import MenuItem from '@mui/material/MenuItem';
import LinearProgress from '@mui/material/LinearProgress';
import ReplayRounded from '@mui/icons-material/ReplayRounded';
import { SettingRow } from './components.jsx';
import { REPLAY_DURATIONS, REPLAY_HOTKEYS } from './replay-settings.js';
import { storageEstimate } from './recording-quality.js';
export default function ReplayControl({settings,replay,disabled,settingsDisabled,onSettings,onAction}) {
  const mb=storageEstimate(settings).mbPerMinute*settings.replaySeconds/60;
  return <section className="replay-settings" aria-labelledby="replay-heading">
    <h3 id="replay-heading">Instant Replay</h3>
    <SettingRow icon={<ReplayRounded />} label="Rolling capture" description={replay.enabled?'Capture is on in the background':'Off until you turn it on'} action={<Switch checked={replay.enabled} disabled={disabled||replay.busy} onChange={event=>onAction({type:'replayEnabled',enabled:event.target.checked})} slotProps={{input:{'aria-label':'Enable Instant Replay'}}} />} />
    <div className="settings-group">
      <label className="field-label" id="replay-duration-label">Clip duration</label>
      <Select fullWidth size="small" labelId="replay-duration-label" value={settings.replaySeconds} disabled={settingsDisabled} onChange={event=>onSettings({replaySeconds:event.target.value})}>
        {REPLAY_DURATIONS.map(seconds=><MenuItem key={seconds} value={seconds}>{seconds<60?`${seconds} seconds`:`${seconds/60} minutes`}</MenuItem>)}
      </Select>
    </div>
    <div className="settings-group">
      <label className="field-label" id="replay-hotkey-label">Save clip shortcut</label>
      <Select fullWidth size="small" labelId="replay-hotkey-label" value={settings.replayHotkey} disabled={settingsDisabled} onChange={event=>onSettings({replayHotkey:event.target.value})}>
        {REPLAY_HOTKEYS.map(key=><MenuItem key={key} value={key}>{key}</MenuItem>)}
      </Select>
    </div>
    <span className="field-hint">Uses your selected source, quality, audio and save folder. Background capture uses GPU/CPU and disk. Estimated video buffer: {mb>=1000?`${(mb/1000).toFixed(2)} GB`:`${Math.round(mb)} MB`} plus temporary audio and chunk overhead.</span>
    {replay.enabled?<span className="field-hint">Turn replay off to change source, quality, audio, duration, shortcut or folder. Extra free space is required when saving clips.</span>:null}
    <p className="update-message" role="status" aria-live="polite">{replay.message}</p>
    {replay.enabled?<>
      <LinearProgress variant="determinate" value={Math.min(100,replay.bufferedSeconds/settings.replaySeconds*100)} aria-label="Replay buffer progress" />
      <span className="field-hint">{replay.bufferedSeconds} / {settings.replaySeconds} seconds buffered. If not full, saves the time available.</span>
    </>:null}
    <div className="update-actions">
      <Button variant="contained" size="small" disabled={!replay.enabled||replay.busy||replay.bufferedSeconds<1} onClick={()=>onAction({type:'saveClip'})}>{replay.saving?'Saving clip…':'Save clip'}</Button>
      {replay.lastClip?<Button size="small" variant="outlined" onClick={()=>onAction({type:'openClip'})}>Open last clip</Button>:null}
    </div>
    <span className="field-hint">Shortcut works while CamCord is minimized, unless another app or your desktop reserves it. Replay starts off every launch; unsaved buffer is cleared when turned off.</span>
  </section>;
}
