const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('camcordDesktop', Object.freeze({
  platform: 'linux',
  postMessage(message) {
    if (typeof message === 'string' && message.length <= 4096) ipcRenderer.send('camcord-command', message);
  },
  subscribe(listener) {
    const handler = (_, state) => listener({data:state});
    ipcRenderer.on('camcord-state', handler);
    return () => ipcRenderer.removeListener('camcord-state', handler);
  },
}));
