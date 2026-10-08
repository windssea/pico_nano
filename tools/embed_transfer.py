"""嵌入设备网页，不包含开发演练入口。 / Embed device web assets without the development exercise route."""
import pathlib,sys
source=pathlib.Path(sys.argv[1]);out=pathlib.Path(sys.argv[2]);rows=['#include "transfer_assets.h"','#include <string.h>']
assets=[]
for index,name in enumerate(('index.html','app.js','style.css','sha256.mjs','hash-worker.mjs')):
 data=(source/name).read_bytes()
 if name=='index.html':
  data=data.replace('开发预览 · 当前连接的是 PC 服务<br>设备无线传输和壁纸应用尚未接入。'.encode(),'无线传输 · 当前连接的是小纸 Pico<br>字体和图片上传后需单独选择。'.encode())
 rows.append(f'static const uint8_t asset_{index}[]={{')
 rows.extend(','.join(str(v) for v in data[at:at+64])+',' for at in range(0,len(data),64));rows.append('};')
 mime='text/html' if name.endswith('.html') else 'text/css' if name.endswith('.css') else 'text/javascript'
 assets.append(f'{{"{"/" if name=="index.html" else "/"+name}","{mime}; charset=utf-8",asset_{index},sizeof asset_{index}}}')
rows.append('static const pn_transfer_asset_t assets[]={'+','.join(assets)+'};')
rows.append('const pn_transfer_asset_t *pn_transfer_asset(const char *path){for(size_t i=0;i<sizeof assets/sizeof assets[0];i++)if(!strcmp(path,assets[i].path))return &assets[i];return NULL;}')
out.write_text('\n'.join(rows)+'\n',encoding='utf8')
