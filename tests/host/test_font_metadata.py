"""独立SFNT字节构造校验中文优先/UTF16/原始字重。 / Independently construct SFNT bytes to check Chinese preference, UTF-16 and raw weight."""
import struct,pathlib,tempfile,subprocess,sys,json
base=pathlib.Path(sys.argv[2]).read_bytes();n=struct.unpack_from('>H',base,4)[0];tables={}
for i in range(n):
 tag,checksum,at,size=struct.unpack_from('>4sIII',base,12+16*i);tables[tag]=base[at:at+size]
def checksum(data):
 data=data+b'\0'*((-len(data))%4);return sum(struct.unpack('>'+str(len(data)//4)+'I',data))&0xffffffff
def font(records):
 t=dict(tables);strings=b'';entries=b''
 for platform,encoding,language,name,value in records:
  entries+=struct.pack('>6H',platform,encoding,language,name,len(value),len(strings));strings+=value
 t[b'name']=struct.pack('>3H',0,len(records),6+len(entries))+entries+strings
 os2=bytearray(t[b'OS/2']);struct.pack_into('>H',os2,4,700);t[b'OS/2']=bytes(os2)
 head=bytearray(t[b'head']);head[8:12]=b'\0'*4;t[b'head']=bytes(head)
 count=len(t);power=2**(count.bit_length()-1);out=bytearray(struct.pack('>I4H',0x10000,count,power*16,power.bit_length()-1,count*16-power*16));at=12+16*count;data=b'';head_at=None
 for tag,body in sorted(t.items()):
  out+=struct.pack('>4sIII',tag,checksum(body),at,len(body));
  if tag==b'head':head_at=at
  padded=body+b'\0'*((-len(body))%4);data+=padded;at+=len(padded)
 out+=data;struct.pack_into('>I',out,head_at+8,(0xb1b0afba-checksum(out))&0xffffffff);return out
def u(value):return value.encode('utf-16-be')
english=[(3,1,0x409,1,u('Test Family')),(3,1,0x409,2,u('Regular'))]
variants=[(english+[(3,1,0x804,1,u('中文字体')),(3,1,0x804,16,u('中文字体🚀')),(3,1,0x804,17,u('常规'))],'中文字体🚀','常规'),(english+[(3,1,0x804,16,b'\xd8\x00')],'Test Family','Regular'),(english+[(3,1,0x804,16,u('字体'*100))],'Test Family','Regular'),(english+[(3,3,0x804,16,b'\xd6\xd0')],'Test Family','Regular')]
with tempfile.TemporaryDirectory() as folder:
 for i,(records,family,style) in enumerate(variants):
  path=pathlib.Path(folder)/f'{i}.ttf';path.write_bytes(font(records));r=subprocess.run([sys.argv[1],str(path)],capture_output=True,text=True,timeout=20)
  assert r.returncode==0,(r.stdout,r.stderr)
  metadata=json.loads(r.stdout);assert metadata['family']==family and metadata['style']==style and metadata['weight']==700 and metadata['glyphs']==4 and not metadata['variable'],metadata
print('Font metadata: independent multilingual UTF-16/surrogate/long-name/legacy encoding and weight checks passed')
