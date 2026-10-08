# 设备TXT入口与内部存储接入

0.0.9将共享文件书架接入设备main，可以选择TXT阅读；尚不是完整第一版产品。未烧写、未进行真机启动/触摸/刷新/功耗验证。以下描述代码和可复跑软件证据。

## 启动边界

app_main仅创建独立32KiB栈worker（core1、priority5）。worker初始化NVS失败直接退出，不erase；BSP失败清理；PMU未ready或VCOM未标定/读失败时不推屏。PMU RUNNING握手失败也停止启动并禁止推屏。有效VCOM只读一次，传给显示封装，不写NVS或SY标定寄存器。worker大栈并不证明余量25%达标，仍需测high-water。

逻辑684×1216映射通过BSP epd_draw_pixel到物理framebuffer。present必须等待epd_hl扫描返回后才确认；初屏/扫描失败后先clear并从白推GC16，已知参考下使用全像素GC16/GL16（GL16不用差分白底路径）。波形温度暂用原工程25°C，真实温度和欠载恢复仍待实现。每次完成关闭EPD电源轨，保持像素图。

## 内部data挂载

使用固定esp_littlefs 1.20.3（commit 8274371dc5912196f66ac3e71dbb6291760cb8b0）及其littlefs子模块adad0fbbcf5382c20978d07f94f9c13be9041c1b。源码/vendor不改，配置在外；完整116文件摘要在LICENSES/littlefs-manifest.json。port为MIT，核心BSD-3-Clause，原LICENSE均保留。

只挂载分区label=data、base=/data，明确format_if_mount_failed=false、grow_on_mount=false、read_only=false。失败保留分区并展示提示；不把损坏当空白、不自动初始化、清NVS或格式化TF。其他assets/wallpaper分区本阶段不挂载。OBJ_NAME_LEN设96，防止64字符SHA加.a/.b（66bytes）在目录操作中被截断；块4096、read/prog128、cache512、lookahead128保持对应配置。

初始化镜像独立生成，不加入常规flash_args：

```powershell
python tools/dev.py factory-data
```

产物build-dev/factory-data.bin大小1MiB、LittleFS磁盘格式2.1，只有progress目录；生成器用同一固定核心，不下载Python镜像库，不访问任何设备。命令会重写该构建产物。runtime源码不调用格式化；主机生成器只格式化自己的内存镜像。

本工具没有烧录入口。只有确认为本固件布局的新板空分区，或用户明确选择清除data进行修复，才能单独写入data地址0xC20000（以实际partition-table验证为准）；已有data写此镜像会丢内部进度/后续书库数据，不能加入OTA或普通升级。未确认设备、布局、备份和用途前不运行此操作。此处不提供猜测COM端口的烧录命令。

同一命令另生成build-dev/factory-wallpaper.bin：1MiB空LittleFS，供首次启用锁屏壁纸时由用户单独写入wallpaper分区0xF00000（以实际partition-table为准）。固件挂载该分区时不格式化；未初始化时锁屏使用系统默认，壁纸设置页提示分区不可用。已应用的壁纸会被覆盖镜像清除，回到系统默认。镜像能否被设备驱动挂载尚未在真机验证。

## 开发入口与源文件

初始化data成功、TF真实已挂载后，优先列出/sdcard/books；没有该目录则列出/sdcard根目录。每页六本，点TXT行打开，点击阅读页左上方返回书架。可选/sdcard/fonts/reader.ttf（完整正文TTF）。缺字体用UI子集，可能缺字，不能声称全中文正文支持。缺卡/文件/内部存储会显示保留数据的提示，可点下方重试；其他已识别扩展名当前只展示，不解析；字体路径仍是开发约定，详见CATALOG.md。

所有TF读取借用同一个sd_media，卡失效调用media_lost关闭阅读/字体句柄并排空租约，再关闭会话保存内部位置；重新插卡需要用户重试后显式BSP remount，不自动格式化。保存屏障失败保留会话、显示失败提示，不继续释放/切电。内部目录/data/progress存原有SHA A/B记录，未知版本或损坏不会擦除。

触摸由CST读活动点，经pn_reader_input处理：两次空采样释放、按钮滑出取消、多点/读错取消、正文边缘点按及横向120px/纵向≤80px滑动翻页；每个手势最多提交一次。按键/字体变化/卡失效需取消旧触摸。SDL现复用同一识别器处理mouse down/move/up，真实DPI/物理触摸坐标待测。

电源短按先保存/关闭再显示锁页，再短按重新打开恢复位置；当前是锁屏原型，worker仍轮询，未进入浅睡/PMU软睡，不能声称低功耗锁屏已完成。壁纸/封面锁页、自动锁屏、拿起唤醒和协作关机均待实现。PMU报告ready与读键是正常host协议，不提供VCOM编辑。

## 资源与并发剩余项

app使用2MiB PSRAM预算，含阅读帧、glyph缓冲、body/UI字体及错误提示临时帧；BSP framebuffer、文件系统、libc、任务栈另计。未实测整机峰值、SRAM余量与栈水位，不能从PC池峰值推断硬件安全。

所有输入/绘制/推屏当前在单worker同步执行，hash/probe/seek/字体/扫描时不处理新输入；大书可能响应慢，需T05/T07分片worker/取消/输入队列与索引。当前未实现最长10ms计算片段、deadline或最顶级跟手体验。测试入口不会被当作第一版完成。

## 可复跑证据

```powershell
python tools/dev.py host
python tools/dev.py sim
python tools/dev.py sdl
python tools/dev.py firmware-ci
python tools/dev.py firmware-board
python tools/dev.py docs
```

host新增实际设备main+硬件替身：NVS失败不继续、VCOM无效不推屏、mount配置禁止format/grow、缺卡只提示；共享输入的取消/一次提交测试。LittleFS核心在1MiB NOR替身上验证空白mount不写、不自动format，以及A/B在每个观测到的prog/erase中断点、11种partial写/擦切点后重挂载恢复旧或新完整记录。

软件NOR模拟不证明真机随机断电/flash缓存/真实VFS同步已验收；还有真实mount/fstat/fsync、同卡拔插、GPIO坐标与三键、VCOM factory gate、120MHz稳定性、温度与功耗门。SDK两配置编译验证同源代码，未运行设备。

sdl入口构建固定SDK基础的可缓存测试镜像，只在该镜像安装SDL2，不改Windows工具链；运行dummy事件CTest并启用ASan/UBSan，不能显示桌面窗口。实际窗口需要Linux/WSLg原生命令（参阅READER_SESSION）。

## TXT书签交互

0.0.11可点阅读页顶部“书签”进入六条列表，添加已显示页、选条目跳转/改名/删除确认，跳转后顶部“返回”恢复原锚点。设备名称键盘支持字母/数字/空格/常用符号，不提供设备中文输入法；已保存中文名可显示，覆盖范围受字体影响。书签页面复用阅读帧，未呈现成功的确认页禁止删除；每秒最多一次失败呈现重试。卡失效/锁屏/换书取消草稿，原始阅读进度仍走保存屏障。没有实际设备点击或刷新验收，详见 [书签界面](BOOKMARK_UI.md)。

0.0.13正文顶部“排版”打开七项设置（含字间距）。改动先进入草稿，预览显示原锚点正文，点正文回设置，再应用或取消。锁屏/离书丢未申请应用的草稿；已经申请应用但写失败会阻拦关闭并保留重试，详见 [配置契约](TYPESETTING_SETTINGS.md)。


0.0.28的TXT/EPUB分派、续读与动态PSRAM pool边界见 [混合阅读入口](EPUB_ROUTING.md)，设备仍须实际烧写/验收。
