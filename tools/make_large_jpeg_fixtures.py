"""生成原创CC0大JPEG测试图，需Pillow 12.3.0。 / Generate original CC0 large JPEG fixtures using Pillow 12.3.0.

仅维护fixture时运行；普通测试无需Pillow。 / Run only for fixture maintenance; ordinary tests do not need Pillow.
"""
import hashlib,json,pathlib
from PIL import Image,ImageDraw,features

def main():
    root=pathlib.Path(__file__).resolve().parents[1]/'tests'/'fixtures';samples=[]
    for name,width,height,progressive in [('jpeg-large-baseline.jpg',1920,1080,False),('jpeg-large-progressive.jpg',1092,1533,True)]:
        image=Image.new('RGB',(width,height),(80,120,160))
        ImageDraw.Draw(image).rectangle((0,height//2,width-1,height-1),fill=(200,160,120))
        path=root/name;image.save(path,'JPEG',quality=95,subsampling=2,progressive=progressive)
        samples.append(dict(file=name,width=width,height=height,progressive=progressive,sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    manifest=dict(generator='Pillow 12.3.0; two original constant-color bands, quality=95, subsampling=2',jpeg_codec=features.version_codec('jpg'),license='CC0-1.0',samples=samples)
    (root/'jpeg-large-samples.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
