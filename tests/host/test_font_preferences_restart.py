"""实际进程重启与同名替换后保留配置。 / Actual process restart and preserved settings after same-name replacement."""
import pathlib,tempfile,subprocess,sys,shutil,hashlib
with tempfile.TemporaryDirectory() as folder:
 root=pathlib.Path(folder);font=root/'字体.ttf';shutil.copyfile(sys.argv[2],font)
 def run(action,success):
  r=subprocess.run([sys.argv[1],action,str(root),str(font)],capture_output=True,text=True,timeout=20)
  assert (r.returncode==0)==success,(r.stdout,r.stderr)
  assert 'pool=0' in r.stdout,r.stdout
 run('write',True);run('read',True)
 snapshots={p.name:p.read_bytes() for p in root.glob('fonts.*')};original=font.read_bytes();changed=bytearray(original);changed[-1]^=1;font.write_bytes(changed)
 run('read',False);assert {p.name:p.read_bytes() for p in root.glob('fonts.*')}==snapshots
 font.write_bytes(original);run('read',True)
 font.unlink();run('read',False);assert {p.name:p.read_bytes() for p in root.glob('fonts.*')}==snapshots
print('Font preferences restart: actual source identity, same-name replacement/missing file rejected without rewriting settings')
