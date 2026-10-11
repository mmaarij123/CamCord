import { spawnSync } from 'node:child_process';
const result=spawnSync(process.platform==='win32'?'python':'python3',['-m','py_compile','src/capture_helper.py','tests/x11_fixture.py','tests/portal_test.py','tests/playback.py'],{stdio:'inherit'});
if(result.status!==0)throw result.error || new Error('Python capture helper syntax check failed.');
