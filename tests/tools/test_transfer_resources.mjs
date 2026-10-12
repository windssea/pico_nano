// 中文：真实网页脚本的异步类别/授权与删除确认边界。/ English: async category/auth and delete-confirmation boundaries in the actual page script.
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import test from 'node:test';

function harness(fetch){
  const elements=new Map();
  class Element {
    constructor(){this.children=[];this.dataset={};this.classList={toggle(){},add(){},remove(){}};this.style={};this.value='';}
    setAttribute(){} addEventListener(){} append(...items){this.children.push(...items);} replaceChildren(...items){this.children=[...items];}
    close(){} showModal(){} click(){}
  }
  const element=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id);};
  const tabs=['book','font','cover','wallpaper'].map(kind=>{const e=new Element();e.dataset.kind=kind;return e;});
  const context=vm.createContext({document:{getElementById:element,createElement:()=>new Element(),querySelectorAll:()=>tabs},fetch,URLSearchParams,Uint8Array,TextEncoder,location:{hash:'',pathname:'/',search:''},history:{replaceState(){}},sessionStorage:{getItem(){return null;},setItem(){}},setTimeout});
  const source=readFileSync(new URL('../../assets/transfer/app.js',import.meta.url),'utf8').replace(/^import .*;\r?$/m,'');
  vm.runInContext(source+'\nglobalThis.qa={token:value=>{token=value;},auth:()=>token,remove:removeFont};',context);
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

test('late old authorization failure cannot clear a new pairing',async()=>{
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
