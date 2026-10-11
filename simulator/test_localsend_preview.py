"""共享确认窗口的点击/分页/取消演练。 / Shared consent-window click, paging and cancellation rehearsal."""
import os
import subprocess
import sys

def run(script):
    env = dict(os.environ,SDL_VIDEODRIVER='dummy',PN_LS_PREVIEW_SCRIPT=script)
    result = subprocess.run([sys.argv[1]],env=env,capture_output=True,text=True,timeout=15)
    assert result.returncode==0,result.stderr
    return result.stdout

out = run('tap:500:1120,tap:300:420,tap:300:650,tap:500:1120,tap:100:1120,quit')
assert 'action=101 status=0 phase=1' in out,out
assert 'action=104 status=0 phase=1' in out,out
assert 'action=1 status=0 phase=2' in out,out
assert 'action=3 status=0 phase=7' in out,out
out = run('tap:550:1010,tap:550:1010,tap:550:1010,tap:100:1120,quit')
assert 'action=6 status=0 phase=1 page=1' in out and 'action=6 status=0 phase=1 page=2' in out,out
assert out.count('action=6')==2 and 'action=2 status=0 phase=6' in out,out
out = run('space,space,space,space,space,quit')
assert all(f'sample={i}' in out for i in range(5)),out
print('LocalSend UI preview: explicit choices, real session consent/reject/cancel, paging and state fixtures passed')
