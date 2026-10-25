import { mkdir, copyFile } from 'node:fs/promises';
const base = new URL('../shared/', import.meta.url);
await mkdir(base, { recursive: true });
export const sharedFiles = ['recording-quality.js', 'bitrate-presets.json'];
for (const file of sharedFiles) {
  await copyFile(new URL(`../../ui/src/${file}`, import.meta.url), new URL(file, base));
}
