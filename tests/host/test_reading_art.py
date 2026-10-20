"""独立核对原生16灰阶逐像素展开与原图SHA。 / Independently check native sixteen-gray expansion pixel by pixel and unchanged source SHA."""
import hashlib,pathlib,subprocess,sys,tempfile
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]/'tools'))
from build_brand_image import decode
from build_firmware_art import reading_bounds,resize
sources=sorted(pathlib.Path(sys.argv[2]).glob('*.png'));assert len(sources)==8
identities={p:hashlib.sha256(p.read_bytes()).digest() for p in sources}
with tempfile.TemporaryDirectory() as directory:
 subprocess.run([sys.argv[1],'--reading',directory],check=True,timeout=120)
 assert pathlib.Path(directory,'0.raw').read_bytes()==b'\xff'*(342*1216)
 for index,path in enumerate(sources,1):
  if index not in (4,7):
   assert pathlib.Path(directory,f'{index}.raw').read_bytes()==b'\xff'*(342*1216)
   continue
  gray,w,h,_=decode(path);assert (w,h)==(684,1216)
  bounds=(10,0,674,1216) if index==3 else ((48,0,636,1216) if index==4 else (0,0,684,1216))
  assert reading_bounds(path)==bounds,path
  pixels=resize(path,684,1216,True,bounds) if index in (3,4) else bytes(round(v*15/255) for v in gray)
  expected=bytes(pixels[i] | pixels[i+1]<<4 for i in range(0,len(pixels),2))
  assert pathlib.Path(directory,f'{index}.raw').read_bytes()==expected,path
assert identities=={p:hashlib.sha256(p.read_bytes()).digest() for p in sources}
print('reading art: retained IDs 4/7 match original quantized/cropped pixels, retired IDs are exact white, every original source SHA unchanged')
