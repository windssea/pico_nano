// 后台逐块摘要，不将整本复制到内存。/ Background block hashing without whole-file memory copies.
import {SHA256} from './sha256.mjs';
self.onmessage=async event=>{try{const file=event.data,hash=new SHA256();for(let at=0;at<file.size;at+=65536){hash.update(new Uint8Array(await file.slice(at,at+65536).arrayBuffer()));self.postMessage({progress:Math.min(at+65536,file.size)/file.size});}self.postMessage({sha256:hash.hex()});}catch(error){self.postMessage({error:'文件读取或摘要计算失败'});}};
