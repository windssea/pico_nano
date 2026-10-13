// 中文：真实网页脚本的异步类别/授权与删除确认边界。/ English: async category/auth and delete-confirmation boundaries in the actual page script.
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import test from 'node:test';

function harness(fetch,bootstrap=false){
  const elements=new Map();
  class Element {
    constructor(){this.children=[];this.dataset={};this.classList={toggle(){},add(){},remove(){}};this.style={};this.value='';}
    setAttribute(){} addEventListener(){} append(...items){this.children.push(...items);} replaceChildren(...items){this.children=[...items];}
    close(){} showModal(){} click(){}
  }
  const element=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id);};
  const tabs=['book','font','cover','wallpaper'].map(kind=>{const e=new Element();e.dataset.kind=kind;return e;});
  const context=vm.createContext({document:{getElementById:element,createElement:()=>new Element(),querySelectorAll:()=>tabs},fetch,URLSearchParams,Uint8Array,TextEncoder,location:{hash:'',pathname:'/',search:''},history:{replaceState(){}},sessionStorage:{getItem(){return null;},setItem(){}},setTimeout,clearTimeout,AbortController});
  const source=readFileSync(new URL('../../assets/transfer/app.js',import.meta.url),'utf8').replace(/^import .*;\r?$/m,'');
  vm.runInContext((bootstrap?source:source.slice(0,source.indexOf('// 打开页面'))+'render();')+'\nglobalThis.qa={token:value=>{token=value;},auth:()=>token,api,remove:removeFont};',context);
  return {context,element,choose:kind=>tabs.find(t=>t.dataset.kind===kind).onclick()};
}
const reply=(value,status=200)=>({ok:status<400,status,json:async()=>value});
const settle=()=>new Promise(resolve=>setImmediate(resolve));

test('late font list cannot repaint the wallpaper category',async()=>{
  let resolveFont;const h=harness(url=>url.endsWith('/fonts')?new Promise(r=>{resolveFont=r;}):Promise.resolve(reply({items:[{name:'透明.png',size:32}],more:false})));
  h.context.qa.token('current');h.choose('font');h.choose('wallpaper');await settle();
  resolveFont(reply({items:[{name:'stale.ttf',size:12}],more:false}));await settle();
  assert.equal(h.element('resources-title').textContent,'锁屏图片原图');
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
