# SPDX-License-Identifier: Apache-2.0
"""真实SDL推箱输入、续局、低预算与空闲无刷新。 / Real SDL Sokoban input, resume, low budget and idle without repaint."""
import os,pathlib,json,re,subprocess,sys,tempfile,zlib
levels=json.loads((pathlib.Path(__file__).resolve().parents[1]/'assets/sokoban/levels.json').read_text())['levels']
with tempfile.TemporaryDirectory() as folder:
 root=pathlib.Path(folder);books=root/'books';state=root/'state';books.mkdir();state.mkdir()
 def snapshot():
  rows=[]
  for f in state.glob('sokoban.[ab]'):
   b=f.read_bytes();assert b[:4]==b'PNBL' and len(b)==1304 and zlib.crc32(b[:-4])==int.from_bytes(b[-4:],'little');rows.append((int.from_bytes(b[12:20],'little'),b[20:-4]))
  return max(rows)[1]
 def run(script,budget=2097152,frames=None):
  env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_CLOCK='12:00',PN_SIM_LIBRARY_SCRIPT=script)
  if frames:frames.mkdir();env['PN_SIM_FRAME_DIR']=str(frames)
  p=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state),'--budget',str(budget)],env=env,capture_output=True,text=True,timeout=90)
  assert p.returncode==0 and 'used=0 live=0' in p.stdout,(p.stdout,p.stderr)
  assert 'sokoban screen=' in p.stdout,p.stdout
  assert not re.search(r'sokoban screen=\d+ status=[1-9]',p.stdout),p.stdout
  return p.stdout
 enter='library,tap:460:190,tap:342:648,wait:400,'
 out=run(enter+'window-close');assert not list(state.glob('sokoban.[ab]'))
 out=run(enter+'tap:340:952,window-close');initial=snapshot();assert initial[:4]==b'PNSV' and initial[20]==levels[0]['player']
 level=levels[0];w,h=level['w'],level['h'];cell=min(96,612//w,684//h)//2*2;x=(684-w*cell)//2;y=184+(684-h*cell)//2
 box,d=level['solution'][0];delta=(-w,1,w,-1)[d];stand=box-delta
 tap=f'tap:{x+(stand%w)*cell+cell//2}:{y+(stand//w)*cell+cell//2}'
 out=run(enter+tap+',key:'+('up','right','down','left')[d]+',window-close');saved=snapshot();assert int.from_bytes(saved[16:20],'little')==1 and saved[36:52]!=initial[36:52]
 out=run(enter+'wait:4000,window-close');assert snapshot()==saved and out.count('sokoban screen=1 status=0')==1,out
 out=run(enter+'key:z,window-close');undone=snapshot();assert undone[36:52]==initial[36:52] and int.from_bytes(undone[16:20],'little')==0
 out=run(enter+'key:r,tap:340:814,window-close');assert snapshot()==undone
 out=run(enter+'lock,wait:400,enter,wait:400,window-close');assert 'lock_preview wake status=0' in out and snapshot()==undone,out
 # 新局确认取消不能改存档；整屏清理只由用户命令触发。/ Cancelling a new game preserves saves; cleaning is explicitly requested.
 out=run(enter+'key:p,tap:340:814,key:c,enter,window-close');assert snapshot()==undone and out.count('entertainment page=1 status=0')>=2,out
 print('Sokoban SDL: touch path, push, undo, cross-process resume, lock, confirm cancel, 2MiB and idle no repaint passed')
