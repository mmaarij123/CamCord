import { mkdir, copyFile } from 'node:fs/promises';
const base = new URL('../shared/', import.meta.url);
await mkdir(base, { recursive: true });
for (const file of ['recording-quality.js', 'bitrate-presets.json']) {
  await copyFile(new URL(`../../ui/src/${file}`, import.meta.url), new URL(file, base));
}
