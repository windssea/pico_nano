// 中文传输队列，原文只读；名称使用textContent。/ Chinese transfer queue, read-only originals and textContent names.
import {SHA256} from './sha256.mjs';
const $=id=>document.getElementById(id),kinds={book:{label:'书籍',accept:'.txt,.epub',hint:'TXT、EPUB 电子书，支持多本添加',limit:512*1024*1024},font:{label:'字体',accept:'.ttf,.otf',hint:'TTF、OTF 字体，最大 32 MiB',limit:32*1024*1024},cover:{label:'封面',accept:'.png,.jpg,.jpeg',hint:'PNG/JPEG → 184×256、16灰阶 PNG；居中裁切，与书籍同名关联',limit:8*1024*1024},banner:{label:'首页横幅',accept:'.png,.jpg,.jpeg',hint:'PNG/JPEG → 684×146、16灰阶 PNG；居中裁切，上传后在设置→书架与主页选择',limit:8*1024*1024},wallpaper:{label:'锁屏壁纸',accept:'.png,.jpg,.jpeg',hint:'PNG/JPEG → 原生尺寸内的16灰阶 PNG；上传后在锁屏壁纸→壁纸预览并应用',limit:8*1024*1024},mask:{label:'阅读遮罩',accept:'.png,.jpg,.jpeg',hint:'PNG/JPEG → 原生尺寸内的16灰阶 PNG，保留16级透明度；上传后在锁屏壁纸→遮罩选择',limit:8*1024*1024}};
kinds.background={label:'阅读背景',accept:'.png,.jpg,.jpeg',hint:'PNG/JPEG → 684×1216、16灰阶 PNG；铺满居中裁切，上传后在排版设置选择',limit:8*1024*1024};
kinds.dict={label:'词典',accept:'.zip,.ifo,.idx,.dict,.dz,.gz',hint:'支持直接拖入 StarDict .zip 词典包，或选择同名 .ifo/.idx/.dict(.dz)；明文 .dict 自动压缩为 DictZip',limit:256*1024*1024};
let kind='book',token=null,running=false,entries=[],resourceGeneration=0,installedDictItems=[],installedByKind={};
const ONLINE_SOURCES=[
 {id:'jsdelivr',label:'国内 CDN 镜像',base:'https://cdn.jsdelivr.net/gh/windssea/pico_nano@main/'},
 {id:'pages',label:'官方站点',base:'https://windssea.github.io/pico_nano/'},
 {id:'raw',label:'GitHub Raw',base:'https://raw.githubusercontent.com/windssea/pico_nano/main/'},
];
const onlineState={catalog:null,source:null,catId:'',checker:false,loading:false,downloading:new Set(),preview:null,fontsLoaded:new Set()};
function notice(text,error=false){$('notice').textContent=text;$('notice').classList.toggle('error',error);}
function connected(value){$('connection').textContent=value?'已连接':'正在连接';$('connection').classList.toggle('connected',value);if(!value)$('network-panel').hidden=true;else loadNetwork();syncFonts();render();}
// 家庭网络：只在设备支持时显示；口令只提交，不回显。/ Home network: shown only when the device supports it; passwords are submitted, never echoed.
function showNetwork(state){$('network-panel').hidden=false;$('network-forget').hidden=!state.saved;$('network-status').textContent=state.pending?'正在保存到阅读器…':state.result==='failed'?'保存失败，请重试；原设置保留。':state.saved?'已保存：'+state.ssid+'。退出传书后可在菜单选择“局域网传书”。':'保存后，阅读器菜单会出现“局域网传书”，手机与阅读器连同一个 WiFi 即可传书。';}
async function loadNetwork(){try{showNetwork(await api('/network'));}catch(error){if(error.status===404)$('network-panel').hidden=true;}}
async function waitNetwork(){for(let i=0;i<20;i++){const state=await api('/network');if(!state.pending){showNetwork(state);return state;}await new Promise(r=>setTimeout(r,300));}notice('阅读器仍在保存，请稍后刷新查看。',true);}
$('network-form').onsubmit=async event=>{event.preventDefault();const button=event.submitter;button.disabled=true;try{await api('/network','POST',{ssid:$('network-ssid').value,password:$('network-password').value});$('network-password').value='';const state=await waitNetwork();if(state&&state.result==='ok')notice('网络已保存到阅读器。');}catch(error){notice(error.message,true);}finally{button.disabled=false;}};
$('network-forget').onclick=async()=>{try{await api('/network','DELETE');await waitNetwork();notice('已忘记家庭网络。');}catch(error){notice(error.message,true);}};
// 只自动重试读取和幂等分块；新建/删除/完成等操作不盲重放。 / Retry only reads and idempotent chunks; never blindly replay create/delete/complete operations.
function requestTimeout(path,method){
 if(method==='POST'&&path.endsWith('/complete'))return 180000;
 if(method==='POST'&&path==='/uploads')return 45000;
 return 30000;
}
function friendlyError(error,timedOut){
 if(timedOut||error?.name==='AbortError'||/signal is aborted/i.test(error?.message||'')){const next=Error('连接超时，请检查与阅读器的 WiFi 连接后重试');next.retryable=true;return next;}
 if(error?.name==='TypeError'||/failed to fetch|load failed|networkerror/i.test(error?.message||'')){const next=Error('网络连接中断，请检查热点或 WiFi 后重试');next.retryable=true;return next;}
 return error;
}
async function api(path,method='GET',body,headers={}){
 const auth=token;const requestHeaders={...headers};if(auth)requestHeaders.Authorization='Bearer '+auth;
 let data=body;if(body&&!(body instanceof Uint8Array)){requestHeaders['Content-Type']='application/json';data=JSON.stringify(body);}
 const retries=method==='GET'||(method==='PUT'&&path.endsWith('/chunks'))?5:0;
 const timeoutMs=requestTimeout(path,method);
 for(let attempt=0;;attempt++){
  const controller=typeof AbortController==='function'?new AbortController():null;
  let timedOut=false;
  // 大字体的完整校验与空闲空间检查较慢，按接口分别留足等待时间，不重复提交非幂等写操作。
  // Large-font validation and free-space checks can take longer; budget timeouts per route without replaying non-idempotent writes.
  const timer=controller?setTimeout(()=>{timedOut=true;controller.abort();},timeoutMs):null;
  try{
   const response=await fetch('/api/v1'+path,{method,body:data,headers:requestHeaders,signal:controller?.signal});
   let value=null;try{value=await response.json();}catch{value=null;}
   if(!response.ok){if(response.status===401&&token===auth){token=null;connected(false);}const error=Error(value?.message||('设备响应异常（HTTP '+response.status+'）'));error.status=response.status;throw error;}
   if(!value){const error=Error('设备响应不完整，请重试');error.retryable=true;throw error;}
   return value;
  }catch(error){
   const normalized=friendlyError(error,timedOut);
   const retryable=normalized.status?normalized.status===408||normalized.status>=500:true;
   if(attempt>=retries||!retryable)throw normalized;
  }finally{if(timer)clearTimeout(timer);}
  await new Promise(resolve=>setTimeout(resolve,Math.min(500*(2**attempt),4000)));
 }
}

function selectKind(nextKind){
 if(!kinds[nextKind])return;
 kind=nextKind;
 for(const tab of document.querySelectorAll('[data-kind]'))tab.setAttribute('aria-pressed',String(tab.dataset.kind===kind));
 $('files').accept=kinds[kind].accept;
 $('accept-hint').textContent=kinds[kind].hint;
 syncFonts();
 if(onlineState.catalog)renderOnlineCategory(kind);
}
for(const button of document.querySelectorAll('[data-kind]'))button.onclick=()=>selectKind(button.dataset.kind);
// 资源原图管理：切换类别丢弃旧列表回复；删除前现取身份，内部锁屏缓存保留。/ Resource management: discard old-category replies and fetch identity before deletion; retain the internal lock cache.
const resourceTypes={font:{route:'fonts',title:'已安装字体',note:'删除后，使用它的书会暂用默认字体，阅读位置不变。'},cover:{route:'covers',title:'已上传封面',note:'封面与书籍文件同名关联，独立保存，不进入壁纸列表；旧 JPEG 可能优先匹配，替换后仍显示旧图时请删除旧封面。删除封面不删除书籍。'},banner:{route:'banners',title:'已上传首页横幅',note:'首页横幅独立保存（684×146），在设置 → 书架与主页 → 正在阅读上方背景中选用。删除后自动回退默认山水背景。'},wallpaper:{route:'wallpapers',title:'已上传锁屏壁纸',note:'上传前按原生边界优化为16灰阶PNG。删除图片文件会保留已应用的内部锁屏缓存；上传后仍需在设备预览并应用。'},mask:{route:'masks',title:'已上传阅读遮罩',note:'阅读遮罩独立保存并保留16级透明度，请在锁屏壁纸 → 遮罩中预览并应用。'}};
resourceTypes.background={route:'backgrounds',title:'已上传阅读背景',note:'背景独立保存；请在阅读器排版设置中选择并应用。删除后暂用无背景，字体和排版保留。'};
resourceTypes.dict={route:'dicts',title:'已安装词典',note:'支持直接拖入 StarDict .zip 压缩包或同名 .ifo/.idx/.dict(.dz)；在阅读正文长按词语或点工具栏“查词”即可离线查询。'};
function dictPartOf(name){
 const low=String(name||'').toLowerCase();
 if(low.endsWith('.dict.dz'))return {role:'dict.dz',stem:name.slice(0,-8),ext:'.dict.dz'};
 if(low.endsWith('.dict.gz'))return {role:'dict.gz',stem:name.slice(0,-8),ext:'.dict.gz'};
 if(low.endsWith('.idx.gz'))return {role:'idx.gz',stem:name.slice(0,-7),ext:'.idx.gz'};
 if(low.endsWith('.ifo'))return {role:'ifo',stem:name.slice(0,-4),ext:'.ifo'};
 if(low.endsWith('.idx'))return {role:'idx',stem:name.slice(0,-4),ext:'.idx'};
 if(low.endsWith('.dict'))return {role:'dict',stem:name.slice(0,-5),ext:'.dict'};
 if(low.endsWith('.zip'))return {role:'zip',stem:name.slice(0,-4),ext:'.zip'};
 return null;
}
function groupDictItems(items){
 const map=new Map();
 for(const item of items){
  const part=dictPartOf(item.name);
  const key=part?part.stem.toLowerCase():item.name.toLowerCase();
  let g=map.get(key);
  if(!g){g={stem:part?part.stem:item.name,items:[],exts:[],size:0,hasIfo:false,hasIdx:false,hasDict:false};map.set(key,g);}
  g.items.push(item);g.size+=item.size||0;
  if(part){if(!g.exts.includes(part.ext))g.exts.push(part.ext);if(part.role==='ifo')g.hasIfo=true;if(part.role==='idx')g.hasIdx=true;if(part.role==='dict'||part.role==='dict.dz')g.hasDict=true;}
 }
 return Array.from(map.values()).sort((a,b)=>a.stem.localeCompare(b.stem));
}
function dictMissingText(g){
 const miss=[];if(!g.hasIfo)miss.push('.ifo');if(!g.hasIdx)miss.push('.idx');if(!g.hasDict)miss.push('.dict/.dz');
 return miss.length?'缺 '+miss.join('/'):'完整可用';
}
function syncFonts(){resourceGeneration++;const panel=$('fonts-panel'),type=resourceTypes[kind];panel.hidden=!token||!type;if(!panel.hidden){$('resources-title').textContent=type.title;$('resources-note').textContent=type.note;$('fonts-list').replaceChildren();$('fonts-empty').hidden=true;$('fonts-more').hidden=true;loadFonts('');}}
async function loadFonts(after){
 const list=$('fonts-list'),selected=kind,auth=token,generation=resourceGeneration,type=resourceTypes[selected];if(!auth||!type)return;
 try{
  if(selected==='dict'){
   const all=[];let cursor='';
   for(let page=0;page<20;page++){
    const data=await api('/'+type.route+(cursor?'?after='+encodeURIComponent(cursor):''));
    if(generation!==resourceGeneration||kind!==selected||token!==auth)return;
    all.push(...data.items);
    if(!data.more||!data.items.length)break;
    cursor=data.items[data.items.length-1].name;
   }
   installedDictItems=all;installedByKind.dict=new Set(groupDictItems(all).map(g=>g.stem.toLowerCase()));list.replaceChildren();
   for(const g of groupDictItems(all)){
    const row=document.createElement('li'),name=document.createElement('span'),badge=document.createElement('span'),info=document.createElement('span'),button=document.createElement('button');
    const complete=g.hasIfo&&g.hasIdx&&g.hasDict;
    name.textContent=g.stem;badge.className='dict-badge '+(complete?'ok':'warn');badge.textContent=dictMissingText(g);name.append(badge);
    info.textContent=(g.exts.length?g.exts.join(' · ')+' · ':'')+size(g.size);
    button.className='secondary';button.textContent=g.items.length>1?'删除整套':'删除';button.onclick=()=>removeDictGroup(g);
    row.append(name,info,button);list.append(row);
   }
   $('fonts-empty').textContent='暂无已安装词典。';$('fonts-empty').hidden=list.children.length>0;$('fonts-more').hidden=true;
   if(onlineState.catalog&&onlineState.catId==='dict')renderOnlineCategory('dict');
   return;
  }
  const data=await api('/'+type.route+(after?'?after='+encodeURIComponent(after):''));
  if(generation!==resourceGeneration||kind!==selected||token!==auth)return;
  if(!after){list.replaceChildren();installedByKind[selected]=new Set();}
  const seenSet=installedByKind[selected]||(installedByKind[selected]=new Set());
  for(const item of data.items){seenSet.add(item.name.toLowerCase());const row=document.createElement('li'),name=document.createElement('span'),info=document.createElement('span'),button=document.createElement('button');
   name.textContent=item.name;info.textContent=size(item.size);button.className='secondary';button.textContent='删除';button.onclick=()=>removeFont(item,selected);row.append(name,info,button);list.append(row);}
  $('fonts-empty').textContent='暂无'+kinds[selected].label+'。';$('fonts-empty').hidden=list.children.length>0;$('fonts-more').hidden=!data.more;
  $('fonts-more').onclick=()=>loadFonts(data.items[data.items.length-1].name);
  if(onlineState.catalog&&onlineState.catId===selected)renderOnlineCategory(selected);
 }catch(error){if(generation!==resourceGeneration||kind!==selected||token!==auth)return;if(error.status===404)$('fonts-panel').hidden=true;else notice(error.message,true);}
}
function confirmDelete(name,selected){return new Promise(resolve=>{const dialog=$('delete-dialog'),label=kinds[selected].label;$('delete-title').textContent='删除这个'+label+'原文件？';$('delete-note').textContent=resourceTypes[selected].note+' 原文件删除后无法撤销。';$('delete-no').textContent='保留'+label;$('delete-name').textContent=name;$('delete-no').onclick=()=>{dialog.close();resolve(false);};$('delete-yes').onclick=()=>{dialog.close();resolve(true);};dialog.oncancel=()=>resolve(false);dialog.showModal();});}
async function removeFont(item,selected){
 const auth=token;if(!await confirmDelete(item.name,selected)||token!==auth)return;
 try{
  const old=await api('/files/'+selected+'?name='+encodeURIComponent(item.name));
  if(token!==auth)return;
  await api('/'+resourceTypes[selected].route+'?name='+encodeURIComponent(item.name)+'&size='+old.size+'&sha256='+old.sha256,'DELETE');
  notice('已删除'+kinds[selected].label+'原文件：'+item.name+'。');if(kind===selected)await loadFonts('');
 }catch(error){notice(error.message,true);await loadFonts('');}
}
async function removeDictGroup(group){
 const auth=token,names=group.items.map(i=>i.name);
 if(!await confirmDelete(names.join('、'),'dict')||token!==auth)return;
 try{
  for(const item of group.items){
   const old=await api('/files/dict?name='+encodeURIComponent(item.name));
   if(token!==auth)return;
   await api('/dicts?name='+encodeURIComponent(item.name)+'&size='+old.size+'&sha256='+old.sha256,'DELETE');
  }
  notice('已删除词典：'+group.stem+'（共 '+group.items.length+' 个文件）。');if(kind==='dict')await loadFonts('');
 }catch(error){notice(error.message,true);if(kind==='dict')await loadFonts('');}
}
// 后缀决定类别；书籍/字体放错标签时自动归类（图片用途必须由用户选封面或锁屏）。
// 手机文件选择器常给出无扩展名的名称，字体再按文件头识别并补上扩展名；错误分别说明格式、空文件与大小。
// The suffix decides the category; books/fonts dropped on the wrong tab are routed automatically (image use must be chosen as cover or lock screen).
// Phone pickers often give names without extensions, so fonts are also recognized by their header and get an extension; errors explain format, empty files and size separately.
const suffixOf=name=>name.includes('.')?'.'+name.split('.').pop().toLowerCase():'';
const accepts=(k,suffix)=>kinds[k].accept.split(',').includes(suffix);
async function fontHeader(file){try{const b=new Uint8Array(await file.slice(0,4).arrayBuffer());const tag=String.fromCharCode(...b);return tag==='OTTO'?'.otf':tag==='\0\x01\0\0'||tag==='true'?'.ttf':'';}catch(error){return '';}}
// 原图只在浏览器解码；封面固定184×256，锁屏图按原生边界等比缩小，始终重新编码16灰阶普通PNG。
// Decode originals only in the browser; covers are 184×256, lock images fit native bounds, and every image becomes a non-interlaced 16-gray PNG.
const IMAGE_PANEL={width:684,height:1216},IMAGE_COVER={width:184,height:256},IMAGE_BANNER={width:684,height:146};
const pngCrcTable=Uint32Array.from({length:256},(_,i)=>{for(let n=0;n<8;n++)i=i&1?0xedb88320^(i>>>1):i>>>1;return i>>>0;});
function crc32Bytes(bytes){let crc=0xffffffff;for(let i=0;i<bytes.length;i++)crc=pngCrcTable[(crc^bytes[i])&255]^(crc>>>8);return (crc^0xffffffff)>>>0;}
function pngChunk(tag,data){const out=new Uint8Array(data.length+12),view=new DataView(out.buffer);view.setUint32(0,data.length);out.set(new TextEncoder().encode(tag),4);out.set(data,8);let crc=0xffffffff;for(let i=4;i<out.length-4;i++)crc=pngCrcTable[(crc^out[i])&255]^(crc>>>8);view.setUint32(out.length-4,(crc^0xffffffff)>>>0);return out;}
// HTTP旧浏览器没有压缩流时仍写合法zlib，不允许退回原图；4位像素本身已限制传输上界。
// Older HTTP browsers without compression streams still receive valid zlib, never original uploads; packed pixels bound the transfer size.
async function imageDeflate(bytes){
 if(typeof CompressionStream==='function')return new Uint8Array(await new Response(new Blob([bytes]).stream().pipeThrough(new CompressionStream('deflate'))).arrayBuffer());
 const blocks=Math.ceil(bytes.length/65535),out=new Uint8Array(2+blocks*5+bytes.length+4);out.set([0x78,0x01]);let at=2,a=1,b=0;
 for(let start=0;start<bytes.length;start+=65535){const n=Math.min(65535,bytes.length-start);out[at++]=start+n===bytes.length?1:0;out[at++]=n&255;out[at++]=n>>>8;out[at++]=(~n)&255;out[at++]=((~n)>>>8)&255;out.set(bytes.subarray(start,start+n),at);at+=n;}
 for(const byte of bytes){a=(a+byte)%65521;b=(b+a)%65521;}new DataView(out.buffer).setUint32(at,((b<<16)|a)>>>0);return out;
}
// PNG预测只影响压缩字节，灰阶和透明度不变。/ PNG prediction changes compressed bytes only, never shades or alpha.
function imagePredict(filter,left,up,corner){if(filter===1)return left;if(filter===2)return up;if(filter===3)return (left+up)>>>1;if(filter!==4)return 0;const p=left+up-corner,a=Math.abs(p-left),b=Math.abs(p-up),c=Math.abs(p-corner);return a<=b&&a<=c?left:b<=c?up:corner;}
async function imagePng(rgba,width,height,preserveAlpha){
 let alpha=false;if(preserveAlpha)for(let i=3;i<rgba.length;i+=4)if(Math.round(rgba[i]/17)<15){alpha=true;break;}
 const depth=alpha?8:4,stride=alpha?width:Math.ceil(width/2),rows=new Uint8Array((stride+1)*height),previous=new Uint8Array(stride),row=new Uint8Array(stride);
 let legacyRows=null;
 for(let y=0;y<height;y++){
  row.fill(0);for(let x=0;x<width;x++){const at=4*(y*width+x),opacity=preserveAlpha?Math.round(rgba[at+3]/17):15;let gray=(rgba[at]*77+rgba[at+1]*150+rgba[at+2]*29+128)>>>8;
   if(!preserveAlpha)gray=Math.floor((gray*rgba[at+3]+255*(255-rgba[at+3])+127)/255);
   const shade=opacity?Math.min(15,Math.floor((gray+8)/17)):0;if(alpha)row[x]=(opacity<<4)|shade;else row[x>>>1]|=shade<<(x&1?0:4);}
  // 在五种PNG行预测中选择最小残差；不加抖动或降低画质。/ Pick the smallest residual among all five PNG predictors without dither or quality reduction.
  let filter=0,best=Infinity,legacyFilter=0,legacyBest=Infinity;for(let f=0;f<5;f++){let score=0;for(let x=0;x<stride;x++){const value=(row[x]-imagePredict(f,x?row[x-1]:0,previous[x],x?previous[x-1]:0))&255;score+=Math.min(value,256-value);}if(score<best){best=score;filter=f;}if(f<3&&score<legacyBest){legacyBest=score;legacyFilter=f;}}
  const offset=y*(stride+1);if(filter!==legacyFilter&&!legacyRows){legacyRows=new Uint8Array(rows.length);legacyRows.set(rows.subarray(0,offset));}if(legacyRows){legacyRows[offset]=legacyFilter;for(let x=0;x<stride;x++)legacyRows[offset+1+x]=(row[x]-imagePredict(legacyFilter,x?row[x-1]:0,previous[x],x?previous[x-1]:0))&255;}rows[offset]=filter;for(let x=0;x<stride;x++)rows[offset+1+x]=(row[x]-imagePredict(filter,x?row[x-1]:0,previous[x],x?previous[x-1]:0))&255;previous.set(row);if((y&63)===63)await new Promise(resolve=>setTimeout(resolve,0));
 }
 const header=new Uint8Array(13),view=new DataView(header.buffer);view.setUint32(0,width);view.setUint32(4,height);header[8]=depth;header[9]=alpha?3:0;
 const count=alpha?256:16,palette=new Uint8Array(count*3),opacity=new Uint8Array(count);for(let i=0;i<count;i++){palette.fill((i&15)*17,i*3,i*3+3);opacity[i]=(i>>>4)*17;}
 const parts=[new Uint8Array([137,80,78,71,13,10,26,10]),pngChunk('IHDR',header)];if(alpha)parts.push(pngChunk('PLTE',palette),pngChunk('tRNS',opacity));let compressed=await imageDeflate(rows);if(legacyRows&&typeof CompressionStream==='function'){const legacy=await imageDeflate(legacyRows);if(legacy.length<compressed.length)compressed=legacy;}parts.push(pngChunk('IDAT',compressed),pngChunk('IEND',new Uint8Array()));return new Blob(parts,{type:'image/png'});
}
async function prepareImage(file,name,target){
 if(target!=='cover'&&target!=='banner'&&target!=='wallpaper'&&target!=='mask'&&target!=='background')throw Error('请选择图片用途：封面、首页横幅、锁屏壁纸、阅读遮罩或阅读背景');
 if(typeof createImageBitmap!=='function')throw Error('浏览器不支持图片优化，请使用新版浏览器；未上传原图');
 let bitmap;try{bitmap=await createImageBitmap(file,{imageOrientation:'from-image'});}catch{throw Error('图片无法解码，请重新选择 PNG/JPEG；未上传原图');}
 try{
  if(!bitmap.width||!bitmap.height||bitmap.width*bitmap.height>64*1024*1024)throw Error('图片尺寸过大或无效，请先缩小；未上传原图');
  const cover=target==='cover',banner=target==='banner',paper=target==='background',fixed=cover||banner||paper,landscape=bitmap.width>bitmap.height,bounds=landscape?{width:IMAGE_PANEL.height,height:IMAGE_PANEL.width}:IMAGE_PANEL;
  const scale=Math.min(1,bounds.width/bitmap.width,bounds.height/bitmap.height),w=cover?IMAGE_COVER.width:banner?IMAGE_BANNER.width:paper?IMAGE_PANEL.width:Math.max(1,Math.round(bitmap.width*scale)),h=cover?IMAGE_COVER.height:banner?IMAGE_BANNER.height:paper?IMAGE_PANEL.height:Math.max(1,Math.round(bitmap.height*scale));
  const canvas=document.createElement('canvas');canvas.width=w;canvas.height=h;const g=canvas.getContext('2d',{willReadFrequently:true});if(!g)throw Error('无法准备图片画布；未上传原图');
  g.imageSmoothingEnabled=true;g.imageSmoothingQuality='high';if(fixed){g.fillStyle='#fff';g.fillRect(0,0,w,h);const fit=Math.max(w/bitmap.width,h/bitmap.height),sw=w/fit,sh=h/fit;g.drawImage(bitmap,(bitmap.width-sw)/2,(bitmap.height-sh)/2,sw,sh,0,0,w,h);}else g.drawImage(bitmap,0,0,w,h);
  const outputName=name.replace(/\.[^.]+$/, '')+'.png';
  const nameBytes=new TextEncoder().encode(outputName).length;
  if(paper&&nameBytes>=160)throw Error('阅读背景文件名过长，请缩短后重试；未上传原图');
  if(banner&&nameBytes>=148)throw Error('首页横幅文件名过长，请缩短后重试；未上传原图');
  const blob=await imagePng(g.getImageData(0,0,w,h).data,w,h,target==='mask'||target==='wallpaper');
  return {file:new File([blob],outputName,{type:'image/png'}),name:outputName,note:w+'×'+h+' · 16灰阶 PNG'};
 }finally{bitmap.close?.();}
}
// 词典上传优化：支持直接导入 .zip 词典包、自动解压 .idx.gz、明文 .dict/普通 gzip 自动转标准 RA DictZip，并校齐三件套。
// Dictionary upload optimization: direct .zip unpack, .idx.gz auto-inflate, plain .dict/gzip to RFC 1952 RA DictZip, and 3-part completeness check.
const DICTZIP_CHUNK=58315;
async function streamTransform(bytes,Ctor,format){
 return new Uint8Array(await new Response(new Blob([bytes]).stream().pipeThrough(new Ctor(format))).arrayBuffer());
}
function hasDictZipRA(bytes){
 if(!bytes||bytes.length<28||bytes[0]!==0x1f||bytes[1]!==0x8b||bytes[2]!==0x08||(bytes[3]&4)===0)return false;
 const xlen=bytes[10]|(bytes[11]<<8),extraEnd=12+xlen;
 if(xlen<10||extraEnd+8>=bytes.length)return false;
 for(let pos=12;pos+4<=extraEnd;){
  const slen=bytes[pos+2]|(bytes[pos+3]<<8);
  if(pos+4+slen>extraEnd)return false;
  if(bytes[pos]===0x52&&bytes[pos+1]===0x41&&slen>=6){
   const ver=bytes[pos+4]|(bytes[pos+5]<<8),chlen=bytes[pos+6]|(bytes[pos+7]<<8),chcnt=bytes[pos+8]|(bytes[pos+9]<<8);
   return ver===1&&chlen>0&&chcnt>0&&chcnt*2+6<=slen;
  }
  pos+=4+slen;
 }
 return false;
}
async function dictZipEncode(raw,onProgress){
 if(typeof CompressionStream!=='function')return null;
 const total=raw.length,chcnt=Math.max(1,Math.ceil(total/DICTZIP_CHUNK));
 if(chcnt>32760)return null;
 const chunks=[],lens=new Uint16Array(chcnt);
 for(let i=0;i<chcnt;i++){
  const slice=raw.subarray(i*DICTZIP_CHUNK,Math.min(total,(i+1)*DICTZIP_CHUNK));
  const comp=await streamTransform(slice,CompressionStream,'deflate-raw');
  if(!comp.length||comp.length>65535)return null;
  chunks.push(comp);lens[i]=comp.length;
  if((i&7)===7){onProgress?.((i+1)/chcnt);await new Promise(r=>setTimeout(r,0));}
 }
 const slen=6+chcnt*2,xlen=4+slen,hdr=new Uint8Array(12+xlen),dv=new DataView(hdr.buffer);
 hdr.set([0x1f,0x8b,0x08,0x04,0,0,0,0,2,3],0);dv.setUint16(10,xlen,true);
 hdr[12]=0x52;hdr[13]=0x41;dv.setUint16(14,slen,true);dv.setUint16(16,1,true);dv.setUint16(18,DICTZIP_CHUNK,true);dv.setUint16(20,chcnt,true);
 for(let i=0;i<chcnt;i++)dv.setUint16(22+i*2,lens[i],true);
 const trailer=new Uint8Array(8),tdv=new DataView(trailer.buffer);
 tdv.setUint32(0,crc32Bytes(raw),true);tdv.setUint32(4,total>>>0,true);
 return new Blob([hdr,...chunks,trailer],{type:'application/gzip'});
}
async function dictExtractZip(file){
 const bytes=new Uint8Array(await file.arrayBuffer());
 if(bytes.length<22)throw Error('不是有效的 ZIP 压缩包');
 const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
 let eocd=-1;const minOff=Math.max(0,bytes.length-65557);
 for(let at=bytes.length-22;at>=minOff;at--){if(view.getUint32(at,true)===0x06054b50&&at+22+view.getUint16(at+20,true)===bytes.length){eocd=at;break;}}
 if(eocd<0)throw Error('无法识别 ZIP 目录尾，请确认压缩包完整');
 const count=view.getUint16(eocd+10,true),cdSize=view.getUint32(eocd+12,true),cdOff=view.getUint32(eocd+16,true);
 if(count===0xffff||cdSize===0xffffffff||cdOff===0xffffffff)throw Error('暂不支持 ZIP64 压缩包，请解压后选择词典文件');
 if(count>512||cdOff+cdSize>eocd)throw Error('ZIP 目录损坏或条目过多');
 const decoder=typeof TextDecoder==='function'?new TextDecoder('utf-8',{fatal:false}):null;
 const extracted=[],seen=new Set();
 for(let i=0,at=cdOff;i<count;i++){
  if(at+46>cdOff+cdSize||view.getUint32(at,true)!==0x02014b50)throw Error('ZIP 目录记录损坏');
  const method=view.getUint16(at+10,true),expectedCrc=view.getUint32(at+16,true),compSize=view.getUint32(at+20,true),uncompSize=view.getUint32(at+24,true);
  const nameLen=view.getUint16(at+28,true),extraLen=view.getUint16(at+30,true),commentLen=view.getUint16(at+32,true),localOff=view.getUint32(at+42,true);
  const rawName=decoder?decoder.decode(bytes.subarray(at+46,at+46+nameLen)):String.fromCharCode(...bytes.subarray(at+46,at+46+nameLen));
  at+=46+nameLen+extraLen+commentLen;
  const norm=rawName.replace(/\\/g,'/');
  if(!norm||norm.endsWith('/')||norm.startsWith('__MACOSX/')||norm.split('/').some(s=>s.startsWith('.')||s==='..'))continue;
  const leaf=norm.split('/').pop();
  const part=dictPartOf(leaf);
  if(!part||part.role==='zip')continue;
  if(seen.has(leaf.toLowerCase()))continue;
  if(uncompSize>kinds.dict.limit)throw Error('压缩包内“'+leaf+'”超过 256 MiB 上限');
  if(localOff+30>bytes.length||view.getUint32(localOff,true)!==0x04034b50)throw Error('ZIP 本地头损坏：'+leaf);
  const dataOff=localOff+30+view.getUint16(localOff+26,true)+view.getUint16(localOff+28,true);
  if(dataOff+compSize>bytes.length)throw Error('ZIP 数据截断：'+leaf);
  const payload=bytes.subarray(dataOff,dataOff+compSize);
  let out;
  if(method===0)out=payload.slice();
  else if(method===8){if(typeof DecompressionStream!=='function')throw Error('当前浏览器不支持在线解压 ZIP，请先解压后再选词典文件');out=await streamTransform(payload,DecompressionStream,'deflate-raw');}
  else throw Error('“'+leaf+'”使用了不支持的 ZIP 压缩算法（'+method+'）');
  if(out.length!==uncompSize||crc32Bytes(out)!==expectedCrc)throw Error('“'+leaf+'”解压校验失败，请确认压缩包完整');
  seen.add(leaf.toLowerCase());
  extracted.push(new File([out],leaf));
 }
 if(!extracted.length)throw Error('压缩包内未找到 StarDict 词典文件（.ifo / .idx / .dict / .dict.dz）');
 return extracted;
}
function parseIfoMeta(text){
 const lines=text.replace(/^\uFEFF/,'').split(/\r?\n/);
 if(!lines.length||lines[0].trim()!=="StarDict's dict ifo file")return null;
 const meta={bookname:'',wordcount:0,idxfilesize:0};
 for(let i=1;i<lines.length&&i<64;i++){
  const eq=lines[i].indexOf('=');if(eq<1)continue;
  const k=lines[i].slice(0,eq).trim(),v=lines[i].slice(eq+1).trim();
  if(k==='bookname')meta.bookname=v;else if(k==='wordcount')meta.wordcount=Number(v)||0;else if(k==='idxfilesize')meta.idxfilesize=Number(v)||0;
 }
 return meta.wordcount>0?meta:null;
}
async function prepareDictFile(file,name){
 const part=dictPartOf(name);
 if(!part||part.role==='zip')throw Error('不是支持的 StarDict 词典文件');
 if(part.role==='ifo'){
  if(file.size>65536)throw Error('.ifo 文件过大，请检查格式');
  const text=typeof TextDecoder==='function'?new TextDecoder('utf-8').decode(await file.arrayBuffer()):await file.text();
  const meta=parseIfoMeta(text);
  if(!meta)throw Error('不是有效的 StarDict .ifo 索引头文件');
  return {file,name,meta,note:(meta.bookname||part.stem)+' · '+meta.wordcount.toLocaleString()+' 词'};
 }
 if(part.role==='idx.gz'){
  if(typeof DecompressionStream!=='function')throw Error('当前浏览器不支持解压 .idx.gz，请先解压为 .idx');
  notice('正在解压索引“'+name+'”…');
  const out=await streamTransform(new Uint8Array(await file.arrayBuffer()),DecompressionStream,'gzip');
  if(out.length<10||!out.subarray(0,Math.min(512,out.length)).includes(0))throw Error('解压后的 .idx 索引格式无效');
  const outName=part.stem+'.idx';
  return {file:new File([out],outName),name:outName,note:'已自动解压 .idx.gz（'+size(file.size)+' → '+size(out.length)+'）'};
 }
 if(part.role==='idx'){
  const head=new Uint8Array(await file.slice(0,512).arrayBuffer());
  if(file.size<10||!head.includes(0))throw Error('不是有效的 StarDict .idx 索引文件');
  return {file,name,note:''};
 }
 if(part.role==='dict.dz'){
  const head=new Uint8Array(await file.slice(0,Math.min(file.size,65560)).arrayBuffer());
  if(hasDictZipRA(head))return {file,name,note:'DictZip 随机索引已就绪'};
  if(head.length>=18&&head[0]===0x1f&&head[1]===0x8b&&head[2]===0x08){
   if(typeof DecompressionStream!=='function'||typeof CompressionStream!=='function')throw Error('该 .dict.dz 缺少随机索引头，请用新版浏览器自动重打包或提供标准 DictZip / 明文 .dict');
   notice('“'+name+'”缺少随机索引头，正在重打包为标准 DictZip…');
   const raw=await streamTransform(new Uint8Array(await file.arrayBuffer()),DecompressionStream,'gzip');
   const dz=await dictZipEncode(raw,p=>notice('正在重打包“'+name+'”… '+Math.round(p*100)+'%'));
   if(!dz)throw Error('词典体积过大，无法在浏览器重打包为 DictZip');
   return {file:new File([dz],name,{type:'application/gzip'}),name,note:'已修复为标准 DictZip 随机索引（'+size(dz.size)+'）'};
  }
  throw Error('不是有效的 DictZip (.dict.dz) 词典文件');
 }
 if(part.role==='dict.gz'){
  if(typeof DecompressionStream!=='function')throw Error('当前浏览器不支持解压 .dict.gz，请先解压');
  notice('正在转换“'+name+'”为标准 DictZip…');
  const raw=await streamTransform(new Uint8Array(await file.arrayBuffer()),DecompressionStream,'gzip');
  const dz=await dictZipEncode(raw,p=>notice('正在压缩“'+name+'”… '+Math.round(p*100)+'%'));
  if(dz&&dz.size<raw.length){const outName=part.stem+'.dict.dz';return {file:new File([dz],outName,{type:'application/gzip'}),name:outName,note:'已转为 DictZip（'+size(raw.length)+' → '+size(dz.size)+'）'};}
  const outName=part.stem+'.dict';return {file:new File([raw],outName),name:outName,note:'已解压为 .dict'};
 }
 if(part.role==='dict'){
  if(file.size>65536&&typeof CompressionStream==='function'){
   notice('正在将明文词典“'+name+'”压缩为 DictZip…');
   await new Promise(r=>setTimeout(r,0));
   const raw=new Uint8Array(await file.arrayBuffer());
   const dz=await dictZipEncode(raw,p=>notice('正在压缩“'+name+'”… '+Math.round(p*100)+'%'));
   if(dz&&dz.size<raw.length){
    const outName=part.stem+'.dict.dz';
    return {file:new File([dz],outName,{type:'application/gzip'}),name:outName,note:'已自动压缩为 DictZip（'+size(file.size)+' → '+size(dz.size)+'）'};
   }
  }
  return {file,name,note:''};
 }
 return {file,name,note:''};
}
function dictPriority(name){
 const p=dictPartOf(name);
 if(!p)return 3;
 if(p.role==='idx')return 0;
 if(p.role==='dict'||p.role==='dict.dz')return 1;
 if(p.role==='ifo')return 2;
 return 3;
}
function sortPendingDictEntries(){
 const idxs=[];const pending=[];
 for(let i=0;i<entries.length;i++){
  const e=entries[i];
  if(e.kind==='dict'&&!e.done&&!e.cancelled&&!e.id){idxs.push(i);pending.push(e);}
 }
 pending.sort((a,b)=>{
  const pa=dictPartOf(a.name),pb=dictPartOf(b.name);
  const sa=pa?pa.stem.toLowerCase():a.name.toLowerCase(),sb=pb?pb.stem.toLowerCase():b.name.toLowerCase();
  return sa!==sb?sa.localeCompare(sb):dictPriority(a.name)-dictPriority(b.name);
 });
 for(let i=0;i<idxs.length;i++)entries[idxs[i]]=pending[i];
}
function dictQueueCompleteness(){
 const active=entries.filter(e=>e.kind==='dict'&&!e.cancelled);
 if(!active.length)return '';
 const queuedStems=new Set(active.map(e=>{const p=dictPartOf(e.name);return p?p.stem.toLowerCase():e.name.toLowerCase();}));
 const combined=[...installedDictItems,...active.map(e=>({name:e.name,size:e.file.size}))];
 const groups=groupDictItems(combined).filter(g=>queuedStems.has(g.stem.toLowerCase()));
 const incomplete=groups.filter(g=>!(g.hasIfo&&g.hasIdx&&g.hasDict));
 if(incomplete.length){
  return '词典尚缺组件：'+incomplete.map(g=>g.stem+'（'+dictMissingText(g)+'）').join('；')+'。';
 }
 const names=groups.map(g=>{
  const ifoEntry=active.find(e=>{const p=dictPartOf(e.name);return p&&p.role==='ifo'&&p.stem.toLowerCase()===g.stem.toLowerCase()&&e.dictMeta;});
  return ifoEntry?.dictMeta?.bookname?(ifoEntry.dictMeta.bookname+'（'+ifoEntry.dictMeta.wordcount.toLocaleString()+' 词）'):g.stem;
 });
 return '词典三件套已齐套：'+names.join('、')+'，将按 .idx → .dict(.dz) → .ifo 顺序发送。';
}
async function add(files,forceKind=null){
 const selected=forceKind||kind,expanded=[];let addedCount=0;
 for(const file of Array.from(files)){
  const part=dictPartOf(file.name);
  if(selected==='dict'&&part&&part.role==='zip'){
   notice('正在解包词典压缩包“'+file.name+'”…');
   await new Promise(resolve=>setTimeout(resolve,0));
   try{const items=await dictExtractZip(file);for(const item of items)expanded.push({file:item,forceDict:true});notice('已从“'+file.name+'”提取 '+items.length+' 个词典文件。');}
   catch(error){notice('“'+file.name+'”：'+error.message,true);}
  }else expanded.push({file,forceDict:false});
 }
 let addedDict=false;
 for(const item of expanded){
  const file=item.file;let name=file.name,target=item.forceDict?'dict':selected;const suffix=suffixOf(name),part=dictPartOf(name);
  if(entries.length>=32){notice('一次最多添加 32 项。',true);break;}
  if(!item.forceDict&&!forceKind&&!accepts(target,suffix)){
   const other=['book','font'].find(k=>accepts(k,suffix));
   if(other)target=other;
   else if(part&&(part.role==='ifo'||part.role==='idx'||part.role==='dict'||part.role==='dict.dz'||part.role==='idx.gz'||part.role==='dict.gz'))target='dict';
   else if(selected==='font'){const ext=await fontHeader(file);if(ext){name=(name||'font')+ext;}}
  }
  if(!accepts(target,suffixOf(name))){notice('“'+file.name+'”不是'+kinds[selected].label+'支持的格式：'+kinds[selected].hint+'。',true);continue;}
  if(!file.size){notice('“'+file.name+'”是空文件。',true);continue;}
  let upload=file,dictMeta=null;
  if(target==='cover'||target==='banner'||target==='wallpaper'||target==='mask'||target==='background'){notice('正在优化“'+file.name+'”…');await new Promise(resolve=>setTimeout(resolve,0));try{const prepared=await prepareImage(file,name,target);upload=prepared.file;name=prepared.name;notice('“'+file.name+'”已优化为 '+prepared.note+'（'+size(file.size)+' → '+size(upload.size)+'），保存为“'+name+'”。');}catch(error){notice('“'+file.name+'”：'+error.message,true);continue;}}
  else if(target==='dict'){try{const prepared=await prepareDictFile(file,name);upload=prepared.file;name=prepared.name;dictMeta=prepared.meta||null;if(prepared.note)notice('“'+file.name+'”：'+prepared.note+'。');}catch(error){notice('“'+file.name+'”：'+error.message,true);continue;}}
  if(upload.size>kinds[target].limit){notice('“'+file.name+'”有 '+size(upload.size)+'，超过'+kinds[target].label+'上限 '+size(kinds[target].limit)+'。',true);continue;}
  if(entries.length>=32){notice('一次最多添加 32 项。',true);break;}
  if(!forceKind&&target!==kind)notice('“'+file.name+'”已按'+kinds[target].label+'发送。');
  const previewURL=target!=='dict'&&upload!==file&&typeof URL!=='undefined'&&typeof URL.createObjectURL==='function'?URL.createObjectURL(upload):null;
  entries.push({file:upload,name,kind:target,dictMeta,previewURL,state:'等待发送',offset:0,progress:0,paused:false,cancelled:false});
  addedCount++;
  if(target==='dict')addedDict=true;
 }
 if(addedDict){sortPendingDictEntries();const summary=dictQueueCompleteness();if(summary)notice(summary,summary.startsWith('词典尚缺'));}
 render();
 return addedCount;
}
$('files').onchange=()=>{add($('files').files);$('files').value='';};$('choose').onclick=event=>{event.stopPropagation();$('files').click();};$('drop').onclick=()=> $('files').click();$('drop').onkeydown=event=>{if(event.key==='Enter'||event.key===' '){event.preventDefault();$('files').click();}};
for(const event of ['dragenter','dragover'])$('drop').addEventListener(event,e=>{e.preventDefault();$('drop').classList.add('drag');});for(const event of ['dragleave','drop'])$('drop').addEventListener(event,e=>{e.preventDefault();$('drop').classList.remove('drag');if(event==='drop')add(e.dataTransfer.files);});
function size(bytes){return bytes<1024*1024?(bytes/1024).toFixed(1)+' KiB':(bytes/1024/1024).toFixed(1)+' MiB';}
function render(){const queue=$('queue');queue.replaceChildren();$('count').textContent=entries.length;$('clear').disabled=!entries.some(e=>e.done||e.cancelled);$('empty').hidden=entries.length>0;$('start').disabled=!token||running||!entries.some(e=>!e.done&&!e.cancelled&&!e.paused);for(const entry of entries){const row=document.createElement('div');row.className='item';const top=document.createElement('div');top.className='item-top';const info=document.createElement('div');info.className='item-info';const name=document.createElement('p');name.className='item-name';name.textContent=entry.name;const meta=document.createElement('p');meta.className='item-meta';meta.textContent=kinds[entry.kind].label+' · '+size(entry.file.size)+' · '+entry.state;info.append(name,meta);const actions=document.createElement('div');actions.className='item-actions';if(!entry.done&&!entry.cancelled){const pause=document.createElement('button');pause.textContent=entry.paused?'继续':'暂停';pause.onclick=()=>{entry.paused=!entry.paused;entry.state=entry.paused?'当前分块结束后暂停':'等待续传';render();if(!entry.paused)sendQueue();};const cancel=document.createElement('button');cancel.textContent='取消';cancel.onclick=async()=>{entry.cancelled=true;entry.worker?.terminate();entry.hashReject?.(Error('已取消'));try{if(entry.id)await api('/uploads/'+entry.id,'DELETE');entry.state='已取消';}catch(error){entry.cancelled=false;entry.paused=true;entry.state=error.message;}render();};if(!entry.id&&(!running||entry.paused)){const rename=document.createElement('button');rename.textContent='改名';rename.onclick=()=>renameEntry(entry);actions.append(rename);}actions.append(pause,cancel);}const bar=document.createElement('div');bar.className='bar';bar.setAttribute('role','progressbar');bar.setAttribute('aria-label',entry.name+' '+entry.state);bar.setAttribute('aria-valuemin','0');bar.setAttribute('aria-valuemax','100');bar.setAttribute('aria-valuenow',String(Math.round(entry.progress*100)));const fill=document.createElement('div');fill.className='fill';fill.style.width=Math.round(entry.progress*100)+'%';bar.append(fill);if(entry.previewURL){const image=document.createElement('img');image.className='item-preview';image.src=entry.previewURL;image.alt=kinds[entry.kind].label+'上传预览';top.append(image);}top.append(info,actions);row.append(top,bar);queue.append(row);}}
function hashFile(entry){return new Promise((resolve,reject)=>{const worker=new Worker('/hash-worker.mjs',{type:'module'});entry.worker=worker;entry.hashReject=reject;worker.onmessage=event=>{if(event.data.error){worker.terminate();reject(Error(event.data.error));}else if(event.data.sha256){worker.terminate();entry.worker=null;entry.hashReject=null;resolve(event.data.sha256);}else{entry.progress=event.data.progress;entry.state='准备文件 '+Math.round(entry.progress*100)+'%';render();}};worker.onerror=()=>{worker.terminate();reject(Error('摘要计算失败，请重新选择文件'));};worker.postMessage(entry.file);});}
function confirmReplace(name){return new Promise(resolve=>{const dialog=$('replace-dialog');$('replace-name').textContent=name;$('replace-no').onclick=()=>{dialog.close();resolve(false);};$('replace-yes').onclick=()=>{dialog.close();resolve(true);};dialog.oncancel=()=>resolve(false);dialog.showModal();});}
function renameEntry(entry){const dialog=$('name-dialog');$('save-name').value=entry.name;$('name-no').onclick=()=>dialog.close();$('name-yes').onclick=()=>{const name=$('save-name').value.trim(),original=entry.file.name.split('.').pop().toLowerCase();const limit=entry.kind==='background'?159:entry.kind==='banner'?147:255;if(!name||new TextEncoder().encode(name).length>limit||name.split('.').pop().toLowerCase()!==original){notice('名称无效，请保留原来的扩展名。',true);return;}entry.name=name;entry.state='等待发送';dialog.close();render();};dialog.showModal();}
function savedId(key,value){try{if(value===null)sessionStorage.removeItem?.(key);else if(value)sessionStorage.setItem(key,value);return sessionStorage.getItem(key);}catch{return null;}}
async function send(entry){if(!entry.sha)entry.sha=await hashFile(entry);entry.progress=0;if(entry.cancelled||entry.paused){if(entry.paused)entry.state='已暂停，可继续';render();return;}const key='pico-upload:'+entry.kind+':'+entry.name+':'+entry.file.size+':'+entry.sha;entry.id=entry.id||savedId(key);if(entry.id){try{const state=await api('/uploads/'+entry.id);if(state.name!==entry.name||state.size!==entry.file.size||state.sha256!==entry.sha||state.kind!==entry.kind)throw Error('请选择原文件后再续传');if(state.phase===2){entry.done=true;entry.progress=1;entry.state='已保存';render();return;}if(state.phase===3)throw Error('上传已取消，请重新添加');entry.offset=state.next_offset;}catch(error){if(error.status===404)entry.id=null;else throw error;}}
 if(!entry.id){const request={kind:entry.kind,name:entry.name,size:entry.file.size,sha256:entry.sha};try{entry.id=(await api('/uploads','POST',request)).upload_id;}catch(error){if(error.status!==409)throw error;const old=await api('/files/'+entry.kind+'?name='+encodeURIComponent(entry.name));if(!await confirmReplace(entry.name)){entry.paused=true;entry.state='原文件已保留';render();return;}request.replace={size:old.size,sha256:old.sha256};entry.id=(await api('/uploads','POST',request)).upload_id;}savedId(key,entry.id);}if(entry.cancelled){if(entry.id)await api('/uploads/'+entry.id,'DELETE');entry.state='已取消';render();return;}
 let synced=false;
 while(entry.offset<entry.file.size&&!entry.paused&&!entry.cancelled){const bytes=new Uint8Array(await entry.file.slice(entry.offset,entry.offset+65536).arrayBuffer());entry.state='发送中 '+Math.round(entry.offset/entry.file.size*100)+'%';entry.progress=entry.offset/entry.file.size;render();const chunk=new SHA256().update(bytes).hex();
  try{const result=await api('/uploads/'+entry.id+'/chunks','PUT',bytes,{'X-Offset':String(entry.offset),'X-Chunk-SHA256':chunk});entry.offset=result.next_offset;entry.progress=entry.offset/entry.file.size;synced=false;render();}
  catch(error){if(error.status===409&&!synced){const state=await api('/uploads/'+entry.id);if(state.phase===2){entry.done=true;entry.progress=1;entry.state=state.cleanup_pending?'已保存，备份待清理':'已保存';render();return;}if(state.phase===3)throw Error('上传已取消，请重新添加');if(state.next_offset!==entry.offset){entry.offset=state.next_offset;synced=true;continue;}}throw error;}}
 if(entry.cancelled)return;if(entry.paused){entry.state='已暂停，可继续';render();return;}entry.state='正在校验文件';render();
 try{const result=await api('/uploads/'+entry.id+'/complete','POST',{sha256:entry.sha});entry.done=true;entry.progress=1;entry.state=result.cleanup_pending?'已保存，备份待清理':'已保存';savedId(key,null);render();}
 catch(error){if(!error.status){try{const state=await api('/uploads/'+entry.id);if(state.phase===2){entry.done=true;entry.progress=1;entry.state=state.cleanup_pending?'已保存，备份待清理':'已保存';savedId(key,null);render();return;}}catch{}}if(error.status===422&&entry.id){try{await api('/uploads/'+entry.id,'DELETE');}catch{}entry.id=null;entry.offset=0;savedId(key,null);}throw error;}}
async function sendQueue(){if(running||!token)return;sortPendingDictEntries();running=true;render();for(const entry of entries){if(entry.done||entry.cancelled||entry.paused)continue;try{await send(entry);}catch(error){if(!entry.cancelled){entry.state=error.message;entry.paused=true;notice(error.message,true);}render();if(!token)break;}}running=false;render();syncFonts();if(entries.length&&entries.every(e=>e.done||e.cancelled))notice('这一批已处理完，可以添加更多内容。');}
$('start').onclick=sendQueue;$('clear').onclick=()=>{entries=entries.filter(e=>{if(!e.done&&!e.cancelled)return true;if(e.previewURL&&typeof URL!=='undefined')URL.revokeObjectURL(e.previewURL);return false;});render();};
// 在线共享素材与词典库：按需加载、绝不向外域携带设备令牌、下载前严格校验 SHA-256，复用本地优化与上传事务。
// Online shared-assets & dictionaries: lazy-loaded, never sends device tokens to external origins, strictly SHA-256 verified, and routed through the local optimizer/upload pipeline.
function onlineUrl(source,relPath){
 const clean=String(relPath||'').replace(/^\/+/,'');
 return source.base+clean.split('/').map(seg=>encodeURIComponent(seg)).join('/');
}
async function fetchExternal(url,timeoutMs){
 const controller=typeof AbortController==='function'?new AbortController():null;
 const timer=controller?setTimeout(()=>controller.abort(),timeoutMs):null;
 try{
  const response=await fetch(url,{method:'GET',mode:'cors',credentials:'omit',cache:'no-cache',signal:controller?.signal});
  if(!response.ok)throw Error('HTTP '+response.status);
  return response;
 }finally{if(timer)clearTimeout(timer);}
}
function onlineCategories(doc){
 const list=Array.isArray(doc?.categories)?doc.categories.slice():[];
 const dicts=Array.isArray(doc?.dictionaries)?doc.dictionaries:[];
 list.push({id:'dict',kind:'dict',title:'词典',shortTitle:'词典',description:'开源 StarDict 离线词典（自动解包并按 .idx → .dict.dz → .ifo 发送）。',uploadTarget:'传书页「词典」（/piconano/dicts/）',previewMode:'dict',count:dicts.length,items:dicts.map(d=>({name:d.fileName,stem:d.title,subtitle:d.subtitle,wordcount:d.wordcount,license:d.license,path:d.path,giteeUrl:d.giteeUrl,size:d.size,sha256:d.sha256,id:d.id}))});
 return list;
}
function onlineIsInstalled(catId,item){
 const set=installedByKind[catId];
 if(!set||!set.size)return false;
 if(catId==='dict'){const id=String(item.id||'').toLowerCase();return id&&set.has(id);}
 const pngName=String(item.name||'').replace(/\.[^.]+$/,'.png').toLowerCase();
 return set.has(String(item.name||'').toLowerCase())||set.has(pngName);
}
function onlineProseNode(){
 const prose=document.createElement('div');prose.className='online-prose';prose.setAttribute('aria-hidden','true');
 const h=document.createElement('strong');h.textContent='第一章 拾起阅读';
 const p1=document.createElement('span');p1.textContent='字与字之间，留出恰好的距离。让阅读回到舒适，把时间留给文字。';
 const p2=document.createElement('span');p2.textContent='在柔和的墨水屏上，每一页都安静沉稳。';
 prose.append(h,p1,p2);return prose;
}
function renderOnlinePreviewStage(stage,cat,item,url){
 stage.className='online-preview-stage is-'+(cat.previewMode||'wallpaper')+(cat.previewMode==='mask'&&onlineState.checker?' is-checker':'');
 stage.replaceChildren();
 if(cat.previewMode==='mask'||cat.previewMode==='background')stage.append(onlineProseNode());
 else if(cat.previewMode==='banner'){const ov=document.createElement('div');ov.className='online-banner-copy';ov.textContent='读书 · 拾起上次的阅读';stage.append(ov);}
 const img=document.createElement('img');img.src=url;img.alt=item.stem||item.name;img.crossOrigin='anonymous';
 stage.append(img);
}
function openOnlinePreview(cat,item){
 if(!onlineState.source)return;
 const url=onlineUrl(onlineState.source,item.path);
 onlineState.preview={cat,item,url};
 $('online-preview-title').textContent=(cat.title||'素材')+' · '+(item.stem||item.name);
 const dims=item.width&&item.height?item.width+'×'+item.height+' · ':'';
 $('online-preview-meta').textContent=dims+size(item.size||0)+' · 保存至 '+(cat.uploadTarget||kinds[cat.id]?.label||'');
 $('online-preview-bg').hidden=cat.previewMode!=='mask';
 $('online-preview-bg').textContent=onlineState.checker?'切换阅读底':'切换透明棋盘';
 renderOnlinePreviewStage($('online-preview-stage'),cat,item,url);
 $('online-preview-dialog').showModal?.();
}
async function loadOnlineFontPreview(item,url,box){
 if(typeof FontFace!=='function'||typeof document.fonts==='undefined')return;
 const family='pn-tr-font-'+String(item.sha256||item.name).slice(0,12);
 if(!onlineState.fontsLoaded.has(family)){
  const face=new FontFace(family,'url("'+url+'")');
  await face.load();document.fonts.add(face);onlineState.fontsLoaded.add(family);
 }
 box.style.fontFamily='"'+family+'","Microsoft YaHei",sans-serif';
}
async function importOnlineAsset(catId,item,autoSend=true){
 if(!onlineState.source)throw Error('请先加载在线素材库');
 const key=catId+':'+(item.sha256||item.name);
 if(onlineState.downloading.has(key))return 0;
 onlineState.downloading.add(key);
 if(onlineState.catalog)renderOnlineCategory(onlineState.catId||catId);
 try{
  const url=onlineUrl(onlineState.source,item.path);
  notice('正在从「'+onlineState.source.label+'」下载“'+(item.stem||item.name)+'”…');
  const response=await fetchExternal(url,catId==='dict'||catId==='font'?90000:30000);
  const bytes=new Uint8Array(await response.arrayBuffer());
  if(!bytes.length)throw Error('下载到的文件为空');
  if(item.sha256){
   const actual=new SHA256().update(bytes).hex();
   if(actual!==String(item.sha256).toLowerCase())throw Error('文件摘要校验失败，已中止导入');
  }
  const mime=catId==='dict'?'application/zip':/\.png$/i.test(item.name)?'image/png':/\.jpe?g$/i.test(item.name)?'image/jpeg':'application/octet-stream';
  const file=new File([bytes],item.name,{type:mime});
  const queued=await add([file],catId);
  if(queued>0&&autoSend&&token&&!running)await sendQueue();
  else if(queued>0&&!token)notice('已加入待传列表（等待与阅读器连接后可点击“开始发送”）。');
  return queued;
 }catch(error){
  notice('拉取“'+(item.stem||item.name)+'”失败：'+error.message+'（可尝试切换线路或使用 Gitee 下载）',true);
  throw error;
 }finally{
  onlineState.downloading.delete(key);
  if(onlineState.catalog)renderOnlineCategory(onlineState.catId||catId);
 }
}
function renderOnlineCategory(preferredId){
 const doc=onlineState.catalog,body=$('online-body'),tabs=$('online-tabs'),grid=$('online-grid');
 if(!doc||!body||!tabs||!grid)return;
 body.hidden=false;
 const cats=onlineCategories(doc);
 let cat=cats.find(c=>c.id===preferredId);
 if(!cat)cat=cats.find(c=>c.count>0)||cats[0];
 onlineState.catId=cat?cat.id:'';
 tabs.replaceChildren();
 for(const c of cats){
  const btn=document.createElement('button');btn.type='button';btn.className='online-tab';btn.setAttribute('role','tab');
  btn.setAttribute('aria-selected',String(c.id===onlineState.catId));
  const title=document.createElement('span');title.textContent=c.shortTitle||c.title;
  const cnt=document.createElement('span');cnt.className='online-tab-count';cnt.textContent=String(c.count||0);
  btn.append(title,cnt);
  btn.onclick=()=>{if(kinds[c.id]&&kind!==c.id)selectKind(c.id);else renderOnlineCategory(c.id);};
  tabs.append(btn);
 }
 const giteeHome=$('online-gitee-home');
 if(giteeHome){giteeHome.hidden=!doc.giteeReleaseUrl;if(doc.giteeReleaseUrl)giteeHome.href=doc.giteeReleaseUrl;}
 const packLink=$('online-pack-gitee');
 if(packLink){const hasPack=Boolean(cat?.pack?.giteeUrl);packLink.hidden=!hasPack;if(hasPack){packLink.href=cat.pack.giteeUrl;packLink.textContent='Gitee 整包（'+size(cat.pack.size||0)+'）';}}
 const checkerBtn=$('online-checker');
 if(checkerBtn){checkerBtn.hidden=cat?.previewMode!=='mask';checkerBtn.textContent=onlineState.checker?'切换阅读底':'切换棋盘底';}
 $('online-cat-note').textContent=cat?(cat.description||'选择素材即可预览并发送到设备。'):'';
 grid.dataset.mode=cat?.previewMode||'wallpaper';
 grid.replaceChildren();
 if(!cat||!cat.items||!cat.items.length){
  const empty=document.createElement('div');empty.className='online-empty';
  const msg=document.createElement('p');msg.textContent='「'+(cat?.title||'当前分类')+'」暂未收录在线素材，后续推送到仓库后会自动显示。';
  empty.append(msg);
  const avail=cats.filter(c=>c.count>0);
  if(avail.length){
   const jumps=document.createElement('div');jumps.className='online-empty-jumps';
   for(const c of avail){
    const jump=document.createElement('button');jump.type='button';jump.className='secondary';
    jump.textContent='查看'+(c.title||c.shortTitle)+'（'+c.count+'）';
    jump.onclick=()=>{if(kinds[c.id])selectKind(c.id);else renderOnlineCategory(c.id);};
    jumps.append(jump);
   }
   empty.append(jumps);
  }
  grid.append(empty);
  return;
 }
 for(const item of cat.items){
  const card=document.createElement('article');card.className='online-card';
  const url=onlineUrl(onlineState.source,item.path);
  const busy=onlineState.downloading.has(cat.id+':'+(item.sha256||item.name));
  if(cat.kind==='image'){
   const stage=document.createElement('button');stage.type='button';
   stage.className='online-thumb is-'+(cat.previewMode||'wallpaper')+(cat.previewMode==='mask'&&onlineState.checker?' is-checker':'');
   stage.setAttribute('aria-label','放大预览 '+(item.stem||item.name));
   if(cat.previewMode==='mask'||cat.previewMode==='background')stage.append(onlineProseNode());
   else if(cat.previewMode==='banner'){const ov=document.createElement('div');ov.className='online-banner-copy';ov.textContent='读书 · 拾起上次的阅读';stage.append(ov);}
   const img=document.createElement('img');img.src=url;img.alt=item.stem||item.name;img.loading='lazy';img.crossOrigin='anonymous';
   stage.append(img);stage.onclick=()=>openOnlinePreview(cat,item);card.append(stage);
  }else if(cat.kind==='font'){
   const box=document.createElement('div');box.className='online-font-box';
   const l1=document.createElement('p');l1.textContent='字与字之间，留出恰好的距离。';
   const l2=document.createElement('p');l2.textContent='PicoNano 0123456789';
   box.append(l1,l2);card.append(box);
   card._fontBox=box;
  }
  const info=document.createElement('div');info.className='online-card-body';
  const title=document.createElement('p');title.className='online-card-title';title.textContent=item.stem||item.name;
  if(onlineIsInstalled(cat.id,item)){const tag=document.createElement('span');tag.className='dict-badge ok';tag.textContent='设备已有';title.append(tag);}
  const meta=document.createElement('p');meta.className='online-card-meta';
  const parts=[];
  if(item.width&&item.height)parts.push(item.width+'×'+item.height);
  if(item.wordcount)parts.push(Number(item.wordcount).toLocaleString()+' 词');
  if(item.license)parts.push(item.license);
  if(item.size)parts.push(size(item.size));
  meta.textContent=parts.join(' · ');
  const actions=document.createElement('div');actions.className='online-card-actions';
  if(cat.kind==='image'){
   const prev=document.createElement('button');prev.type='button';prev.className='secondary';prev.textContent='预览';prev.onclick=()=>openOnlinePreview(cat,item);actions.append(prev);
  }else if(cat.kind==='font'&&card._fontBox){
   const tryBtn=document.createElement('button');tryBtn.type='button';tryBtn.className='secondary';tryBtn.textContent='试看字形';
   tryBtn.onclick=()=>{tryBtn.disabled=true;loadOnlineFontPreview(item,url,card._fontBox).catch(()=>notice('字形预览加载失败，可直接下载导入',true)).finally(()=>{tryBtn.disabled=false;});};
   actions.append(tryBtn);
  }else if(item.giteeUrl){
   const gitee=document.createElement('a');gitee.className='online-link';gitee.href=item.giteeUrl;gitee.target='_blank';gitee.rel='noopener noreferrer';gitee.textContent='Gitee';actions.append(gitee);
  }
  const sendBtn=document.createElement('button');sendBtn.type='button';sendBtn.disabled=busy;
  sendBtn.textContent=busy?'拉取中…':token?'发送到设备':'加入待传';
  sendBtn.onclick=()=>{importOnlineAsset(cat.id,item,Boolean(token)).catch(()=>{});};
  actions.append(sendBtn);
  info.append(title,meta,actions);card.append(info);grid.append(card);
 }
}
async function loadOnlineCatalog(preferredSourceId){
 if(onlineState.loading)return null;
 onlineState.loading=true;
 const loadBtn=$('online-load'),statusEl=$('online-status');
 if(loadBtn)loadBtn.disabled=true;
 const chosenId=preferredSourceId||$('online-source')?.value||'jsdelivr';
 const order=[...ONLINE_SOURCES.filter(s=>s.id===chosenId),...ONLINE_SOURCES.filter(s=>s.id!==chosenId)];
 let lastErr=null;
 try{
  for(const src of order){
   if(statusEl)statusEl.textContent='正在连接「'+src.label+'」获取在线素材清单…';
   try{
    const response=await fetchExternal(onlineUrl(src,'assets/share/manifest.json'),8000);
    const doc=await response.json();
    if(!doc||doc.schema!==1||!Array.isArray(doc.categories))throw Error('清单格式无效');
    onlineState.catalog=doc;onlineState.source=src;
    if($('online-source'))$('online-source').value=src.id;
    const total=onlineCategories(doc).reduce((sum,c)=>sum+(c.count||0),0);
    if(statusEl)statusEl.textContent='已连接「'+src.label+'」 · 共 '+total+' 项在线资源（下载前自动 SHA-256 校验，图片自动转为 16 灰阶 PNG）。';
    if(loadBtn)loadBtn.textContent='刷新素材库';
    const cats=onlineCategories(doc);
    const currentHas=cats.find(c=>c.id===kind&&c.count>0);
    const targetCat=currentHas?kind:(cats.find(c=>c.count>0)?.id||kind);
    renderOnlineCategory(targetCat);
    return doc;
   }catch(error){lastErr=error;}
  }
  if(statusEl)statusEl.textContent='无法连接在线素材库（若正使用「热点传书」，请改用「局域网传书」或联网后点重试；仍可正常上传本地文件）。';
  if(loadBtn)loadBtn.textContent='重试加载';
  notice('在线素材库暂不可达：'+(lastErr?.message||'网络不可用')+'，不影响本地传书。',true);
  return null;
 }finally{
  onlineState.loading=false;
  if(loadBtn)loadBtn.disabled=false;
 }
}
if($('online-load'))$('online-load').onclick=()=>loadOnlineCatalog();
if($('online-source'))$('online-source').onchange=()=>{if(onlineState.catalog)loadOnlineCatalog($('online-source').value);};
if($('online-checker'))$('online-checker').onclick=()=>{onlineState.checker=!onlineState.checker;if(onlineState.catalog)renderOnlineCategory(onlineState.catId);};
if($('online-preview-close'))$('online-preview-close').onclick=()=>$('online-preview-dialog').close?.();
if($('online-preview-bg'))$('online-preview-bg').onclick=()=>{if(!onlineState.preview)return;onlineState.checker=!onlineState.checker;$('online-preview-bg').textContent=onlineState.checker?'切换阅读底':'切换透明棋盘';renderOnlinePreviewStage($('online-preview-stage'),onlineState.preview.cat,onlineState.preview.item,onlineState.preview.url);if(onlineState.catalog)renderOnlineCategory(onlineState.catId);};
if($('online-preview-queue'))$('online-preview-queue').onclick=()=>{const p=onlineState.preview;if(!p)return;$('online-preview-dialog').close?.();importOnlineAsset(p.cat.id,p.item,false).catch(()=>{});};
if($('online-preview-send'))$('online-preview-send').onclick=()=>{const p=onlineState.preview;if(!p)return;$('online-preview-dialog').close?.();importOnlineAsset(p.cat.id,p.item,true).catch(()=>{});};
// 打开页面自动建立会话，客户端不再收集配对码。 / Establish a session automatically without collecting a pairing code.
api('/session').then(session=>{token=session.token;connected(true);notice('已连接，可以开始发送。');}).catch(error=>{connected(false);notice(error.message,true);});render();
