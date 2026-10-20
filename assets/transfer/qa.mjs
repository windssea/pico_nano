// 仅开发演练：合成File，不读取本机私人文件。/ Development-only synthetic File, never reading private local files.
const button=document.createElement('button');button.textContent='添加演练小册';button.className='secondary';document.getElementById('drop').after(button);button.onclick=()=>{const transfer=new DataTransfer();transfer.items.add(new File(['小纸 Pico 的传输演练正文。\n'.repeat(150000)],'演练小册.txt',{type:'text/plain'}));const input=document.getElementById('files');input.files=transfer.files;input.dispatchEvent(new Event('change',{bubbles:true}));};
// 浏览器生成大图走真实图片队列及安装事务；本脚本不嵌入设备网页。 / Browser-generated large images exercise the real queue and install transaction; this script is never embedded in firmware.
for(const [label,kind,transparent] of [['封面','cover',false],['壁纸','wallpaper',false],['透明遮罩','wallpaper',true]]){
 const control=document.createElement('button');control.textContent='添加演练'+label;control.className='secondary';button.after(control);
 control.onclick=async()=>{control.disabled=true;try{
  const canvas=document.createElement('canvas');canvas.width=1368;canvas.height=2432;const g=canvas.getContext('2d');
  if(!transparent){const gradient=g.createLinearGradient(0,0,canvas.width,canvas.height);gradient.addColorStop(0,'#b4d9e9');gradient.addColorStop(0.5,'#536475');gradient.addColorStop(1,'#e4d0a3');g.fillStyle=gradient;g.fillRect(0,0,canvas.width,canvas.height);}
  for(let i=0;i<96;i++){g.fillStyle='rgba(35,62,83,'+(transparent?(i%16)/15:0.4)+')';g.fillRect(40+i*10,180+i*19,300,12);}
  g.fillStyle='#202932';g.font='96px serif';g.fillText('PicoNano',80,150);
  const blob=await new Promise(resolve=>canvas.toBlob(resolve,'image/png')),name='演练'+label+'.png';control.title=canvas.width+'×'+canvas.height+' · 原始 '+blob.size+' B';
  document.querySelector('[data-kind="'+kind+'"]').click();const transfer=new DataTransfer();transfer.items.add(new File([blob],name,{type:'image/png'}));const input=document.getElementById('files');input.files=transfer.files;input.dispatchEvent(new Event('change',{bubbles:true}));
 }finally{control.disabled=false;}};
}
const invalid=document.createElement('button');invalid.textContent='添加演练坏图片';invalid.className='secondary';button.after(invalid);invalid.onclick=()=>{document.querySelector('[data-kind="cover"]').click();const transfer=new DataTransfer();transfer.items.add(new File(['invalid image'],'不能上传.jpg',{type:'image/jpeg'}));const input=document.getElementById('files');input.files=transfer.files;input.dispatchEvent(new Event('change',{bubbles:true}));};
