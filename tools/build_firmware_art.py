"""预缩放固件插画，不修改原件。 / Prescale firmware artwork without modifying originals."""
import argparse
import hashlib
import json
import pathlib
import struct
import zlib
from build_brand_image import decode, weights


def resize(path, width, height, crop, bounds=None):
    """面积平均、16灰阶；封面居中裁切。 / Area averaging and 16 grays; center-crop covers."""
    gray, sw, sh, _ = decode(path)
    bx,by,bw,bh=bounds if bounds is not None else (0,0,sw,sh)
    if bx<0 or by<0 or bw<1 or bh<1 or bx+bw>sw or by+bh>sh:raise ValueError("Invalid artwork bounds")
    cw, ch = bw, bh
    if crop:
        if bw * height > bh * width:
            cw = bh * width // height
        else:
            ch = bw * height // width
    x0, y0 = bx+(bw-cw)//2, by+(bh-ch)//2
    horizontal, vertical = weights(cw, width), weights(ch, height)
    rows = [[sum(gray[(y+y0)*sw+x0+x]*weight for x, weight in cell)
             for cell in horizontal] for y in range(ch)]
    return bytes(max(0, min(15, round(sum(rows[j][x]*weight for j, weight in cell)*15/255)))
                 for cell in vertical for x in range(width))


def packed(values, width, height, png=False):
    """PNG高半字节在左，画布低半字节在左。 / PNG has the left pixel high; frames have it low."""
    return bytes((values[i]<<4 | values[i+1]) if png else (values[i] | values[i+1]<<4)
                 for i in range(0, width*height, 2))


def packbits(data):
    """有界重复/字面量编码，运行时无需分配。 / Bounded run/literal encoding with allocation-free decoding."""
    out=bytearray();i=0
    while i<len(data):
        run=1
        while i+run<len(data) and run<128 and data[i+run]==data[i]:run+=1
        if run>=2:
            out.extend((257-run,data[i]));i+=run;continue
        count=1
        while i+count<len(data) and count<128 and not (i+count+1<len(data) and data[i+count]==data[i+count+1]):count+=1
        out.append(count-1);out.extend(data[i:i+count]);i+=count
    return bytes(out)


def native_art(values, width, height):
    """逐行异或后压缩，保留原量化像素。 / Compress row XOR differences, preserving quantized pixels."""
    raw=packed(values,width,height);stride=width//2
    delta=bytes(v^(raw[i-stride] if i>=stride else 0) for i,v in enumerate(raw))
    choices=[(packbits(raw),0),(packbits(delta),1)]
    data,method=min(choices,key=lambda item:len(item[0]))
    return bytes([method])+data


def png4(values, width, height):
    """逐行自适应过滤后无损压缩。 / Losslessly compress after adaptive per-row filtering."""
    raw=packed(values,width,height,True);stride=width//2;previous=bytes(stride);rows=[]
    for y in range(height):
        row=raw[y*stride:(y+1)*stride];candidates=[]
        for method in range(5):
            filtered=bytearray(stride)
            for i,value in enumerate(row):
                left=row[i-1] if i else 0;above=previous[i];corner=previous[i-1] if i else 0
                p=left+above-corner;ds=(abs(p-left),abs(p-above),abs(p-corner))
                predictor=(0,left,above,(left+above)//2,(left,above,corner)[ds.index(min(ds))])[method]
                filtered[i]=(value-predictor)&255
            candidates.append((sum(min(v,256-v) for v in filtered),bytes([method])+filtered))
        rows.append(min(candidates,key=lambda c:c[0])[1]);previous=row
    def chunk(tag,data):
        return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,4,0,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows),9))+chunk(b'IEND',b'')


def reading_bounds(path):
    """仅裁掉两侧整列近白边，不动中间留白。 / Trim only all-near-white edge columns, preserving central whitespace."""
    _,w,h,box=decode(path)
    return (box[0],0,box[2]-box[0],h)


def reading_art(path):
    """原生16灰阶无损压缩，不降采样或钳制浅灰。 / Losslessly pack native sixteen-gray pixels without downsampling or light-gray clamping."""
    values=resize(path,684,1216,True,reading_bounds(path))
    return values,native_art(values,684,1216)


def generate(boot, covers, background, output, report):
    """只将预处理数据写到构建目录。 / Write preprocessed data into the build directory only."""
    boot_pixels=resize(boot,684,1216,False)
    boot_art=native_art(boot_pixels,684,1216)
    background_pixels=resize(background,684,1216,False)
    background_art=native_art(background_pixels,684,1216)
    sources=sorted(covers.glob('*.png'))
    if len(sources)!=8:
        raise ValueError('Expected the eight supplied cover images')
    # 新原图均为长图：按宽适配，只上下居中裁切。 / New portrait originals fit the width and crop vertically only.
    for path in sources:
        _,sw,sh,_=decode(path)
        if sw*100>sh*72:raise ValueError(f'Cover too wide for vertical-only cropping: {path}')
    reading_sources=sorted((boot.parent/'bg').glob('*.png'))
    if len(reading_sources)!=8 or any(not p.name.startswith(f'{i:02d}_') for i,p in enumerate(reading_sources,1)):
        raise ValueError('Expected reading backgrounds 01 through 08')
    reading=[reading_art(p) for p in reading_sources];reading_offsets=[0]
    for _,data in reading:reading_offsets.append(reading_offsets[-1]+len(data))
    reading_data=b''.join(data for _,data in reading)
    cover_pixels=b''.join(packed(resize(p,72,100,True),72,100) for p in sources)
    lines=['/* Generated by tools/build_firmware_art.py; do not edit. */','#include <stddef.h>','#include <stdint.h>']
    for name,data in [('pn_boot_art_packed',boot_art),('pn_bookmark_art_packed',background_art),('pn_default_cover_pixels',cover_pixels),('pn_reading_art_packed',reading_data)]:
        lines.append(f'const uint8_t {name}[]={{')
        lines.extend(','.join(map(str,data[i:i+32]))+',' for i in range(0,len(data),32))
        lines.extend(['};',f'const size_t {name}_size={len(data)};'])
    lines.append('const uint32_t pn_reading_art_offsets[]={'+','.join(map(str,reading_offsets))+'};')
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    # 导出可检查的设备色阶缓存，原图只读。 / Export inspectable device-gray caches; originals remain read-only.
    cache=output.parent/'art-cache';cache.mkdir(parents=True,exist_ok=True)
    (cache/'boot.png').write_bytes(png4(boot_pixels,684,1216))
    (cache/'bookmark.png').write_bytes(png4(background_pixels,684,1216))
    for path in sources:(cache/path.name).write_bytes(png4(resize(path,72,100,True),72,100))
    for path,(values,_) in zip(reading_sources,reading):(cache/('reading-'+path.name)).write_bytes(png4(values,684,1216))
    metadata={'reading_source_bounds':[reading_bounds(p) for p in reading_sources],'reading_packed_bytes':len(reading_data),'reading_each_bytes':[len(data) for _,data in reading],'reading_size':[684,1216],'reading_gray_levels':list(range(16)),
              'boot_packed_bytes':len(boot_art),'bookmark_packed_bytes':len(background_art),'cover_bytes':len(cover_pixels),'cover_count':len(sources),'cover_size':[72,100],
              'gray_levels':16,'cover_runtime_decode':False,'boot_size':[684,1216],
              'source_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [boot,background,*sources,*reading_sources]}}
    report.write_text(json.dumps(metadata,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(metadata,ensure_ascii=False))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('boot','covers','background','output','report'):parser.add_argument(name,type=pathlib.Path)
    args=parser.parse_args();generate(args.boot,args.covers,args.background,args.output,args.report)
