# SPDX-License-Identifier: Apache-2.0
"""缓存冷开正文与缺包基线一致，校验期间NEXT不进入正文。 / Cached cold opens match missing-cache bodies; NEXT during verification never reaches the reader."""
import os,pathlib,shutil,subprocess,sys,tempfile,zipfile,random
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tests/host'))
from test_resume import fixture
with tempfile.TemporaryDirectory() as tmp:
 root=pathlib.Path(os.environ.get('PN_COLD_OPEN_EVIDENCE',tmp));root.mkdir(parents=True,exist_ok=True)
 fixtures=root/'fixtures';fixtures.mkdir();fixture(fixtures)
 for kind in ('txt','epub'):
  books=root/(kind+'-books');books.mkdir();shutil.copyfile(fixtures/('book.'+kind),books/('book.'+kind))
  if kind=='epub':
   # 大资源仍属于完整原包摘要，即使只恢复一页也不可跳过。/ A large resource still belongs to the full source digest even when restoring just one page.
   with zipfile.ZipFile(books/'book.epub','a') as z:z.writestr('bulk-verification.dat',random.Random(20261026).randbytes(8*1024*1024),compress_type=zipfile.ZIP_STORED)
  def run(label,state,script):
   frames=root/(kind+'-'+label);frames.mkdir()
   env=dict(os.environ,SDL_VIDEODRIVER=os.environ.get('SDL_VIDEODRIVER','dummy'),PN_SIM_CLOCK='12:34',PN_SIM_LIBRARY_SCRIPT=script,PN_SIM_FRAME_DIR=str(frames),PN_READING_CACHE=str(state/'reading'),PN_SIM_PRESENT_DELAY_MS='200')
   result=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state)],env=env,capture_output=True,text=True,timeout=90)
   (frames/'stdout.log').write_text(result.stdout);(frames/'stderr.log').write_text(result.stderr)
   assert result.returncode==0 and 'used=0 live=0' in result.stdout,(kind,label,result.stdout,result.stderr[-2000:])
   return result,[p.read_bytes().split(b'\n',3)[3] for p in sorted(frames.glob('frame-*.pgm'))]
  seed=root/(kind+'-seed')
  result,frames=run('seed',seed,'library,enter,next,next,back,window-close');expected=frames[-2]
  assert len(list((seed/'reading'/'resume').glob('*.0.pnr')))==1 and not list((seed/'reading'/'resume').glob('*.1.pnr')),(kind,result.stdout,result.stderr)
  for label in ('hit','race','missing','corrupt','changed'):
   state=root/(kind+'-'+label+'-state');shutil.copytree(seed,state)
   if label=='missing':shutil.rmtree(state/'reading'/'resume')
   if label=='corrupt':
    for p in (state/'reading'/'resume').glob('*.pnr*'):p.write_bytes(b'broken')
   if label=='changed':
    source=books/('book.'+kind);original=source.read_bytes();source.write_bytes(original+b'changed after reboot')
   action='race-open-next:60,wait:300' if label=='race' else 'enter'
   result,frames=run(label,state,'library,tap:200:190,'+action+',window-close')
   hit='reading_snapshot inert=1 verified_source=0' in result.stdout
   if label in ('hit','race'):
    assert not hit,(kind,label,result.stdout,result.stderr)
    assert frames[-2]==expected,(kind,label,'body changed or queued NEXT escaped verification gate')
    assert result.stdout.count('library_open name=book.')==1,result.stdout
   elif label in ('missing','corrupt'):
    assert not hit and frames[-2]==expected,(kind,label,result.stdout)
   else:
    source.write_bytes(original)
    assert 'library_open name=book.' not in result.stdout,(kind,label,result.stdout)
  print(kind,'normal opening, in-flight NEXT dropped, missing/corrupt fallback and changed source rejection passed',flush=True)
