import { spawn } from 'node:child_process';
import { createWriteStream } from 'node:fs';
import { once } from 'node:events';

export class ManagedProcess {
  constructor(executable, args, log, options = {}) {
    this.child = spawn(executable, args, {stdio:['pipe','pipe','pipe'], ...options});
    this.exited = false; this.error = null;
    this.done = new Promise(resolve => {
      this.child.once('error', error => {this.error = error; this.exited = true; resolve({code:null, error});});
      this.child.once('close', (code, signal) => {this.exited = true; resolve({code, signal, error:this.error});});
    });
    this.child.stdin.on('error', error => {this.inputError = error;});
    if (log) {
      this.log = createWriteStream(log, {flags:'a', mode:0o600});
      this.log.on('error', error => {this.error = error; this.child.kill('SIGTERM');});
      this.child.stderr.pipe(this.log);
      this.child.once('close', () => this.log.end());
    } else this.child.stderr.resume();
  }
  async write(bytes, timeout = 30000) {
    if (this.exited || this.inputError) throw new Error('The video encoder stopped accepting frames.');
    const input = this.child.stdin;
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('The video encoder is stalled. Try a lower frame rate or bitrate.')), timeout);
      input.write(bytes, error => {clearTimeout(timer); error ? reject(error) : resolve();});
    });
  }
  async stop({interrupt = false, timeout = 0} = {}) {
    if (!this.exited) {
      this.child.stdin.end();
      // SIGINT alone can leave a raw-video demuxer blocked on its pipe read.
      // EOF releases that read; the signal also releases live audio inputs.
      if (interrupt) this.child.kill('SIGINT');
    }
    let timer;
    if (timeout) timer = setTimeout(() => { if (!this.exited) this.child.kill('SIGKILL'); }, timeout);
    const result = await this.done;
    clearTimeout(timer);
    if (result.error || (result.code !== 0 && !(interrupt && result.code === 255))) throw result.error || new Error('A recording process stopped unexpectedly. Recovery files were kept.');
    return result;
  }
}
export async function run(executable, args, {timeout=30000, maxBytes=2*1024*1024} = {}) {
  const child = spawn(executable, args, {stdio:['ignore','pipe','pipe']});
  let stdout = '', stderr = '', overflow = false;
  const add = (stream, text) => { if (Buffer.byteLength(stdout) + Buffer.byteLength(stderr) + text.length > maxBytes) {overflow=true;child.kill('SIGKILL');return;} if(stream==='out') stdout+=text;else stderr+=text; };
  child.stdout.on('data', bytes => add('out', bytes.toString()));
  child.stderr.on('data', bytes => add('err', bytes.toString()));
  let timedOut=false;
  const timer=setTimeout(()=>{timedOut=true;child.kill('SIGKILL');},timeout);
  try {
    const [code, signal] = await once(child, 'close');
    if (code !== 0 || overflow || timedOut) throw new Error(timedOut ? 'A recording operation timed out.' : stderr.slice(-2000) || `Process failed: ${code ?? signal}`);
    return stdout;
  } finally {clearTimeout(timer);}
}
