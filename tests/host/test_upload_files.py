"""真实文件、覆盖及每个正向同步点的进程中断恢复。 / Real files, replacement and process interruption at every positive sync point."""
import pathlib,subprocess,tempfile,sys,shutil,struct,zlib
exe=sys.argv[1]
expected=bytes(65+i%26 for i in range(65536+123))
with tempfile.TemporaryDirectory() as tmp:
 base=pathlib.Path(tmp)
 def root(name):
  p=base/name;p.mkdir();(p/'books').mkdir();(p/'books/book.txt').write_bytes(b'old book\n');return p
 def run(p,mode='new',crash=0,space='normal'):
  return subprocess.run([exe,str(p),mode,str(crash),space],capture_output=True,text=True,timeout=60)
 p=root('normal');r=run(p);assert r.returncode==0,(r.stdout,r.stderr);assert (p/'books/book.txt').read_bytes()==expected
 syncs=int(r.stdout.split('syncs=')[1]);assert syncs>8
 assert not list((p/'books').glob('.pn-backup-*'))
 for crash in range(1,syncs+1):
  p=root('cut-'+str(crash));r=run(p,crash=crash);assert r.returncode==77,(crash,r.stdout,r.stderr)
  if not (base/'corrupt-new').exists() and (p/'books/book.txt').exists() and (p/'books/book.txt').read_bytes()==expected and list((p/'books').glob('.pn-backup-*')):
   corrupt=base/'corrupt-new';shutil.copytree(p,corrupt);damaged=bytearray(expected);damaged[0]^=1;(corrupt/'books/book.txt').write_bytes(damaged)
   recovery=run(corrupt,'resume');assert recovery.returncode==1 and (corrupt/'books/book.txt').read_bytes()==b'old book\n',(recovery.stdout,recovery.stderr)
  r=run(p,'resume');assert r.returncode==0,(crash,r.stdout,r.stderr)
  assert (p/'books/book.txt').read_bytes()==expected
  assert not list((p/'books').glob('.pn-backup-*'))
 for mode in ('changed','target-link','dir-link','quota','reserved'):
  p=root(mode);external=base/(mode+'-outside');external.mkdir();(external/'book.txt').write_bytes(b'private outside\n')
  if mode=='changed':(p/'books/book.txt').write_bytes(b'changed book\n')
  if mode=='target-link':(p/'books/book.txt').unlink();(p/'books/book.txt').symlink_to(external/'book.txt')
  if mode=='dir-link':(p/'.readpico').symlink_to(external,target_is_directory=True)
  if mode=='quota':
   uploads=p/'.readpico/uploads';uploads.mkdir(parents=True)
   for i in range(10,18):(uploads/((bytes([i])+bytes(15)).hex()+'.part')).write_bytes(b'orphan')
  if mode=='reserved':
   uploads=p/'.readpico/uploads';uploads.mkdir(parents=True)
   for i in (10,11):
    identifier=bytes([i])+bytes(15);name=b'a.txt';payload=bytearray(144+len(name));payload[:4]=b'PNUP';struct.pack_into('<H',payload,4,1);payload[6]=1;struct.pack_into('<Q',payload,8,123);struct.pack_into('<Q',payload,16,512*1024*1024);payload[48:64]=identifier;struct.pack_into('<H',payload,128,len(name));payload[144:]=name
    frame=b'PNBL'+struct.pack('<HHIQ',1,0,len(payload),1)+payload;frame+=struct.pack('<I',zlib.crc32(frame));(uploads/(identifier.hex()+'.a')).write_bytes(frame)
  r=run(p);assert r.returncode==1,(mode,r.stdout,r.stderr)
  assert (external/'book.txt').read_bytes()==b'private outside\n'
 p=root('corrupt-cleanup');r=run(p,space='corrupt');assert r.returncode==1 and 'status=10' in r.stdout,(r.stdout,r.stderr)
 records=list((p/'.readpico/uploads').glob('07000000000000000000000000000000.[ab]'));latest=max(records,key=lambda p:struct.unpack_from('<Q',p.read_bytes(),12)[0]);assert latest.read_bytes()[20+7]==1
 p=root('warning');r=run(p,space='warn');assert r.returncode==0 and 'cleanup warning repaired' in r.stdout and (p/'books/book.txt').read_bytes()==expected,(r.stdout,r.stderr)
 p=root('low');r=run(p,space='low');assert r.returncode==0 and (p/'books/book.txt').read_bytes()==b'old book\n'
print('Real upload files: replacement preserved through '+str(syncs)+' process cuts, durable reopen, backup cleanup and insufficient-space refusal passed')
