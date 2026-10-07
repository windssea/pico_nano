// 独立Node标准crypto对照任意分块SHA，不依赖浏览器WebCrypto。/ Independent Node crypto oracle for arbitrarily chunked SHA without browser WebCrypto.
import {SHA256} from '../../assets/transfer/sha256.mjs';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
for(const length of [0,1,3,55,56,63,64,65,65535,65536,65539,1048577]){const input=Uint8Array.from({length},(_,i)=>(i*37+1)&255),oracle=createHash('sha256').update(input).digest('hex');for(const split of [1,7,64,65536]){const hash=new SHA256();for(let at=0;at<input.length;at+=split)hash.update(input.subarray(at,at+split));assert.equal(hash.hex(),oracle);assert.throws(()=>hash.hex());}}
console.log('Incremental browser SHA: independent crypto vectors, block/padding boundaries, multi-MiB and arbitrary chunking passed');
