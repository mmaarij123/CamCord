import { readFile, readdir, mkdir, copyFile, writeFile } from 'node:fs/promises';
import path from 'node:path';

const [root, output] = process.argv.slice(2);
if (!root || !output) throw new Error('Usage: node collect-licenses.mjs <project-root> <license-output>');
const lock = JSON.parse(await readFile(path.join(root, 'ui', 'package-lock.json'), 'utf8'));
const index = ['Frontend dependency notices', 'Generated from the locked production dependencies; original license files follow.', ''];
for (const [relative, entry] of Object.entries(lock.packages)) {
  // package-lock can retain peer/optional resolution candidates which npm does
  // not install for this platform. Only ship notices for the locked packages
  // that are actually part of the production installation.
  if (!relative || entry.dev || entry.optional || entry.extraneous) continue;
  const directory = path.join(root, 'ui', relative);
  const manifest = JSON.parse(await readFile(path.join(directory, 'package.json'), 'utf8'));
  const files = (await readdir(directory, { withFileTypes: true })).filter(file => file.isFile() && /^(licen[cs]e|copying|ofl)(\.|-|$)/i.test(file.name));
  if (!files.length) throw new Error(`No license found for shipped dependency ${manifest.name}`);
  const destination = path.join(output, 'frontend', `${manifest.name.replaceAll('/', '__')}@${manifest.version}`);
  await mkdir(destination, { recursive: true });
  for (const file of files) await copyFile(path.join(directory, file.name), path.join(destination, file.name));
  index.push(`${manifest.name} ${manifest.version} — ${manifest.license ?? 'See license files'}`);
}
await writeFile(path.join(output, 'FRONTEND-NOTICES.txt'), index.join('\n') + '\n');
