export const REPLAY_DURATIONS = [30, 60, 90, 120, 180, 240, 300];
export const REPLAY_HOTKEYS = ['Ctrl+Shift+F8', 'Ctrl+Shift+F9', 'Ctrl+Shift+F10', 'Ctrl+Alt+F8', 'Ctrl+Alt+F9', 'Ctrl+Alt+F10'];
export const normalizeReplaySeconds = value => REPLAY_DURATIONS.includes(value) ? value : 60;
export const normalizeReplayHotkey = value => REPLAY_HOTKEYS.includes(value) ? value : REPLAY_HOTKEYS[0];
