import { parseStateMessage } from './recorder-state.js';

const webview = window.chrome?.webview;
const desktop = window.camcordDesktop;
export const isNative = Boolean(webview || desktop);

export function sendToHost(message) {
  if (!webview && !desktop) return false;
  (desktop || webview).postMessage(JSON.stringify(message));
  return true;
}

export function subscribeToHost(listener) {
  if (!webview && !desktop) return () => {};
  const onMessage = (event) => {
    const message = parseStateMessage(event.data);
    if (message) listener(message);
  };
  if (desktop) return desktop.subscribe(onMessage);
  webview.addEventListener('message', onMessage);
  return () => webview.removeEventListener('message', onMessage);
}
