# 文件书架与选择阅读

0.0.9提供共享文件书架，可在设备及PC选择TXT。它是第一版开发中的一个入口，封面提取、元数据索引、最近阅读和书签列表仍在后续开发范围。

设备优先读取TF上的books目录，目录不存在时读取TF根目录，不递归进入子目录。每页六本，按UTF-8文件名字节顺序排列；不是拼音或自然数字排序。点TXT行进入阅读，点阅读页左上方返回书架，底部按钮切换书目页。上次阅读位置按内容SHA恢复，同名不同内容不会混用位置；同内容不同文件名共享位置。

TXT/EPUB/PDF/FB2/CBZ扩展名均可识别并展示，大小写不敏感。当前TXT和EPUB连接解析器；PDF/FB2/CBZ条目提示尚未接入，保留文件。格式卡是无封面的占位卡，不能视为提取出的书籍封面。显示文件名和文件大小，不读取作者/标题元数据。长名称最多两行，超出裁切；UI字体子集缺字显示方框，完整书名字体回退仍待完善。

## PC窗口

先运行 `python tools/dev.py sdl` 验证SDL构建及dummy事件测试。真正显示窗口需要有桌面环境的Linux/WSLg，在该环境构建并执行：

```sh
cmake -S simulator -B build-sdl -DPN_SIM_SDL=ON
cmake --build build-sdl
build-sdl/pn_sim --library /path/to/books --state-dir /path/to/state
```

上/下选择，Enter打开，PageUp/PageDown切书目页，Esc从阅读保存返回、从书架退出。阅读中左右键翻页，+/-调整字号；鼠标支持书架行/按钮、正文翻页和左上方返回。`--font FONT.ttf`选择正文TTF，不改变书架常驻UI字体。保存失败留在阅读会话，Esc重试，不丢dirty位置。窗口关闭请求同样先保存。

`--library`不可与`--book`、静态捕获或headless组合。Windows Docker的dummy驱动只跑自动事件，不展示Windows桌面。PC模拟共享逻辑、字体和状态，不模拟ESP32指令、电纸波形、功耗或真实SD时延。

## 有界分页契约

pn_catalog借用已挂载介质的READ租约，扫描前、逐条和结束校验代次，不挂载、写文件或取得长期独占权。每次扫描只保留六条候选；previous查询选择游标之前最近六条，next选择之后最近六条。page.more始终表示本页末尾之后还有条目，返回前页后仍可以再往后翻。

忽略隐藏文件、不支持扩展名、非普通文件、非法UTF-8/控制字符/路径符号、超长名称或完整路径。主机lstat拒绝条目符号链接；设备FAT使用stat。目录路径由可信owner提供，调用方不得用不可信上传参数直接指定扫描目录。无法stat的条目计为跳过，opendir/readdir/closedir失败返回IO；损坏介质不能被当作成功的空书架。

文件名缓冲768bytes、完整路径1024bytes，每页固定六条；绘图帧为684×1216、4bpp。实现每页全目录扫描且逐条stat，内存不随库大小增长，但时间仍为O(N)，没有异步取消、deadline或持久索引，不承诺大书库打开速度。1000个真实文件测试验证分页和过滤；真实FAT长文件名、DPI/触摸、性能与媒体拔插仍需上板测试。

静态书架捕获工具 `pn_shelf_snapshot BOOK_DIR OUTPUT.pgm` 使用相同查询和绘制，只写指定图像，不打开正文或修改阅读历史。自动回归入口：`python tools/dev.py host`、`python tools/dev.py sdl`、两个firmware构建和`python tools/dev.py docs`。

基础阅读功能的实际覆盖见 [阅读状态契约](READING_STATE.md)，设备挂载及硬件边界见 [设备接入](DEVICE_PORT.md)。

0.0.12顶部增加“最近/全部”切换与“继续”入口。R切列表，C继续最近一本，读取时核对期望SHA；原文件缺失或同路径内容改变不会清历史。详细维护契约见 [最近阅读](RECENT_READING.md)。
