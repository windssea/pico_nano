// 中文传输队列，原文只读；名称使用textContent。/ Chinese transfer queue, read-only originals and textContent names.
import {SHA256} from './sha256.mjs';
const $=id=>document.getElementById(id),kinds={book:{label:'书籍',accept:'.txt,.epub',hint:'TXT、EPUB 电子书，支持多本添加',limit:512*1024*1024},font:{label:'字体',accept:'.ttf,.otf',hint:'TTF、OTF 字体，最大 32 MiB',limit:32*1024*1024},cover:{label:'封面',accept:'.png,.jpg,.jpeg',hint:'PNG、JPEG 图片，大图会在上传前自动缩小',limit:8*1024*1024},wallpaper:{label:'锁屏图片',accept:'.png,.jpg,.jpeg',hint:'大图会在上传前自动缩小；上传后在阅读器菜单“锁屏壁纸”中预览并应用',limit:8*1024*1024}};
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
const resourceTypes={font:{route:'fonts',title:'已安装字体',note:'删除后，使用它的书会暂用默认字体，阅读位置不变。'},cover:{route:'covers',title:'封面原图',note:'删除外置封面后，书架会重新查找内嵌封面或显示占位。书籍不会删除。'},wallpaper:{route:'wallpapers',title:'锁屏图片原图',note:'支持 PNG、JPEG。删除原图会保留已应用的内部锁屏缓存；上传后仍需在设备预览并应用。'}};
function syncFonts(){resourceGeneration++;const panel=$('fonts-panel'),type=resourceTypes[kind];panel.hidden=!token||!type;if(!panel.hidden){$('resources-title').textContent=type.title;$('resources-note').textContent=type.note;$('fonts-list').replaceChildren();$('fonts-empty').hidden=true;$('fonts-more').hidden=true;loadFonts('');}}
async function loadFonts(after){
 const list=$('fonts-list'),selected=kind,auth=token,generation=resourceGeneration,type=resourceTypes[selected];if(!auth||!type)return;
 try{
  const data=await api('/'+type.route+(after?'?after='+encodeURIComponent(after):''));
  if(generation!==resourceGeneration||kind!==selected||token!==auth)return;
  if(!after)list.replaceChildren();
  for(const item of data.items){const row=document.createElement('li'),name=document.createElement('span'),info=document.createElement('span'),button=document.createElement('button');
   name.textContent=item.name;info.textContent=size(item.size);button.className='secondary';button.textContent='删除';button.onclick=()=>removeFont(item,selected);row.append(name,info,button);list.append(row);}
  $('fonts-empty').textContent='还没有'+type.title+'。';$('fonts-empty').hidden=list.children.length>0;$('fonts-more').hidden=!data.more;
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
// 封面/锁屏图上传前在浏览器里处理：按EXIF转正，长边超过1824（面板1.5倍）时缩小；渐进JPEG/交错PNG重编码为基线JPEG/普通PNG，免设备整图解码内存不足；PNG保持PNG以保留透明。失败或无收益时保留原图。
// Covers/lock images are prepared in the browser before upload: EXIF-upright, shrunk when the long edge exceeds 1824 (1.5x the panel); progressive JPEG / interlaced PNG are re-encoded as baseline JPEG / plain PNG so the device avoids whole-image decode OOM; PNG stays PNG to keep transparency. Failures or no gain keep the original.
const IMAGE_EDGE=1824;
async function imageNeeds(file,png){try{const b=new Uint8Array(await file.slice(0,Math.min(file.size,262144)).arrayBuffer());if(png)return b.length>28&&b[28]===1;for(let i=2;i+3<b.length;){if(b[i]!==0xff){i++;continue;}const m=b[i+1];if(m===0xc2||m===0xc6||m===0xca||m===0xce)return true;if(m===0xda||m===0xd9)return false;if(m===0xff||m===0x01||(m>=0xd0&&m<=0xd7)){i+=2;continue;}i+=2+((b[i+2]<<8)|b[i+3]);}return false;}catch{return false;}}
async function prepareImage(file,name){const png=suffixOf(name)==='.png';if(typeof createImageBitmap!=='function')return {file};let bitmap;try{bitmap=await createImageBitmap(file,{imageOrientation:'from-image'});}catch{return {file};}
 try{const scale=Math.min(1,IMAGE_EDGE/Math.max(bitmap.width,bitmap.height)),w=Math.max(1,Math.round(bitmap.width*scale)),h=Math.max(1,Math.round(bitmap.height*scale));const needed=scale<1||await imageNeeds(file,png);if(!needed)return {file};
  const canvas=document.createElement('canvas');canvas.width=w;canvas.height=h;const g=canvas.getContext('2d');if(!png){g.fillStyle='#fff';g.fillRect(0,0,w,h);}g.drawImage(bitmap,0,0,w,h);
  const blob=await new Promise(resolve=>canvas.toBlob(resolve,png?'image/png':'image/jpeg',0.9));if(!blob||!blob.size)return {file};
  return {file:new File([blob],name,{type:blob.type}),note:w+'×'+h};}finally{bitmap.close?.();}}
async function add(files){for(const file of files){let name=file.name,target=kind;const suffix=suffixOf(name);
 if(!accepts(target,suffix)){const other=['book','font'].find(k=>accepts(k,suffix));if(other)target=other;else if(kind==='font'){const ext=await fontHeader(file);if(ext){name=(name||'font')+ext;}}}
 if(!accepts(target,suffixOf(name))){notice('“'+file.name+'”不是'+kinds[kind].label+'支持的格式：'+kinds[kind].hint+'。',true);continue;}
 if(!file.size){notice('“'+file.name+'”是空文件。',true);continue;}
 let upload=file;if(target==='cover'||target==='wallpaper'){const prepared=await prepareImage(file,name);upload=prepared.file;if(prepared.note)notice('“'+file.name+'”已在浏览器优化为 '+prepared.note+'（'+size(file.size)+' → '+size(upload.size)+'）。');}
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
