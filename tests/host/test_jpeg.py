"""原生JPEG像素、源完整性和预算故障。 / Native JPEG pixels, source integrity and budget faults."""
import pathlib,json,subprocess,sys,os,re,tempfile
binary=sys.argv[1];root=pathlib.Path(sys.argv[2]);manifest=json.loads((root/'jpeg-samples.json').read_text())
def run(path,w=19,h=13,success=True,env=None,probe=False):
 r=subprocess.run([binary,str(path)]+(['--probe'] if probe else [str(w),str(h)]),capture_output=True,env=dict(os.environ,**(env or {})),timeout=20)
 if success is not None:assert (r.returncode==0)==success,r.stderr
 assert b'used=0 live=0' in r.stderr,r.stderr
 if r.returncode:assert not r.stdout
 return r
with tempfile.TemporaryDirectory() as folder:
 bad=pathlib.Path(folder)/'bad.jpg'
 for sample in manifest['samples']:
  path=root/sample['file'];r=run(path);pixels=r.stdout.split(b'\n',3)[3]
  expected=bytes(sample['gray4bpp'][(y*11//13)*17+x*17//19] for y in range(13) for x in range(19))
  assert pixels==expected
  assert json.loads(run(path,probe=True).stdout)=={k:sample[k] for k in ('width','height','progressive')}
  assert run(path,env={'PN_JPEG_CHUNK':'1'}).stdout==r.stdout
  attempts=int(re.search(rb'attempts=(\d+)',r.stderr)[1])
  for fault in range(1,attempts+1):
   failed=run(path,success=None,env={'PN_JPEG_FAIL_AT':str(fault)})
   if failed.returncode==0:assert failed.stdout==r.stdout

  run(path,success=False,env={'PN_JPEG_BUDGET':'4096'})
  r=run(path,success=False,env={'PN_JPEG_STOP_AFTER':'50','PN_JPEG_CHUNK':'17'});assert b'jpeg status=6 ' in r.stderr
  raw=path.read_bytes()
  for content in [raw[:-2],raw+b'X',raw[:80],b'not a JPEG']:
   bad.write_bytes(content);run(bad,success=False)
print('jpeg: baseline/progressive independent pixels, short reads, media failure, truncation/tail and all allocation faults passed')
