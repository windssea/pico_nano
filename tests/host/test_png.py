"""独立生成PNG及灰阶oracle，验证无整图分配。 / Independently generate PNG and grayscale oracle, checking bounded decoding."""
import binascii
import json
import os
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import zlib

binary=sys.argv[1]
def chunk(name,data):return struct.pack('>I',len(data))+name+data+struct.pack('>I',binascii.crc32(name+data))
def png(width,height,pixels):
    header=struct.pack('>IIBBBBB',width,height,8,6,0,0,0)
    rows=b''.join(b'\0'+bytes(c for p in pixels[y*width:(y+1)*width] for c in p) for y in range(height))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',header)+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b'')
def adam7(width,height,pixels):
    raw=b''
    for x0,y0,dx,dy in [(0,0,8,8),(4,0,8,8),(0,4,4,8),(2,0,4,4),(0,2,2,4),(1,0,2,2),(0,1,1,2)]:
        if x0>=width:continue
        for y in range(y0,height,dy):
            raw+=b'\0'+bytes(c for x in range(x0,width,dx) for c in pixels[y*width+x])
    header=struct.pack('>IIBBBBB',width,height,8,6,0,0,1)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',header)+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
with tempfile.TemporaryDirectory() as folder:
    path=pathlib.Path(folder)/'image.png'
    pixels=[((i*17)%256,(i*31)%256,(i*53)%256,(i*71)%256) for i in range(7*5)]
    path.write_bytes(png(7,5,pixels))
    probe=subprocess.run([binary,str(path),'--probe'],capture_output=True,timeout=10)
    assert probe.returncode==0 and json.loads(probe.stdout)=={'width':7,'height':5,'depth':8,'color_type':6,'interlace':0}
    def run(w=11,h=9,success=True,env=None):
        r=subprocess.run([binary,str(path),str(w),str(h)],capture_output=True,timeout=30,env=dict(os.environ,**(env or {})))
        assert (r.returncode==0)==success,r.stderr
        assert b'used=0 live=0' in r.stderr
        if not success:assert not r.stdout
        return r
    r=run();raw=r.stdout.split(b'\n',3)[3];expected=[]
    for y in range(9):
        for x in range(11):
            red,green,blue,alpha=pixels[(y*5//9)*7+x*7//11]
            gray=(red*77+green*150+blue*29+128)//256
            composite=(gray*alpha+255*(255-alpha)+127)//255
            expected.append(min(15,(composite+8)//17)*17)
    assert raw==bytes(expected)
    assert run(env={'PN_PNG_CHUNK':'1'}).stdout.split(b'\n',3)[3]==bytes(expected)
    stopped=run(success=False,env={'PN_PNG_CHUNK':'1','PN_PNG_STOP_AFTER':'50'});assert b'status=6' in stopped.stderr
    path.write_bytes(adam7(7,5,pixels));assert run().stdout.split(b'\n',3)[3]==bytes(expected)
    path.write_bytes(png(7,5,pixels))
    attempts=int(re.search(rb'attempts=(\d+)',r.stderr)[1])
    for fault in range(1,attempts+1):run(success=False,env={'PN_PNG_FAIL_AT':str(fault)})
    valid=path.read_bytes()
    for bad in [valid[:-2],valid+b'junk',valid[:40]+bytes([valid[40]^1])+valid[41:]]:
        path.write_bytes(bad);run(success=False)
    header=struct.pack('>IIBBBBB',7,1,8,3,0,0,0)
    palette=bytes([255,0,0,0,255,0]);data=b'\0'+bytes([0,1,0,1,0,1,0])
    indexed=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',header)+chunk(b'PLTE',palette)+chunk(b'tRNS',bytes([255,0]))+chunk(b'IDAT',zlib.compress(data))+chunk(b'IEND',b'')
    path.write_bytes(indexed);assert run(7,1).stdout.split(b'\n',3)[3]==bytes([85,255,85,255,85,255,85])
    gray16=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',3,1,16,0,0,0,0))+chunk(b'IDAT',zlib.compress(b'\0\0\0\x80\0\xff\xff'))+chunk(b'IEND',b'')
    path.write_bytes(gray16);assert run(3,1).stdout.split(b'\n',3)[3]==bytes([0,136,255])
    excess=valid[:-12]+b''.join(chunk(b'tEXt',b'key\0value') for _ in range(130))+valid[-12:]
    path.write_bytes(excess);run(success=False)
    large_header=struct.pack('>IIBBBBB',8192,8192,8,6,0,0,0)
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',large_header)+chunk(b'IEND',b''));run(success=False)
    path.write_bytes(png(2048,1024,[(40,80,120,255)]*(2048*1024)))
    run(32,16,env={'PN_PNG_BUDGET':'262144'})
    print('png: independent RGBA scaling/alpha oracle, CRC/tail and allocation faults passed')
