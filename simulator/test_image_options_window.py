"""真实SDL触摸：图片草稿取消、应用、进程重开及恢复默认。 / Real SDL touch: cancel/apply image drafts, process reopen and restoring the default."""
import os,pathlib,struct,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
 root=pathlib.Path(folder);books=root/'books';books.mkdir();(books/'book.txt').write_text('Original reading option test.\n'*100)
 state=root/'state'
 def run(script):
  result=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state)],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT='library,enter,'+script+'back,quit'),capture_output=True,text=True,timeout=60)
  assert result.returncode==0 and 'used=0 live=0' in result.stdout,(result.stdout,result.stderr)
 def latest():
  packets=[p.read_bytes() for p in state.glob('*.style.*')];assert packets
  packet=max(packets,key=lambda p:int.from_bytes(p[8:16],'little'));at=packet.index(b'PNTS');return struct.unpack_from('<HH',packet,at+4)
 run('styles,tap:300:1060,style-cancel,');assert not list(state.glob('*.style.*'))
 run('styles,tap:300:1060,style-apply,');assert latest()==(9,0x100)
 run('');assert latest()==(9,0x100)
 run('styles,tap:300:1060,style-cancel,');assert latest()==(9,0x100)
 run('styles,tap:300:1060,style-apply,');assert latest()==(4,0)
print('image option window: actual touch, draft cancellation, apply, cross-process persistence and restored legacy schema passed')
