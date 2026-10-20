"""验证灰阶构建输入与PNG过滤/半字节约定。 / Verify grayscale inputs and PNG filter/nibble conventions."""
import pathlib
import struct
import sys
import tempfile
import unittest
import zlib
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]/'tools'))
from build_firmware_art import resize,packed,png4,native_art,packbits,reading_bounds


class FirmwareArt(unittest.TestCase):
    def test_native_nibbles_and_gray_source(self):
        def chunk(tag,data):
            return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
        data=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',4,1,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(b'\0\0\x55\xaa\xff'))+chunk(b'IEND',b'')
        with tempfile.TemporaryDirectory() as directory:
            p=pathlib.Path(directory)/'gray.png';p.write_bytes(data)
            values=resize(p,4,1,False)
            self.assertEqual(values,bytes([0,5,10,15]));self.assertEqual(p.read_bytes(),data)
            self.assertEqual(packed(values,4,1),b'\x50\xfa');self.assertEqual(packed(values,4,1,True),b'\x05\xaf')

    def test_reading_edge_trim_keeps_central_whitespace(self):
        with tempfile.TemporaryDirectory() as directory:
            path=pathlib.Path(directory)/'margin.png'
            # 只裁连续的整列白边；中间白洞不能被压掉。 / Trim only contiguous white edge columns; retain the white center.
            def source(values):
                def chunk(tag,data):return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
                rows=b''.join(b'\0'+bytes(v*17 for v in values[y*10:(y+1)*10]) for y in range(8))
                return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',10,8,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b'')
            pixels=bytes(15 if x<2 or x>=8 or 4<=x<6 else 10 for y in range(8) for x in range(10))
            path.write_bytes(source(pixels));self.assertEqual(reading_bounds(path),(2,0,6,8))
            self.assertEqual(resize(path,6,8,False,(2,0,6,8)),bytes(pixels[y*10+x] for y in range(8) for x in range(2,8)))
            path.write_bytes(source(bytes([15])*80))
            with self.assertRaises(ValueError):reading_bounds(path)

    def test_native_background_roundtrip(self):
        for values in (bytes([15])*64*48,bytes((x//9+y//7)%16 for y in range(48) for x in range(64))):
            encoded=native_art(values,64,48);out=bytearray();at=1
            while at<len(encoded):
                control=encoded[at];at+=1
                if control<128:out.extend(encoded[at:at+control+1]);at+=control+1
                else:out.extend(bytes([encoded[at]])*(257-control));at+=1
            if encoded[0]:
                for i in range(32,len(out)):out[i]^=out[i-32]
            self.assertEqual(bytes(out),packed(values,64,48))
        self.assertEqual(packbits(bytes([7])*128),bytes([129,7]))

    def test_png_filters_reconstruct_exact_pixels(self):
        values=bytes((x*3+y*5)%16 for y in range(8) for x in range(16));data=png4(values,16,8)
        at=8;compressed=b''
        while at<len(data):
            size=struct.unpack_from('>I',data,at)[0];tag=data[at+4:at+8];body=data[at+8:at+8+size]
            self.assertEqual(struct.unpack_from('>I',data,at+8+size)[0],zlib.crc32(tag+body)&0xffffffff)
            if tag==b'IHDR':self.assertEqual(struct.unpack('>IIBBBBB',body),(16,8,4,0,0,0,0))
            if tag==b'IDAT':compressed+=body
            at+=size+12
        raw=zlib.decompress(compressed);rows=[];previous=bytes(8)
        for y in range(8):
            method=raw[y*9];row=bytearray(raw[y*9+1:y*9+9])
            for x in range(8):
                a=row[x-1] if x else 0;b=previous[x];c=previous[x-1] if x else 0
                predictors=[0,a,b,(a+b)//2,min((a,b,c),key=lambda v:abs(a+b-c-v))]
                row[x]=(row[x]+predictors[method])&255
            rows.append(bytes(row));previous=row
        self.assertEqual(b''.join(rows),packed(values,16,8,True))


if __name__=='__main__':unittest.main()
