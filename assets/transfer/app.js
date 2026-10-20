// 中文传输队列，原文只读；名称使用textContent。/ Chinese transfer queue, read-only originals and textContent names.
import {SHA256} from './sha256.mjs';
const $=id=>document.getElementById(id),kinds={book:{label:'书籍',accept:'.txt,.epub',hint:'TXT、EPUB 电子书，支持多本添加',limit:512*1024*1024},font:{label:'字体',accept:'.ttf,.otf',hint:'TTF、OTF 字体，最大 32 MiB',limit:32*1024*1024},cover:{label:'封面',accept:'.png,.jpg,.jpeg',hint:'PNG/JPEG → 184×256、16灰阶 PNG；居中裁切，与书籍同名关联',limit:8*1024*1024},wallpaper:{label:'锁屏图片',accept:'.png,.jpg,.jpeg',hint:'PNG/JPEG → 原生尺寸内的16灰阶 PNG，保留透明度；上传后在阅读器预览并应用',limit:8*1024*1024}};
let kind='book',token=null,running=false,entries=[],resourceGeneration=0;
function notice(text,error=false){$('notice').textContent=text;$('notice').classList.toggle('error',error);}
function connected(value){$('connection').textContent=value?'已连接':'正在连接';$('connection').classList.toggle('connected',value);if(!value)$('network-panel').hidden=true;else loadNetwork();syncFonts();render();}
// 家庭网络：只在设备支持时显示；口令只提交，不回显。/ Home network: shown only when the device supports it; passwords are submitted, never echoed.
function showNetwork(state){$('network-panel').hidden=false;$('network-forget').hidden=!state.saved;$('network-status').textContent=state.pending?'正在保存到阅读器…':state.result==='failed'?'保存失败，请重试；原设置保留。':state.saved?'已保存：'+state.ssid+'。退出传书后可在菜单选择“局域网传书”。':'保存后，阅读器菜单会出现“局域网传书”，手机与阅读器连同一个 WiFi 即可传书。';}
async function loadNetwork(){try{showNetwork(await api('/network'));}catch(error){if(error.status===404)$('network-panel').hidden=true;}}
async function waitNetwork(){for(let i=0;i<20;i++){const state=await api('/network');if(!state.pending){showNetwork(state);return state;}await new Promise(r=>setTimeout(r,300));}notice('阅读器仍在保存，请稍后刷新查看。',true);}
$('network-form').onsubmit=async event=>{event.preventDefault();const button=event.submitter;button.disabled=true;try{await api('/network','POST',{ssid:$('network-ssid').value,password:$('network-password').value});$('network-password').value='';const state=await waitNetwork();if(state&&state.result==='ok')notice('网络已保存到阅读器。');}catch(error){notice(error.message,true);}finally{button.disabled=false;}};
$('network-forget').onclick=async()=>{try{await api('/network','DELETE');await waitNetwork();notice('已忘记家庭网络。');}catch(error){notice(error.message,true);}};
// 只自动重试读取和幂等分块；新建/删除/完成等操作不盲重放。 / Retry only reads and idempotent chunks; never blindly replay create/delete/complete operations.
async function api(path,method='GET',body,headers={}){
 const auth=token;if(auth)headers.Authorization='Bearer '+auth;
 let data=body;if(body&&!(body instanceof Uint8Array)){headers['Content-Type']='application/json';data=JSON.stringify(body);}
 const retries=method==='GET'||(method==='PUT'&&path.endsWith('/chunks'))?3:0;
 for(let attempt=0;;attempt++){
  const controller=typeof AbortController==='function'?new AbortController():null;
  // 大字体的完整校验可能较久，完成请求单独等待，不重复提交。 / Full validation of large fonts can take longer; wait separately without resubmitting completion.
  const timer=controller?setTimeout(()=>controller.abort(),method==='POST'&&path.endsWith('/complete')?120000:15000):null;
  try{
   const response=await fetch('/api/v1'+path,{method,body:data,headers,signal:controller?.signal}),value=await response.json();
   if(!response.ok){if(response.status===401&&token===auth){token=null;connected(false);}const error=Error(value.message||'操作失败');error.status=response.status;throw error;}
   return value;
  }catch(error){
   if(attempt>=retries||(error.status&&error.status<500))throw error;
  }finally{if(timer)clearTimeout(timer);}
  await new Promise(resolve=>setTimeout(resolve,500*(2**attempt)));
 }
}

for(const button of document.querySelectorAll('[data-kind]'))button.onclick=()=>{kind=button.dataset.kind;for(const tab of document.querySelectorAll('[data-kind]'))tab.setAttribute('aria-pressed',String(tab===button));$('files').accept=kinds[kind].accept;$('accept-hint').textContent=kinds[kind].hint;syncFonts();};
// 资源原图管理：切换类别丢弃旧列表回复；删除前现取身份，内部锁屏缓存保留。/ Resource management: discard old-category replies and fetch identity before deletion; retain the internal lock cache.
const resourceTypes={font:{route:'fonts',title:'已安装字体',note:'删除后，使用它的书会暂用默认字体，阅读位置不变。'},cover:{route:'covers',title:'已上传封面',note:'封面与书籍文件同名关联，独立保存，不进入壁纸列表；旧 JPEG 可能优先匹配，替换后仍显示旧图时请删除旧封面。删除封面不删除书籍。'},wallpaper:{route:'wallpapers',title:'已上传锁屏图片',note:'上传前按原生边界优化为16灰阶PNG。删除图片文件会保留已应用的内部锁屏缓存；上传后仍需在设备预览并应用。'}};
function syncFonts(){resourceGeneration++;const panel=$('fonts-panel'),type=resourceTypes[kind];panel.hidden=!token||!type;if(!panel.hidden){$('resources-title').textContent=type.title;$('resources-note').textContent=type.note;$('fonts-list').replaceChildren();$('fonts-empty').hidden=true;$('fonts-more').hidden=true;loadFonts('');}}
async function loadFonts(after){
 const list=$('fonts-list'),selected=kind,auth=token,generation=resourceGeneration,type=resourceTypes[selected];if(!auth||!type)return;
 try{
  const data=await api('/'+type.route+(after?'?after='+encodeURIComponent(after):''));
  if(generation!==resourceGeneration||kind!==selected||token!==auth)return;
  if(!after)list.replaceChildren();
  for(const item of data.items){const row=document.createElement('li'),name=document.createElement('span'),info=document.createElement('span'),button=document.createElement('button');
   name.textContent=item.name;info.textContent=size(item.size);button.className='secondary';button.textContent='删除';button.onclick=()=>removeFont(item,selected);row.append(name,info,button);list.append(row);}
  $('fonts-empty').textContent='暂无'+kinds[selected].label+'。';$('fonts-empty').hidden=list.children.length>0;$('fonts-more').hidden=!data.more;
  $('fonts-more').onclick=()=>loadFonts(data.items[data.items.length-1].name);
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
// 后缀决定类别；书籍/字体放错标签时自动归类（图片用途必须由用户选封面或锁屏）。
// 手机文件选择器常给出无扩展名的名称，字体再按文件头识别并补上扩展名；错误分别说明格式、空文件与大小。
// The suffix decides the category; books/fonts dropped on the wrong tab are routed automatically (image use must be chosen as cover or lock screen).
// Phone pickers often give names without extensions, so fonts are also recognized by their header and get an extension; errors explain format, empty files and size separately.
const suffixOf=name=>name.includes('.')?'.'+name.split('.').pop().toLowerCase():'';
const accepts=(k,suffix)=>kinds[k].accept.split(',').includes(suffix);
async function fontHeader(file){try{const b=new Uint8Array(await file.slice(0,4).arrayBuffer());const tag=String.fromCharCode(...b);return tag==='OTTO'?'.otf':tag==='\0\x01\0\0'||tag==='true'?'.ttf':'';}catch(error){return '';}}
// 原图只在浏览器解码；封面固定184×256，锁屏图按原生边界等比缩小，始终重新编码16灰阶普通PNG。
// Decode originals only in the browser; covers are 184×256, lock images fit native bounds, and every image becomes a non-interlaced 16-gray PNG.
const IMAGE_PANEL={width:684,height:1216},IMAGE_COVER={width:184,height:256};
const pngCrcTable=Uint32Array.from({length:256},(_,i)=>{for(let n=0;n<8;n++)i=i&1?0xedb88320^(i>>>1):i>>>1;return i>>>0;});
function pngChunk(tag,data){const out=new Uint8Array(data.length+12),view=new DataView(out.buffer);view.setUint32(0,data.length);out.set(new TextEncoder().encode(tag),4);out.set(data,8);let crc=0xffffffff;for(let i=4;i<out.length-4;i++)crc=pngCrcTable[(crc^out[i])&255]^(crc>>>8);view.setUint32(out.length-4,(crc^0xffffffff)>>>0);return out;}
// HTTP旧浏览器没有压缩流时仍写合法zlib，不允许退回原图；4位像素本身已限制传输上界。
// Older HTTP browsers without compression streams still receive valid zlib, never original uploads; packed pixels bound the transfer size.
async function imageDeflate(bytes){
 if(typeof CompressionStream==='function')return new Uint8Array(await new Response(new Blob([bytes]).stream().pipeThrough(new CompressionStream('deflate'))).arrayBuffer());
 const blocks=Math.ceil(bytes.length/65535),out=new Uint8Array(2+blocks*5+bytes.length+4);out.set([0x78,0x01]);let at=2,a=1,b=0;
 for(let start=0;start<bytes.length;start+=65535){const n=Math.min(65535,bytes.length-start);out[at++]=start+n===bytes.length?1:0;out[at++]=n&255;out[at++]=n>>>8;out[at++]=(~n)&255;out[at++]=((~n)>>>8)&255;out.set(bytes.subarray(start,start+n),at);at+=n;}
 for(const byte of bytes){a=(a+byte)%65521;b=(b+a)%65521;}new DataView(out.buffer).setUint32(at,((b<<16)|a)>>>0);return out;
}
async function imagePng(rgba,width,height,preserveAlpha){
 let alpha=false;if(preserveAlpha)for(let i=3;i<rgba.length;i+=4)if(Math.round(rgba[i]/17)<15){alpha=true;break;}
 const depth=alpha?8:4,stride=alpha?width:Math.ceil(width/2),rows=new Uint8Array((stride+1)*height),previous=new Uint8Array(stride),row=new Uint8Array(stride);
 for(let y=0;y<height;y++){
  row.fill(0);for(let x=0;x<width;x++){const at=4*(y*width+x),opacity=preserveAlpha?Math.round(rgba[at+3]/17):15;let gray=(rgba[at]*77+rgba[at+1]*150+rgba[at+2]*29+128)>>>8;
   if(!preserveAlpha)gray=Math.floor((gray*rgba[at+3]+255*(255-rgba[at+3])+127)/255);
   const shade=opacity?Math.min(15,Math.floor((gray+8)/17)):0;if(alpha)row[x]=(opacity<<4)|shade;else row[x>>>1]|=shade<<(x&1?0:4);}
  // 在None/Sub/Up中选择残差最小的行，兼顾纸纹与大块留白；不加抖动噪点。 / Pick the smallest None/Sub/Up residual without adding dither noise.
  let filter=0,best=Infinity;for(let f=0;f<3;f++){let score=0;for(let x=0;x<stride;x++){const value=(row[x]-(f===1?(x?row[x-1]:0):f===2?previous[x]:0))&255;score+=Math.min(value,256-value);}if(score<best){best=score;filter=f;}}
  const offset=y*(stride+1);rows[offset]=filter;for(let x=0;x<stride;x++)rows[offset+1+x]=(row[x]-(filter===1?(x?row[x-1]:0):filter===2?previous[x]:0))&255;previous.set(row);
 }
 const header=new Uint8Array(13),view=new DataView(header.buffer);view.setUint32(0,width);view.setUint32(4,height);header[8]=depth;header[9]=3;
 const count=alpha?256:16,palette=new Uint8Array(count*3),opacity=new Uint8Array(count);for(let i=0;i<count;i++){palette.fill((i&15)*17,i*3,i*3+3);opacity[i]=(i>>>4)*17;}
 const parts=[new Uint8Array([137,80,78,71,13,10,26,10]),pngChunk('IHDR',header),pngChunk('PLTE',palette)];if(alpha)parts.push(pngChunk('tRNS',opacity));parts.push(pngChunk('IDAT',await imageDeflate(rows)),pngChunk('IEND',new Uint8Array()));return new Blob(parts,{type:'image/png'});
}
async function prepareImage(file,name,target){
 if(target!=='cover'&&target!=='wallpaper')throw Error('请选择图片用途：封面或锁屏图片');
 if(typeof createImageBitmap!=='function')throw Error('浏览器不支持图片优化，请使用新版浏览器；未上传原图');
 let bitmap;try{bitmap=await createImageBitmap(file,{imageOrientation:'from-image'});}catch{throw Error('图片无法解码，请重新选择 PNG/JPEG；未上传原图');}
 try{
  if(!bitmap.width||!bitmap.height||bitmap.width*bitmap.height>64*1024*1024)throw Error('图片尺寸过大或无效，请先缩小；未上传原图');
  const cover=target==='cover',landscape=bitmap.width>bitmap.height,bounds=landscape?{width:IMAGE_PANEL.height,height:IMAGE_PANEL.width}:IMAGE_PANEL;
  const scale=Math.min(1,bounds.width/bitmap.width,bounds.height/bitmap.height),w=cover?IMAGE_COVER.width:Math.max(1,Math.round(bitmap.width*scale)),h=cover?IMAGE_COVER.height:Math.max(1,Math.round(bitmap.height*scale));
  const canvas=document.createElement('canvas');canvas.width=w;canvas.height=h;const g=canvas.getContext('2d',{willReadFrequently:true});if(!g)throw Error('无法准备图片画布；未上传原图');
  g.imageSmoothingEnabled=true;g.imageSmoothingQuality='high';if(cover){g.fillStyle='#fff';g.fillRect(0,0,w,h);const fit=Math.max(w/bitmap.width,h/bitmap.height),sw=w/fit,sh=h/fit;g.drawImage(bitmap,(bitmap.width-sw)/2,(bitmap.height-sh)/2,sw,sh,0,0,w,h);}else g.drawImage(bitmap,0,0,w,h);
  const blob=await imagePng(g.getImageData(0,0,w,h).data,w,h,!cover),outputName=name.replace(/\.[^.]+$/, '')+'.png';
  return {file:new File([blob],outputName,{type:'image/png'}),name:outputName,note:w+'×'+h+' · 16灰阶 PNG'};
 }finally{bitmap.close?.();}
}
async function add(files){const selected=kind;for(const file of Array.from(files)){let name=file.name,target=selected;const suffix=suffixOf(name);
 if(entries.length>=32){notice('一次最多添加 32 项。',true);break;}
 if(!accepts(target,suffix)){const other=['book','font'].find(k=>accepts(k,suffix));if(other)target=other;else if(selected==='font'){const ext=await fontHeader(file);if(ext){name=(name||'font')+ext;}}}
 if(!accepts(target,suffixOf(name))){notice('“'+file.name+'”不是'+kinds[selected].label+'支持的格式：'+kinds[selected].hint+'。',true);continue;}
 if(!file.size){notice('“'+file.name+'”是空文件。',true);continue;}
 let upload=file;if(target==='cover'||target==='wallpaper'){notice('正在优化“'+file.name+'”…');await new Promise(resolve=>setTimeout(resolve,0));try{const prepared=await prepareImage(file,name,target);upload=prepared.file;name=prepared.name;notice('“'+file.name+'”已优化为 '+prepared.note+'（'+size(file.size)+' → '+size(upload.size)+'），保存为“'+name+'”。');}catch(error){notice('“'+file.name+'”：'+error.message,true);continue;}}
 if(upload.size>kinds[target].limit){notice('“'+file.name+'”有 '+size(upload.size)+'，超过'+kinds[target].label+'上限 '+size(kinds[target].limit)+'。',true);continue;}
 if(entries.length>=32){notice('一次最多添加 32 项。',true);break;}
 if(target!==kind)notice('“'+file.name+'”已按'+kinds[target].label+'发送。');
 entries.push({file:upload,name,kind:target,state:'等待发送',offset:0,progress:0,paused:false,cancelled:false});}render();}
$('files').onchange=()=>{add($('files').files);$('files').value='';};$('choose').onclick=event=>{event.stopPropagation();$('files').click();};$('drop').onclick=()=> $('files').click();$('drop').onkeydown=event=>{if(event.key==='Enter'||event.key===' '){event.preventDefault();$('files').click();}};
for(const event of ['dragenter','dragover'])$('drop').addEventListener(event,e=>{e.preventDefault();$('drop').classList.add('drag');});for(const event of ['dragleave','drop'])$('drop').addEventListener(event,e=>{e.preventDefault();$('drop').classList.remove('drag');if(event==='drop')add(e.dataTransfer.files);});
function size(bytes){return bytes<1024*1024?(bytes/1024).toFixed(1)+' KiB':(bytes/1024/1024).toFixed(1)+' MiB';}
function render(){const queue=$('queue');queue.replaceChildren();$('count').textContent=entries.length;$('clear').disabled=!entries.some(e=>e.done||e.cancelled);$('empty').hidden=entries.length>0;$('start').disabled=!token||running||!entries.some(e=>!e.done&&!e.cancelled&&!e.paused);for(const entry of entries){const row=document.createElement('div');row.className='item';const top=document.createElement('div');top.className='item-top';const info=document.createElement('div');info.className='item-info';const name=document.createElement('p');name.className='item-name';name.textContent=entry.name;const meta=document.createElement('p');meta.className='item-meta';meta.textContent=kinds[entry.kind].label+' · '+size(entry.file.size)+' · '+entry.state;info.append(name,meta);const actions=document.createElement('div');actions.className='item-actions';if(!entry.done&&!entry.cancelled){const pause=document.createElement('button');pause.textContent=entry.paused?'继续':'暂停';pause.onclick=()=>{entry.paused=!entry.paused;entry.state=entry.paused?'当前分块结束后暂停':'等待续传';render();if(!entry.paused)sendQueue();};const cancel=document.createElement('button');cancel.textContent='取消';cancel.onclick=async()=>{entry.cancelled=true;entry.worker?.terminate();entry.hashReject?.(Error('已取消'));try{if(entry.id)await api('/uploads/'+entry.id,'DELETE');entry.state='已取消';}catch(error){entry.cancelled=false;entry.paused=true;entry.state=error.message;}render();};if(!entry.id&&(!running||entry.paused)){const rename=document.createElement('button');rename.textContent='改名';rename.onclick=()=>renameEntry(entry);actions.append(rename);}actions.append(pause,cancel);}const bar=document.createElement('div');bar.className='bar';bar.setAttribute('role','progressbar');bar.setAttribute('aria-label',entry.name+' '+entry.state);bar.setAttribute('aria-valuemin','0');bar.setAttribute('aria-valuemax','100');bar.setAttribute('aria-valuenow',String(Math.round(entry.progress*100)));const fill=document.createElement('div');fill.className='fill';fill.style.width=Math.round(entry.progress*100)+'%';bar.append(fill);top.append(info,actions);row.append(top,bar);queue.append(row);}}
function hashFile(entry){return new Promise((resolve,reject)=>{const worker=new Worker('/hash-worker.mjs',{type:'module'});entry.worker=worker;entry.hashReject=reject;worker.onmessage=event=>{if(event.data.error){worker.terminate();reject(Error(event.data.error));}else if(event.data.sha256){worker.terminate();entry.worker=null;entry.hashReject=null;resolve(event.data.sha256);}else{entry.progress=event.data.progress;entry.state='准备文件 '+Math.round(entry.progress*100)+'%';render();}};worker.onerror=()=>{worker.terminate();reject(Error('摘要计算失败，请重新选择文件'));};worker.postMessage(entry.file);});}
function confirmReplace(name){return new Promise(resolve=>{const dialog=$('replace-dialog');$('replace-name').textContent=name;$('replace-no').onclick=()=>{dialog.close();resolve(false);};$('replace-yes').onclick=()=>{dialog.close();resolve(true);};dialog.oncancel=()=>resolve(false);dialog.showModal();});}
function renameEntry(entry){const dialog=$('name-dialog');$('save-name').value=entry.name;$('name-no').onclick=()=>dialog.close();$('name-yes').onclick=()=>{const name=$('save-name').value.trim(),original=entry.file.name.split('.').pop().toLowerCase();if(!name||new TextEncoder().encode(name).length>255||name.split('.').pop().toLowerCase()!==original){notice('名称无效，请保留原来的扩展名。',true);return;}entry.name=name;entry.state='等待发送';dialog.close();render();};dialog.showModal();}
function savedId(key,value){try{if(value)sessionStorage.setItem(key,value);return sessionStorage.getItem(key);}catch{return null;}}
async function send(entry){if(!entry.sha)entry.sha=await hashFile(entry);entry.progress=0;if(entry.cancelled||entry.paused){if(entry.paused)entry.state='已暂停，可继续';render();return;}const key='pico-upload:'+entry.kind+':'+entry.name+':'+entry.file.size+':'+entry.sha;entry.id=entry.id||savedId(key);if(entry.id){try{const state=await api('/uploads/'+entry.id);if(state.name!==entry.name||state.size!==entry.file.size||state.sha256!==entry.sha||state.kind!==entry.kind)throw Error('请选择原文件后再续传');if(state.phase===2){entry.done=true;entry.progress=1;entry.state='已保存';render();return;}if(state.phase===3)throw Error('上传已取消，请重新添加');entry.offset=state.next_offset;}catch(error){if(error.status===404)entry.id=null;else throw error;}}
 if(!entry.id){const request={kind:entry.kind,name:entry.name,size:entry.file.size,sha256:entry.sha};try{entry.id=(await api('/uploads','POST',request)).upload_id;}catch(error){if(error.status!==409)throw error;const old=await api('/files/'+entry.kind+'?name='+encodeURIComponent(entry.name));if(!await confirmReplace(entry.name)){entry.paused=true;entry.state='原文件已保留';render();return;}request.replace={size:old.size,sha256:old.sha256};entry.id=(await api('/uploads','POST',request)).upload_id;}savedId(key,entry.id);}if(entry.cancelled){if(entry.id)await api('/uploads/'+entry.id,'DELETE');entry.state='已取消';render();return;}
 while(entry.offset<entry.file.size&&!entry.paused&&!entry.cancelled){const bytes=new Uint8Array(await entry.file.slice(entry.offset,entry.offset+65536).arrayBuffer());entry.state='发送中 '+Math.round(entry.offset/entry.file.size*100)+'%';entry.progress=entry.offset/entry.file.size;render();const chunk=new SHA256().update(bytes).hex();const result=await api('/uploads/'+entry.id+'/chunks','PUT',bytes,{'X-Offset':String(entry.offset),'X-Chunk-SHA256':chunk});entry.offset=result.next_offset;entry.progress=entry.offset/entry.file.size;render();}
 if(entry.cancelled)return;if(entry.paused){entry.state='已暂停，可继续';render();return;}entry.state='正在校验文件';render();const result=await api('/uploads/'+entry.id+'/complete','POST',{sha256:entry.sha});entry.done=true;entry.progress=1;entry.state=result.cleanup_pending?'已保存，备份待清理':'已保存';render();}
async function sendQueue(){if(running||!token)return;running=true;render();for(const entry of entries){if(entry.done||entry.cancelled||entry.paused)continue;try{await send(entry);}catch(error){if(!entry.cancelled){entry.state=error.message;entry.paused=true;notice(error.message,true);}render();if(!token)break;}}running=false;render();syncFonts();if(entries.length&&entries.every(e=>e.done||e.cancelled))notice('这一批已处理完，可以添加更多内容。');}
$('start').onclick=sendQueue;$('clear').onclick=()=>{entries=entries.filter(e=>!e.done&&!e.cancelled);render();};
// 打开页面自动建立会话，客户端不再收集配对码。 / Establish a session automatically without collecting a pairing code.
api('/session').then(session=>{token=session.token;connected(true);notice('已连接，可以开始发送。');}).catch(error=>{connected(false);notice(error.message,true);});render();
