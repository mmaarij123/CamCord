import { lazy, Suspense, useEffect, useRef, useState } from 'react';
import Alert from '@mui/material/Alert';
import Button from '@mui/material/Button';
import CircularProgress from '@mui/material/CircularProgress';
import Snackbar from '@mui/material/Snackbar';
import Switch from '@mui/material/Switch';
import ToggleButton from '@mui/material/ToggleButton';
import ToggleButtonGroup from '@mui/material/ToggleButtonGroup';
import Tooltip from '@mui/material/Tooltip';
import FiberManualRecordRounded from '@mui/icons-material/FiberManualRecordRounded';
import FolderOpenRounded from '@mui/icons-material/FolderOpenRounded';
import GraphicEqRounded from '@mui/icons-material/GraphicEqRounded';
import MicRounded from '@mui/icons-material/MicRounded';
import PauseRounded from '@mui/icons-material/PauseRounded';
import PlayArrowRounded from '@mui/icons-material/PlayArrowRounded';
import StopRounded from '@mui/icons-material/StopRounded';
import TuneRounded from '@mui/icons-material/TuneRounded';
import PowerSettingsNewRounded from '@mui/icons-material/PowerSettingsNewRounded';
import SystemUpdateAltRounded from '@mui/icons-material/SystemUpdateAltRounded';
import LinearProgress from '@mui/material/LinearProgress';
import { BrandMark, SettingRow, Waveform } from './components.jsx';
import { isNative, sendToHost, subscribeToHost } from './bridge.js';
import { INITIAL_STATE, STATE_LABELS, formatElapsed, mergeState, updateSettings, canInstallUpdate } from './recorder-state.js';
const BitrateControl = lazy(() => import('./BitrateControl.jsx'));
const CaptureSourceControl = lazy(() => import('./CaptureSourceControl.jsx'));

const RESOLUTIONS = [{ height: 480, label: 'SD' }, { height: 720, label: 'HD' }, { height: 1080, label: 'FHD' }];
const FRAME_RATES = [15, 30, 60];
const HIGH_FRAME_RATES = [...FRAME_RATES, 120];

export default function App() {
  const [app, setApp] = useState(INITIAL_STATE);
  const [pendingState, setPendingState] = useState('');
  const [notice, setNotice] = useState(null);
  const [connected, setConnected] = useState(!isNative);
  const [startupPending, setStartupPending] = useState(false);
  const [sourcePending, setSourcePending] = useState(false);
  const previewTimeout = useRef(null);
  const currentState = pendingState || app.state;
  const idle = currentState === 'idle';
  const recording = currentState === 'recording';
  const paused = currentState === 'paused';
  const busy = !idle && !recording && !paused;
  const installingUpdate = app.update.status === 'installing';
  const updateBusy = ['checking', 'downloading', 'installing'].includes(app.update.status);
  const selectingSource = sourcePending || app.selectingSource;
  const settingsDisabled = !idle || !connected || installingUpdate || selectingSource;
  const elapsed = formatElapsed(app.elapsedSeconds);
  const frameRates = app.settings.height === 1080 ? HIGH_FRAME_RATES : FRAME_RATES;
  const width = app.settings.height === 480 ? 854 : app.settings.height === 720 ? 1280 : 1920;
  const qualityLabel = `${width} × ${app.settings.height} · ${app.settings.fps} FPS`;

  useEffect(() => {
    const unsubscribe = subscribeToHost((message) => {
      setApp((previous) => mergeState(previous, message));
      setPendingState('');
      setConnected(true);
      setStartupPending(false);
      setSourcePending(false);
      if (message.notice?.text) setNotice({ ...message.notice, key: Date.now() });
    });
    sendToHost({ type: 'ready' });
    return unsubscribe;
  }, []);

  useEffect(() => {
    if (isNative) return;
    const interval = window.setInterval(() => {
      setApp((previous) => previous.state === 'recording'
        ? { ...previous, elapsedSeconds: previous.elapsedSeconds + 1 }
        : previous);
    }, 1000);
    return () => window.clearInterval(interval);
  }, []);

  useEffect(() => () => window.clearTimeout(previewTimeout.current), []);

  function changeSettings(changes) {
    if (settingsDisabled) return;
    const settings = updateSettings(app.settings, changes);
    // Native calls stay in event handlers: React may invoke state updaters twice.
    sendToHost({ type: 'settings', settings });
    setApp((previous) => ({ ...previous, settings }));
  }

  function previewTransition(nextState, values = {}) {
    previewTimeout.current = window.setTimeout(() => {
      setApp((previous) => ({ ...previous, ...values, state: nextState }));
      setPendingState('');
    }, 650);
  }

  function changeStartup(enabled) {
    if (settingsDisabled || startupPending) return;
    setStartupPending(true);
    if (!sendToHost({ type: 'startup', enabled })) {
      setApp((previous) => ({ ...previous, startupEnabled: enabled }));
      setStartupPending(false);
    }
  }

  function updateApp(type) {
    if (!connected || updateBusy) return;
    if (type === 'installUpdate' && !canInstallUpdate({ ...app, state: currentState })) return;
    if (!sendToHost({ type })) {
      setNotice({ severity: 'info', text: 'Updates are available in the CamCord desktop app.', key: Date.now() });
    }
  }

  function startRecording() {
    if (settingsDisabled || !app.captureSource.ready) return;
    setPendingState('starting');
    if (!sendToHost({ type: 'start', settings: app.settings })) {
      previewTransition('recording', { elapsedSeconds: 0, encoder: 'Preview mode' });
    }
  }

  function pauseRecording() {
    if (!recording && !paused) return;
    setPendingState(paused ? 'resuming' : 'pausing');
    if (!sendToHost({ type: 'pause' })) previewTransition(paused ? 'recording' : 'paused');
  }

  function stopRecording() {
    if (!recording && !paused) return;
    setPendingState('saving');
    if (!sendToHost({ type: 'stop' })) {
      previewTransition('idle', { lastOutput: 'Preview capture — no file recorded' });
    }
  }

  function changeOutputFolder() {
    if (settingsDisabled) return;
    if (!sendToHost({ type: 'chooseOutput' })) {
      setNotice({ severity: 'info', text: 'Folder selection is available in the CamCord desktop app.', key: Date.now() });
    }
  }

  function changeSource(action) {
    if (settingsDisabled) return;
    setSourcePending(true);
    if (!sendToHost(action)) {
      setSourcePending(false);
      if (action.type === 'sourceMode') {
        setApp((previous) => ({ ...previous, captureSource: { ...previous.captureSource, kind: action.kind,
          ready: action.kind === 'display', id: action.kind === 'window' ? '' : 'preview-primary', label: action.kind === 'display' ? 'Primary display' : 'Choose a source' } }));
      } else if (action.type === 'selectSource') {
        const entry = app.sources.find((source) => source.id === action.id);
        if (entry) setApp((previous) => ({ ...previous, captureSource: { ...previous.captureSource, id: entry.id, label: entry.label, ready: previous.captureSource.kind !== 'region' } }));
      } else setNotice({ severity: 'info', text: 'Use the CamCord desktop app to refresh sources or select an area.', key: Date.now() });
    }
  }

  function openFolder(type) {
    if (!sendToHost({ type })) {
      setNotice({ severity: 'info', text: 'Open the CamCord desktop app to access recording files.', key: Date.now() });
    }
  }

  const readoutNote = !connected ? 'Connecting to the recorder…'
    : selectingSource ? 'Select your capture area. Press Esc to cancel.'
    : currentState === 'starting' ? 'Checking your selected source, audio, and encoder.'
      : currentState === 'pausing' ? 'Finishing the current recording segment.'
        : currentState === 'resuming' ? 'Preparing your next recording segment.'
          : currentState === 'saving' ? 'Finalizing video and audio. Saving can take several minutes for long recordings. Keep CamCord open.'
            : paused ? 'Capture is paused. Resume when you are ready.'
              : recording ? (app.encoder ? `Encoding with ${app.encoder}` : `Capturing ${app.captureSource.label}`)
                : !app.captureSource.ready ? 'Choose an available capture source before recording.'
                  : app.captureSource.kind === 'window' ? 'Only the selected window will be recorded. Keep it restored.'
                    : app.captureSource.kind === 'region' ? 'Only your selected area will be recorded.'
                : app.captureExcluded ? 'Your cursor and CamCord controls stay out of the final video.'
                  : 'Your cursor is hidden. Minimize CamCord to keep its controls out of the video.';

  return (
    <div className="app-shell">
      <div className="ambient ambient-one" aria-hidden="true" />
      <div className="ambient ambient-two" aria-hidden="true" />
      <header className="topbar">
        <div className="brand-lockup">
          <BrandMark />
          <div><span className="brand-name">CamCord</span><span className="brand-edition">desktop recorder</span></div>
        </div>
        <Tooltip title="Open recording folder">
          <button className="folder-button" onClick={() => openFolder('openOutput')} aria-label={`Open recording folder: ${app.outputFolder}`}>
            <FolderOpenRounded fontSize="small" /><span>{app.outputFolder}</span>
          </button>
        </Tooltip>
      </header>

      {!isNative ? <div className="preview-banner">Interface preview · no screen or audio is recorded</div> : null}

      <main className="workspace">
        <section className={`monitor-panel state-${currentState}`} aria-label="Recording controls">
          <div className="monitor-topline">
            <div className="status-label" role="status" aria-live="polite">
              {busy ? <CircularProgress size={12} thickness={5} aria-hidden="true" /> : <span className="status-pulse" />}
              {connected ? STATE_LABELS[currentState] : 'Connecting'}
            </div>
            <span className="quality-readout">{qualityLabel}</span>
          </div>
          <div className="readout">
            <div className="aperture" aria-hidden="true">
              <span className="aperture-ring ring-one" />
              <span className="aperture-ring ring-two" />
              <span className="aperture-core"><span /></span>
            </div>
            <div className="timer" role="timer" aria-label={`Elapsed time ${elapsed}`}>{elapsed}</div>
            <p className="readout-note">{readoutNote}</p>
          </div>
          <Waveform active={recording} paused={paused} />
          <div className="control-dock">
            {idle ? (
              <Button className="record-button" variant="contained" size="large" disabled={settingsDisabled || !app.captureSource.ready} startIcon={<FiberManualRecordRounded />} onClick={startRecording}>Start recording</Button>
            ) : (
              <>
                <Button className="pause-button" variant="outlined" disabled={busy} startIcon={paused ? <PlayArrowRounded /> : <PauseRounded />} onClick={pauseRecording}>{paused ? 'Resume' : 'Pause'}</Button>
                <Button className="stop-button" variant="contained" disabled={busy} startIcon={<StopRounded />} onClick={stopRecording}>Stop &amp; save</Button>
              </>
            )}
          </div>
          {app.lastOutput && idle ? (
            <button className="last-capture" onClick={() => openFolder('openLast')} title={app.lastOutput}>
              <span>Last capture</span><strong>{app.lastOutput}</strong><FolderOpenRounded fontSize="small" />
            </button>
          ) : null}
        </section>

        <aside className="settings-panel" aria-label="Capture setup">
          <div className="settings-heading"><div><span>Capture setup</span><h2>Recording setup</h2></div><TuneRounded /></div>
          <Suspense fallback={<div className="source-settings source-loading" role="status">Loading capture sources…</div>}>
            <CaptureSourceControl source={app.captureSource} sources={app.sources} disabled={settingsDisabled} selecting={selectingSource} onAction={changeSource} />
          </Suspense>
          <div className="settings-group">
            <span className="field-label" id="resolution-label">Resolution</span>
            <ToggleButtonGroup className="resolution-picker" aria-labelledby="resolution-label" exclusive fullWidth value={app.settings.height} disabled={settingsDisabled} onChange={(_, value) => value && changeSettings({ height: value })}>
              {RESOLUTIONS.map(({ height, label }) => <ToggleButton key={height} value={height} aria-label={`${height}p ${label}`}><strong>{height}</strong><span>{label}</span></ToggleButton>)}
            </ToggleButtonGroup>
          </div>
          <div className="settings-group">
            <span className="field-label" id="fps-label">Frame rate</span>
            <ToggleButtonGroup className="fps-picker" aria-labelledby="fps-label" exclusive fullWidth value={app.settings.fps} disabled={settingsDisabled} onChange={(_, value) => value && changeSettings({ fps: value })}>
              {frameRates.map((fps) => <ToggleButton key={fps} value={fps} aria-label={`${fps} frames per second`}>{fps}</ToggleButton>)}
            </ToggleButtonGroup>
            <span className="field-hint">120 FPS is available at 1080p with supported hardware.</span>
          </div>
          <Suspense fallback={<div className="settings-group bitrate-loading" role="status"><span className="field-label">Video bitrate</span><span className="field-hint">Loading bitrate options…</span></div>}>
            <BitrateControl settings={app.settings} disabled={settingsDisabled} onChange={changeSettings} />
          </Suspense>
          <div className="settings-group">
            <span className="field-label">Save location</span>
            <div className="save-location" title={app.outputFolder}>
              <span className="save-location-icon" aria-hidden="true"><FolderOpenRounded /></span>
              <span className="save-location-path">{app.outputFolder}</span>
              <Button className="change-folder-button" variant="outlined" size="small" disabled={settingsDisabled} onClick={changeOutputFolder} aria-label="Change recording folder">Change</Button>
            </div>
            <span className="field-hint">Choose any folder or drive for future recordings.</span>
          </div>
          <div className="settings-divider" />
          <div className="audio-settings">
            <SettingRow icon={<GraphicEqRounded />} label="System audio" description="Desktop playback" action={<Switch checked={app.settings.systemAudio} disabled={settingsDisabled} onChange={(event) => changeSettings({ systemAudio: event.target.checked })} slotProps={{ input: { 'aria-label': 'System audio' } }} />} />
            <SettingRow icon={<MicRounded />} label="Microphone" description="Default input" action={<Switch checked={app.settings.microphone} disabled={settingsDisabled} onChange={(event) => changeSettings({ microphone: event.target.checked })} slotProps={{ input: { 'aria-label': 'Microphone' } }} />} />
          </div>
          <section className="app-preferences" aria-labelledby="app-preferences-heading">
            <h3 id="app-preferences-heading">App settings</h3>
            <SettingRow icon={<PowerSettingsNewRounded />} label="Launch at Windows startup" description="Open minimized when you sign in" action={<Switch checked={app.startupEnabled} disabled={settingsDisabled || startupPending} onChange={(event) => changeStartup(event.target.checked)} slotProps={{ input: { 'aria-label': 'Launch at Windows startup' } }} />} />
            <SettingRow icon={<SystemUpdateAltRounded />} label="Automatic updates" description="Check and download new releases" action={<Switch checked={app.settings.autoCheckUpdates} disabled={settingsDisabled} onChange={(event) => changeSettings({ autoCheckUpdates: event.target.checked })} slotProps={{ input: { 'aria-label': 'Automatically check for updates' } }} />} />
            <div className="update-heading"><strong>Updates</strong><span>Version {app.appVersion}</span></div>
            <p className={`update-message ${app.update.status === 'error' ? 'is-error' : ''}`} role="status" aria-live="polite">
              {app.update.version && ['available', 'downloading', 'ready'].includes(app.update.status) ? `Version ${app.update.version} · ` : ''}{app.update.message}
            </p>
            {app.update.status === 'downloading' ? <LinearProgress className="update-progress" variant="determinate" value={Math.max(0, Math.min(100, app.update.progress))} aria-label="Update download progress" /> : null}
            <div className="update-actions">
              <Button variant="outlined" size="small" disabled={!connected || updateBusy} onClick={() => updateApp('checkUpdates')} startIcon={app.update.status === 'checking' ? <CircularProgress size={12} color="inherit" /> : null}>Check now</Button>
              {app.update.status === 'available' ? <Button variant="contained" size="small" disabled={!connected || updateBusy} onClick={() => updateApp('downloadUpdate')}>Download update</Button> : null}
              {app.update.status === 'ready' ? <Tooltip title={!idle ? 'Stop and save your recording first' : 'CamCord will restart after installation'}><span><Button variant="contained" size="small" disabled={!connected || !canInstallUpdate({ ...app, state: currentState })} onClick={() => updateApp('installUpdate')}>Install &amp; restart</Button></span></Tooltip> : null}
            </div>
            <span className="field-hint">Updates install when you choose. Stop and save your recording first.</span>
          </section>
        </aside>
      </main>

      <Snackbar key={notice?.key} open={Boolean(notice)} autoHideDuration={notice?.severity === 'error' ? null : 6500} onClose={(_, reason) => reason !== 'clickaway' && setNotice(null)} anchorOrigin={{ vertical: 'bottom', horizontal: 'center' }}>
        <Alert severity={notice?.severity || 'info'} variant="filled" onClose={() => setNotice(null)}>{notice?.text}</Alert>
      </Snackbar>
    </div>
  );
}
