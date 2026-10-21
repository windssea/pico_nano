// 中文：真实网页脚本的异步类别/授权与删除确认边界。/ English: async category/auth and delete-confirmation boundaries in the actual page script.
import {readFileSync,mkdirSync,writeFileSync} from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import test from 'node:test';
import {File} from 'node:buffer';
import {inflateSync,deflateSync} from 'node:zlib';

function harness(fetch,bootstrap=false,extras={}){
  const elements=new Map();
  class Element {
    constructor(){this.children=[];this.dataset={};this.classList={toggle(){},add(){},remove(){}};this.style={};this.value='';}
    setAttribute(){} addEventListener(){} append(...items){this.children.push(...items);} replaceChildren(...items){this.children=[...items];}
    close(){} showModal(){} click(){}
  }
  const element=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id);};
  const tabs=['book','font','cover','wallpaper','background'].map(kind=>{const e=new Element();e.dataset.kind=kind;return e;});
  const context=vm.createContext({document:{getElementById:element,createElement:()=>new Element(),querySelectorAll:()=>tabs},fetch,URLSearchParams,Uint8Array,TextEncoder,Blob,File,Response,CompressionStream,location:{hash:'',pathname:'/',search:''},history:{replaceState(){}},sessionStorage:{getItem(){return null;},setItem(){}},setTimeout,clearTimeout,AbortController,...extras});
  const source=readFileSync(new URL('../../assets/transfer/app.js',import.meta.url),'utf8').replace(/^import .*;\r?$/m,'');
  vm.runInContext((bootstrap?source:source.slice(0,source.indexOf('// 打开页面'))+'render();')+'\nglobalThis.qa={token:value=>{token=value;},auth:()=>token,api,remove:removeFont,prepare:prepareImage,png:imagePng,add,entries:()=>entries};',context);
  return {context,element,choose:kind=>tabs.find(t=>t.dataset.kind===kind).onclick()};
}
const reply=(value,status=200)=>({ok:status<400,status,json:async()=>value});
const settle=()=>new Promise(resolve=>setImmediate(resolve));

test('late font list cannot repaint the wallpaper category',async()=>{
  let resolveFont;const h=harness(url=>url.endsWith('/fonts')?new Promise(r=>{resolveFont=r;}):Promise.resolve(reply({items:[{name:'透明.png',size:32}],more:false})));
  h.context.qa.token('current');h.choose('font');h.choose('wallpaper');await settle();
  resolveFont(reply({items:[{name:'stale.ttf',size:12}],more:false}));await settle();
  assert.equal(h.element('resources-title').textContent,'已上传锁屏图片');
  assert.deepEqual(h.element('fonts-list').children.map(row=>row.children[0].textContent),['透明.png']);
});

test('late old authorization failure cannot clear a new session',async()=>{
  let finish;const h=harness(()=>new Promise(r=>{finish=r;}));
  h.context.qa.token('old');h.choose('font');h.context.qa.token('new');finish(reply({message:'expired'},401));await settle();
  assert.equal(h.context.qa.auth(),'new');
});

test('delete consent remains bound to the selected resource, cancellation sends nothing',async()=>{
  const calls=[];const h=harness((url,options)=>{calls.push({url,method:options.method});return Promise.resolve(reply(url.includes('/files/')?{size:32,sha256:'a'.repeat(64)}:{deleted:true,items:[],more:false}));});
  h.context.qa.token('current');
  const cancelled=h.context.qa.remove({name:'keep.png'},'wallpaper');h.element('delete-no').onclick();await cancelled;assert.equal(calls.length,0);
  const deletion=h.context.qa.remove({name:'cover.png'},'cover');h.element('delete-yes').onclick();await deletion;
  assert.equal(calls[0].url,'/api/v1/files/cover?name=cover.png');
  assert.match(calls[1].url,/^\/api\/v1\/covers\?name=cover.png&size=32&sha256=/);assert.equal(calls[1].method,'DELETE');
});

test('page establishes a session without collecting a pairing code',async()=>{
  const calls=[];const h=harness((url,options)=>{calls.push({url,options});return Promise.resolve(reply(url.endsWith('/session')?{ready:true,token:'automatic'}:{message:'not supported'},url.endsWith('/session')?200:404));},true);
  await settle();assert.equal(h.context.qa.auth(),'automatic');assert.equal(h.element('connection').textContent,'已连接');
  assert.equal(calls[0].url,'/api/v1/session');assert.ok(calls.every(c=>!c.url.endsWith('/pair')));
});

test('idempotent chunk retries retain offset and bytes; creation is never replayed',async()=>{
 let puts=0,posts=0;const bodies=[];const h=harness((url,options)=>{if(options.method==='PUT'){puts++;bodies.push(options);return Promise.resolve(reply(puts===1?{message:'transient I/O'}:{next_offset:3},puts===1?503:200));}posts++;return Promise.reject(Error('network lost'));});
 h.context.qa.token('current');const bytes=new Uint8Array([1,2,3]);
 const result=await h.context.qa.api('/uploads/id/chunks','PUT',bytes,{'X-Offset':'0'});
 assert.equal(result.next_offset,3);assert.equal(puts,2);assert.equal(bodies[0].body,bodies[1].body);assert.equal(bodies[1].headers['X-Offset'],'0');
 await assert.rejects(h.context.qa.api('/uploads','POST',{}),/network lost/);assert.equal(posts,1);
});

// 独立PNG容器/CRC/滤波/像素检查，不把编码器输出自身作为预期值。 / Independently check PNG chunks, CRC, filters and pixels instead of trusting encoder output.
async function decode(blob){
 const bytes=Buffer.from(await blob.arrayBuffer());assert.equal(bytes.subarray(0,8).toString('hex'),'89504e470d0a1a0a');const parts=[];let header,palette,alpha;
 for(let at=8;at<bytes.length;){const n=bytes.readUInt32BE(at),tag=bytes.toString('ascii',at+4,at+8),data=bytes.subarray(at+8,at+8+n);let crc=0xffffffff;for(const b of bytes.subarray(at+4,at+8+n)){crc^=b;for(let k=0;k<8;k++)crc=crc&1?(crc>>>1)^0xedb88320:crc>>>1;}assert.equal((crc^0xffffffff)>>>0,bytes.readUInt32BE(at+8+n));
  if(tag==='IHDR')header=data;if(tag==='PLTE')palette=data;if(tag==='tRNS')alpha=data;if(tag==='IDAT')parts.push(data);at+=n+12;if(tag==='IEND'){assert.equal(at,bytes.length);break;}}
 assert.ok(header[9]===0 || header[9]===3);assert.equal(header[12],0);const width=header.readUInt32BE(0),height=header.readUInt32BE(4),depth=header[8],stride=Math.ceil(width*depth/8),raw=inflateSync(Buffer.concat(parts)),pixels=[];assert.equal(raw.length,(stride+1)*height);let previous=Buffer.alloc(stride);
 for(let y=0;y<height;y++){const at=y*(stride+1),filter=raw[at],row=Buffer.alloc(stride);assert.ok(filter<=4);for(let x=0;x<stride;x++){const a=x?row[x-1]:0,b=previous[x],c=x?previous[x-1]:0,p=a+b-c,da=Math.abs(p-a),db=Math.abs(p-b),dc=Math.abs(p-c),predict=filter===0?0:filter===1?a:filter===2?b:filter===3?Math.floor((a+b)/2):da<=db&&da<=dc?a:db<=dc?b:c;row[x]=(raw[at+1+x]+predict)&255;}
  for(let x=0;x<width;x++){const i=depth===8?row[x]:(row[x>>>1]>>>(x&1?0:4))&15;if(palette){assert.equal(palette[i*3],palette[i*3+1]);assert.equal(palette[i*3],palette[i*3+2]);}pixels.push([palette?palette[i*3]:i*17,alpha?alpha[i]:255]);}previous=row;}
 return {width,height,depth,pixels,bytes};
}
function fixture(name,png){if(!process.env.PN_TRANSFER_IMAGE_FIXTURES)return;const folder=process.env.PN_TRANSFER_IMAGE_FIXTURES;mkdirSync(folder,{recursive:true});writeFileSync(folder+'/'+name+'.png',png.bytes);writeFileSync(folder+'/'+name+'.json',JSON.stringify({width:png.width,height:png.height,pixels:png.pixels}));}

test('opaque images become true 4-bit 16-gray PNG; compressed and fallback zlib agree',async()=>{
 const rgba=Uint8Array.from({length:17*7*4},(_,i)=>i%4===3?255:(i*37+19)&255),h=harness(()=>{}),fallback=harness(()=>{},false,{CompressionStream:undefined});
 const a=await decode(await h.context.qa.png(rgba,17,7,true)),b=await decode(await fallback.context.qa.png(rgba,17,7,true));assert.equal(a.depth,4);assert.deepEqual(a.pixels,b.pixels);
 fixture('opaque',a);fixture('opaque-fallback',b);
 for(let i=0;i<a.pixels.length;i++){const at=i*4,luma=(rgba[at]*77+rgba[at+1]*150+rgba[at+2]*29+128)>>>8;assert.deepEqual(a.pixels[i],[Math.floor((luma+8)/17)*17,255]);}
});

test('mask palette preserves all 16 alpha levels and grayscale; covers composite white',async()=>{
 const rgba=Uint8Array.from({length:16*4},(_,i)=>i%4===3?Math.floor(i/4)*17:i%4===0?255:0),h=harness(()=>{});
 const mask=await decode(await h.context.qa.png(rgba,16,1,true)),cover=await decode(await h.context.qa.png(rgba,16,1,false));assert.equal(mask.depth,8);assert.equal(cover.depth,4);
 fixture('mask',mask);fixture('white-cover',cover);
 for(let i=0;i<16;i++){assert.deepEqual(mask.pixels[i],[i?85:0,i*17]);const gray=Math.floor((77*i*17+255*(255-i*17)+127)/255);assert.deepEqual(cover.pixels[i],[Math.floor((gray+8)/17)*17,255]);}
});

test('fallback zlib spans multiple blocks and bounds a native wallpaper without original bytes',async()=>{
 const h=harness(()=>{},false,{CompressionStream:undefined}),rgba=new Uint8Array(684*1216*4);for(let i=0;i<rgba.length;i+=4)rgba.set([0,0,0,255],i);
 const png=Buffer.from(await (await h.context.qa.png(rgba,684,1216,true)).arrayBuffer());let idat;for(let at=8;at<png.length;){const n=png.readUInt32BE(at);if(png.toString('ascii',at+4,at+8)==='IDAT')idat=png.subarray(at+8,at+8+n);at+=n+12;}
 const raw=inflateSync(idat);assert.equal(raw.length,343*1216);assert.ok(png.length<420000);
});

function imageHarness(width,height,extras={}){
 let closed=0;const draws=[],canvases=[],h=harness(()=>Promise.resolve(reply({items:[],more:false})),false,{createImageBitmap:async()=>({width,height,close(){closed++;}}),...extras});
 const create=h.context.document.createElement;h.context.document.createElement=tag=>{if(tag!=='canvas')return create(tag);const canvas={width:0,height:0,getContext(){return {fillRect(){},drawImage(...args){draws.push(args.slice(1));},getImageData(){const data=new Uint8Array(canvas.width*canvas.height*4);for(let i=0;i<data.length;i+=4)data.set([255,255,255,255],i);return {data};}};}};canvases.push(canvas);return canvas;};
 return {h,draws,canvases,closed:()=>closed};
}

test('cover always re-encodes and crops to its cache size; name and payload both become PNG',async()=>{
 const {h,draws,closed}=imageHarness(2000,4000),file=new File(['original'],'书名.cover.JPEG'),result=await h.context.qa.prepare(file,file.name,'cover'),png=await decode(result.file);
 assert.notEqual(result.file,file);assert.equal(result.name,'书名.cover.png');assert.equal(result.file.name,result.name);assert.equal(result.file.type,'image/png');assert.equal(png.width,184);assert.equal(png.height,256);assert.equal(png.depth,4);assert.equal(closed(),1);assert.ok(draws[0][1]>0);assert.ok(result.file.size<1024);
});

test('wallpapers respect both panel orientations without cropping or upscaling',async()=>{
 for(const [w,h,ew,eh] of [[2736,4864,684,1216],[4864,2736,1216,684],[100,200,100,200]]){const item=imageHarness(w,h),file=new File(['original'],'壁纸.png'),result=await item.h.context.qa.prepare(file,file.name,'wallpaper'),png=await decode(result.file);assert.equal(png.width,ew);assert.equal(png.height,eh);assert.equal(item.draws[0].length,4);assert.equal(item.closed(),1);assert.notEqual(result.file,file);}
});

test('unavailable decoder, bad image or failed encoder never queue originals, valid next images still work',async()=>{
 const file=new File(['original'],'cover.jpg'),h=harness(()=>Promise.resolve(reply({items:[],more:false})));
 await assert.rejects(h.context.qa.prepare(file,file.name,'cover'),/浏览器不支持/);
 const bad=imageHarness(0,0),broken=imageHarness(20,30,{createImageBitmap:async()=>{throw Error('decode');}});
 await assert.rejects(bad.h.context.qa.prepare(file,file.name,'cover'),/无效/);assert.equal(bad.closed(),1);await assert.rejects(broken.h.context.qa.prepare(file,file.name,'cover'),/无法解码/);
 h.choose('cover');await h.context.qa.add([file]);assert.equal(h.context.qa.entries().length,0);assert.match(h.element('notice').textContent,/未上传原图/);
 const good=imageHarness(184,256);good.h.choose('cover');await good.h.context.qa.add([file]);assert.equal(good.h.context.qa.entries().length,1);const entry=good.h.context.qa.entries()[0];assert.equal(entry.name,'cover.png');assert.equal(entry.kind,'cover');assert.notEqual(entry.file,file);
 const failed=imageHarness(184,256,{CompressionStream:class{constructor(){throw Error('encoder unavailable');}}});failed.h.choose('cover');await failed.h.context.qa.add([file]);assert.equal(failed.h.context.qa.entries().length,0);assert.equal(failed.closed(),1);
});

test('asynchronous image batches survive input clearing and category changes',async()=>{
 const item=imageHarness(184,256),h=item.h,files=[new File(['one'],'one.jpg'),new File(['two'],'two.png')];h.choose('cover');const pending=h.context.qa.add(files);files.length=0;h.choose('wallpaper');await pending;
 assert.deepEqual(Array.from(h.context.qa.entries(),e=>[e.name,e.kind]),[['one.png','cover'],['two.png','cover']]);assert.equal(item.closed(),2);
});

test('reading backgrounds always fill the portrait panel and use opaque native 4-bit grayscale',async()=>{
 for(const [w,h] of [[3000,2000],[2000,4000],[100,200]]){const item=imageHarness(w,h),file=new File(['original'],'纸张.jpeg'),result=await item.h.context.qa.prepare(file,file.name,'background'),png=await decode(result.file);assert.equal(png.width,684);assert.equal(png.height,1216);assert.equal(png.depth,4);assert.equal(item.draws[0].length,8);assert.equal(png.bytes[25],0);assert.notEqual(result.file,file);assert.ok(png.pixels.every(p=>p[1]===255));}
 const item=imageHarness(100,200);item.h.choose('background');await item.h.context.qa.add([new File(['original'],'paper.jpg')]);assert.equal(item.h.context.qa.entries()[0].kind,'background');
});

test('upload previews reference normalized bytes and release their object URL when cleared',async()=>{
 const seen=[],item=imageHarness(80,140,{URL:{createObjectURL(file){seen.push(file);return 'blob:normalized';},revokeObjectURL(url){seen.push(url);}}}),h=item.h;h.choose('cover');await h.context.qa.add([new File(['original'],'cover.jpg')]);const entry=h.context.qa.entries()[0];assert.equal(seen[0],entry.file);assert.equal(entry.previewURL,'blob:normalized');entry.cancelled=true;h.element('clear').onclick();assert.equal(h.context.qa.entries().length,0);assert.equal(seen[1],'blob:normalized');
});

test('lossless predictor expansion cannot exceed the frozen three-predictor compressed size',async()=>{
 const w=184,h=256,rgba=new Uint8Array(w*h*4);let seed=329841;
 for(let y=0;y<h;y++)for(let x=0;x<w;x++){seed=(Math.imul(seed,1664525)+1013904223)>>>0;const shade=((seed>>>28)+(Math.floor(x/23)^Math.floor(y/19)))&15;rgba.set([shade*17,shade*17,shade*17,255],4*(y*w+x));}
 const item=harness(()=>{}),png=await decode(await item.context.qa.png(rgba,w,h,false)),stride=w/2,legacy=Buffer.alloc((stride+1)*h);let previous=Buffer.alloc(stride);
 for(let y=0;y<h;y++){const row=Buffer.alloc(stride);for(let x=0;x<w;x++)row[x>>1]|=(rgba[4*(y*w+x)]/17)<<(x&1?0:4);let best;
  for(let f=0;f<3;f++){const residual=Buffer.from(row,(v)=>v);for(let x=0;x<stride;x++)residual[x]=(row[x]-(f===1?(x?row[x-1]:0):f===2?previous[x]:0))&255;const score=residual.reduce((sum,v)=>sum+Math.min(v,256-v),0);if(!best||score<best.score)best={f,score,residual};}
  legacy[y*(stride+1)]=best.f;best.residual.copy(legacy,y*(stride+1)+1);previous=row;}
 let compressed=0;for(let at=8;at<png.bytes.length;){const n=png.bytes.readUInt32BE(at);if(png.bytes.toString('ascii',at+4,at+8)==='IDAT')compressed+=n;at+=n+12;}
 assert.ok(compressed<=deflateSync(legacy).length);
 const long=imageHarness(100,200);await assert.rejects(long.h.context.qa.prepare(new File(['original'],'汉'.repeat(60)+'.jpg'),'汉'.repeat(60)+'.jpg','background'),/文件名过长/);assert.equal(long.closed(),1);
});
