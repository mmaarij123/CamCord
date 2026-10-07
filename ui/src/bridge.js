import { parseStateMessage } from './recorder-state.js';

const webview = window.chrome?.webview;
export const isNative = Boolean(webview);

export function sendToHost(message) {
  if (!webview) return false;
  webview.postMessage(JSON.stringify(message));
  return true;
}

export function subscribeToHost(listener) {
  if (!webview) return () => {};
  const onMessage = (event) => {
    const message = parseStateMessage(event.data);
    if (message) listener(message);
  };
  webview.addEventListener('message', onMessage);
  return () => webview.removeEventListener('message', onMessage);
}
