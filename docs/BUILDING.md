# PicoNano 构建、运行与验证

更新：2026-10-14。这里只记录现有可复跑入口；当前实现/未完工作分别见[架构](ARCHITECTURE.md)和[开发指南](DEVELOPMENT_PLAN.md)。固件构建、串口写入与真机验收是不同阶段，默认工具命令不烧录。

`catalog_index`与`catalog_mount`增加stdio定位计数及逐次故障注入：首屏不探读页外记录、同页/搜索顺序读，仍用独立目录扫描核对首屏、倒序、字母与搜索；三次定位任一步失败不得发布半页，之后可重建且原READ租约有效。定位次数是软件端口计数，不代表TF事务或耗时。

## 整书索引的软件回归

`book_index`覆盖TXT超过512项的连续编译、256页提交后重开续建、EPUB跨资源、完成表复用、预览取消及确认排版的新旧变体，核对首屏完整像素与释放归零。`font_cache`检查大字宽工作集的度量一致、重复轮廓加载下降及申请失败保留小表；`index_cursor`检查小/满窗口游标查询成本不随边界数线性增长、满窗口256页连续候选、持久游标保留、未提交窗口淘汰后的游标回退和拒绝陈旧完成提交；`device_index_background`/`device_index_wait`使用真实产品控制层检查先正文/先索引顺序。`settings_ui`检查开关持久与忙时拒写，SDL的`book_index_window`检查可取消等待、暖开复用及后台顺序。运行入口仍为`python tools/dev.py host`与`sdl`。`page_index_tf`另以内部512KiB/TF64MiB复现大小文件保留阈值混用，并检查内部仅64KiB时拒写、重开保留已提交表；不删除用户数据。软件回归不代表TF实际掉电、面板响应或真实提速验收。

## 退出阅读的软件回归

`recent`检查已验证快照通知、未变化/写失败/读失败/失效介质及独立读回一致；`recent_idle`在真实TXT/EPUB中检查保存失败/取消不通知、重试成功与释放归零。`device_reading_stats`用真实产品关闭路径核对快照交接后首页不重读日志、媒体代次改变拒用；`device_management`与`shelf_view`分别核对首页缓存键和完整像素的分钟内一致、周余数进位及分钟边界失效。GC16退出扫描、统计同步和保存屏障保留；软件检查不代表退出耗时或面板实测，新批固件构建刷入另按用户确认执行。

## 外部背景专项回归

主机paper用独立生成的原生4位灰度PNG核对全部像素、左右边缘、缩略分页/缓存、缺失源、错误摘要、坏缓存、分配失败，以及TXT/EPUB首次NEXT/PREV、失败回执、挂起恢复和跨进程式重新打开；style覆盖旧schema与新256B schema8往返。device_refresh检查静态灰底不累计、第四次大面积灰阶迁移、未对齐/行位图一致与失败不提交。网页资源测试独立解码全部PNG行预测，检查封面/锁屏/背景的尺寸、灰阶、透明度与失败不上传原图。

可见模拟器新增tools/run_sim.ps1 -BackgroundDir <工作区目录>，缓存写Docker用户卷的独立backgrounds目录；主机也可用PN_BACKGROUND_DIR/PN_BACKGROUND_CACHE指定私有样本和缓存。原书、字体、图片保持只读。窗口/主机计时不能当面板残影或TF性能实测；新批正式构建发布仍需确认。

## Windows 环境

熊猫画面真机观察使用诊断专用指令：先回首页，经既有`device_test.py serve`会话执行`python tools/device_test.py cmd "loading 0"`，随后可发送`loading 1`、`loading 2`、`loading 3`，每帧成功后至少间隔750ms（实际扫描时间另计）；真实开书在首帧成功后100ms即可尝试首次低头，不等待演示完成；中间键返回。只投递给产品owner，不在控制台线程推屏，不开书或改进度；正式镜像不提供该设备入口。原生PNG/串口参考帧用于像素核对，动画观感和残影必须目视观察。正常书籍可能不到动画阈值就已打开，不为演示延长真实开书。

当前仓库 `D:\dev\windssea\pico_nano`，需要真实 Python3、Docker Desktop Linux 引擎；主机 Node 用于工具/安装器测试。Windows 的 python/python3 可能是应用商店别名，先用 `py -0p` 查解释器，本机已验证入口：

```powershell
cd D:\dev\windssea\pico_nano
py -V:Astral/CPython3.12.13 tools/dev.py docs
```

下文通用 `python` 在本机可以替换为 `py -V:Astral/CPython3.12.13`，其他电脑选择自己的实际解释器。开发脚本统一UTF-8输出，不修改全局Python配置。

Docker先确认Linux服务可连接；本机可用context为desktop-linux，仅按实际情况选择当前shell，不持久改变用户配置：

```powershell
docker context ls
docker --context desktop-linux version
$env:DOCKER_CONTEXT='desktop-linux'
```

[tools/dev.py](../tools/dev.py) 固定 IDF 镜像为 `espressif/idf:v6.1@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`；SDL镜像由 [tools/docker](../tools/docker) 构建为 `pico-nano-sdl:dev`。参考仓库只读，不把依赖/波形源整理进自有代码。

离线中式黄历的固定MIT源码及派生历表见`LICENSES/calendar-manifest.json`；`node tools/generate_chinese_calendar.cjs --check`复跑生成物和逐日年度摘要，不联网。主机`chinese_calendar`用同源C查询对照1970..2100全部47,847日，包含闰月、干支、节日和完整宜忌；这不代替设备时间来源、面板显示和真实睡眠验收。

## 日常命令和产物

2026-10-21开发配置使用SDK USB/WiFi共存（`CONFIG_ESP_PHY_ENABLE_USB=y`），不再手动切换USB数据pad；OTA的TLS使用`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`将工作内存放入PSRAM，保留完整证书校验。已有`build-*/sdkconfig`不会自动继承修改后的defaults，复跑相应构建前只同步这两项及Kconfig互斥的内存分配选项，保留板级/CI时序隔离；源码编译门会拒绝旧的USB关闭或TLS内部内存配置。诊断构建不替代正式板级/CI构建；本批用户已另行授权1.0.2正式构建发布及远程OTA测试。

阅读统计检查为主机`reading_stats`、`device_reading_stats`和`bookmark_lock`：前者覆盖门控/跨日/校时/换书归档/重启/坏档保留，第二项用真实产品main和TXT/EPUB验证首屏不加载统计、成功翻页、失败扫描、重绘与存档恢复；硬件接口为替身。SDL新增`reading_stats_window`，通过真实菜单/锁屏/加载延迟旅程与跨进程文件快照核对正文停留、翻页和持久恢复。窗口使用主机时间，执行时间及端口延迟都不是TF、面板或睡眠功耗实测；正式板级/CI和实机诊断按本批授权分开进行。

tools/test_local_system_startup.py的--staged-prepare使用产品同源的源/排版两个阶段；--sources-only在源准备后立即点击，检查0次空闲布局、1次前台布局，完整书/字体身份已就绪。--interrupt-layout --baseline-warm --baseline <独立旧观察器>在实际首屏布局完成时注入新输入取消，再点击；要求旧对象丢弃/新对象保留，点击阶段旧书SHA一次/新零次、完整三帧一致。--expect-layout用于完整候选交接，--paint-unshown继续演练原8节点正文预绘。普通PC仍重查外部修改；固定文件port仅模拟设备READ排他约束。local-samples的原准备矩阵改为分阶段，另加local-idle-source-handoff全量源后点击；中断版本对照需指定独立冻结观察器，不能把同一程序切换策略称为旧固件。hot_host/immutable另在首字度量期间及布局完成后取消，检查对象/菜单缓存保留、失败不提交进度、原帧重试及TTL/USB归还；这些是软件行为，不是TF或面板计时。

`tools/test_local_identity_scratch.py --binary build-host/test_device_identity --output build-dev/local-identity-scratch`复制并完整校验用户`testBook`与`testFont`的全部文件，每份分别在64KiB与1B池预算下比较自管DMA工作区和显式关闭能力后的原借用策略，输出明确标为同程序策略对照。可用`--baseline <独立旧产品观察器>`改为独立版本对照；不将切换同一程序的策略冒称旧固件。该工具已加入`local-samples`。观察真实设备适配器的逻辑fread次数、字节及池分配，PSA/DMA接口为替身，PSA替身暂存整个输入的主机堆在池账本之外；不是TF事务、设备总内存或耗时证据。MOBI/PDF仅参加摘要，不表示支持阅读；原件SHA必须保持。`device_identity`另检查192KiB池中150KiB界面缓存不因多余摘要缓冲被回收，关闭能力的原策略作为对照，并保留内部余量、PSRAM栈与失败退路。

`tools/test_local_menu_probe.py --binary build-host/device_menu_probe`在全新一次性Docker中复制全部用户书籍，逐款把四款用户字体副本装到固定系统字库路径，驱动真实`app_main`首页/设置/传书/书库路由。每款记录冷进入和四轮往返的stdio读量、seek/stat、字体创建与字宽/光栅轮廓加载、池峰值及17张完整4bpp帧；可用`--baseline <旧输出目录>`逐字节对照。最近阅读预览由三本真实文件的元数据种入，不代表阅读位置/内容身份验收；硬件接口为替身，单调时钟记录的是主机经过时间，不能当TF/面板耗时。已加入`local-samples`；`font_cache`另检查五个不同字号的常用字符反复交替后不再加载轮廓，以及原回收/低预算/故障退路。

同一入口加`--cancel-system`在首次完整字库/菜单缓存就绪后，向真实产品`system_font_ready`注入已取消的准备范围，断言退出后仍保留源/UI/池占用和READ租约；随后的同路径重试不得打开字体、读字节或新建引擎，继续核对原17张菜单帧。`local-samples`分别运行普通及取消两组，后者输出`build-dev/local-menu-cancel`。`font_identity`另查公开打开接口在入口/源验证中取消及替换请求，`device_management`在四个检查点取消，保留原失卡、坏字体及显式交接检查。这些是软件回归，不是人为取消状态下的实机速度；旧基线必须由含同一观察探针的原产品源码独立冻结，不能用新行为改名作旧版。

`--system-failure missing|corrupt`另用每款用户字体的缺失或改坏首4B的私有副本，核对反复菜单不再读字体、末轮不创建引擎，原17帧与独立旧探针对照。第18帧是恢复正常字体后的书库，可传`--recovered-baseline <普通菜单输出>`与其`16-library.raw`直接比较；逐字体交替检查显式资源释放和媒体代次变化后的重试，再检查租约配额/写者占用、一次IO错误和OOM不阻止后续加载。恢复副本只在一次性容器端口夹具中持WRITE租约写入，不是上传事务验收；重挂同一份私有书库时明确保留菜单预览种子，不把它当真实阅读记录。`local-samples`增加独立`local-menu-missing`/`local-menu-corrupt`两组；每次须全新容器，不能在同一容器连续复用固定`/sdcard`和`/data`目录。

`epub_cover_page`检查独页封面竖图/横图/极长图/方图的原图比例、双向居中与实际像素边界，另检正文夹图/配文/多图/徽标及坏图占位。`tools/test_local_cover_fit.py --capture build-host/epub_app_capture`用用户全部EPUB和实际霞鹜文楷逐本捕获前三页；可传`--baseline <修改前捕获器>`逐完整帧、页脚和源位置对照。该入口已加入`local-samples`，原件只读，窗口与主机结果不代替面板验收。

0.6.15的封面重绑检查为`cover_resident`，以1/2/13个公开边界样本对照原缓存路径、随机子集/环形换序、继续阅读重复、来源变化、取消及USB；不替代实际书籍。`tools/test_local_cover_resident.py --binary build-host/cover_resident_validate`扫描全部用户EPUB/TXT副本，每13本一组，比较41次完整帧并统计真实PNCV文件读取。`tools/test_local_cover_priority.py --binary build-host/cover_priority_validate --rebind-before-write`在尚未落盘时换序、返回首页再回书库，对照最终PNCV完整字节与唯一写入次数。两者已纳入`local-samples`，原件SHA须保持；端口读取计数和主机画面不是实卡或面板计时。

0.6.14的`device_async`覆盖真实输入线程对UP占槽的两次空触点和终点投递；`tools/test_local_device_shelf.py --binary build-host/device_shelf_validate`用用户整库副本驱动真实产品翻页路由，覆盖三列/四列/列表、只有DOWN/UP的快速滑动、略斜横滑、普通空报告、垂直/多点取消及首末页。固定`/sdcard`路径仅在全新一次性Docker中创建，脚本不接设备或映射TF；已纳入默认Docker `local-samples`，原生运行须改用该容器入口。SDK替身与窗口不能代替实际触控验收。

0.6.13追加七项`device_sleep_*`检查及`tools/test_local_lock.py --binary build-host/device_lock_validate`（已接入`local-samples`），用用户全部EPUB/TXT与四款字体的副本驱动真实产品锁屏/唤醒路由，每组检查三次锁解锁、完整像素/位置、旧电源事件清理、恢复扫描失败后原位重试及关闭池归零。睡眠适配器的GPIO/PMU/SDK接口为替身；可见模拟器只用于观察锁页/正文，不模拟实际低功耗或硬件唤醒，不能把两者代替真机验收。

0.6.12追加`text_inspect`、`shelf_gesture`及`shelf_gesture_window`专项；`tools/test_local_text_inspect.py --binary build-host/test_text_inspect`只读核查用户三本TXT的一遍读取、独立逐字符检查点与Python完整SHA，`tools/test_local_shelf.py --sim build-sim-sdl-idf/pn_sim`核查用户全书库三/四列、左右滑动、封面冷暖完整字节与菜单往返；两者已纳入`local-samples`。这些文件读取/CPU计数不代替真实TF、触控和面板计时。

`text`增加缓存内/跨块的UTF8最短形式、代理区和Unicode上下界，使用1～17字节及256字节短读检查字符/范围与损坏时输出保持。`text_inspect`另记录仅摘要和已知身份后仅探测的CPU时间，仍用独立逐字符检查点对照；比较构建须注明优化/消毒器，不能把主机时间当设备时间。`tools/test_local_text_decode.py --sim build-sim-sdl-idf/pn_sim [--baseline <旧窗口程序>]`在三本用户TXT×四字体副本上比较打开、两次向后和一次向前的48张正文帧/源位置、反向页像素及关闭池归零；已加入`local-samples`。窗口退出会先保存回书库，脚本明确排除最后的书库帧。

语义持久缓存的同源检查为`semantic_snapshot`（事件/结构校验）、`semantic_file`（真实文件/配额/租约）、`semantic_disk`（真实关闭重开、短章/长段、整书SHA、完整帧/位置、损坏/空间不足/2MiB及同路径换内容），窗口旅程为`semantic_cache_window`（两进程、SHA保留、命中不再正文建模、损坏重建和工具菜单抑制）。缓存目录是内部状态root下`semantic`，不在TF上传目录；这些是软件检查，不是面板首显/稳定计时。

`semantic_steps`观察真实文件端口每次最多一次4KiB写入，并把flush/fsync/close/rename分成不同调用；覆盖暂停续写、部分写后放弃、写入/同步/替换失败和低空间/介质失效时旧包保持。`semantic_disk`另检查实际阅读器的新输入、重绘、热挂起及关闭优先释放暂存，安静后可重试；BUSY不能当作已存。底层系统调用的真实耗时不由这些软件检查证明。

`semantic_snapshot`新增真实章节分槽跨会话加载、读/CRC/解码各边界取消、32个加载/解码分配点单次故障、旧单槽兼容和坏尾退路；逐有效字段核对语义，不把C结构padding当作事件内容。`semantic_file`同时覆盖同书多章共享原配额及每步4KiB读取，`source_cache`逐字节核对512/1024/2048/4096供数窗、跨界、大读取、回收和错误。

`semantic_snapshot`另固定分步前版本1载荷摘要，逐准备阶段检查取消后重试、旧包未被提前交付、pin/回收及前台热复放、存储忙/端口身份变化、低内存、跨4096字节字符串和介质变化；完整事件复放与原源对照。`resource`检查基路径/片段/合并路径取消仍是`PN_CANCELLED`且输出不变，坏UTF-8仍拒绝。细阶段计时为`epub.snapshot.count/alloc/zero/header/records/crc/header-crc/offer`，设备需诊断配置，主机需Timing；这些不代替真实面板和最长系统调用测量。

`xhtml_pass`将可重置冷解析与原双遍通用sink逐事件、样式、位置和全章统计对照，观察XML次数和章节供数字节；覆盖超过128KiB的流式章、后置样式隐藏无效资源、多个前置head、小页完成仍拒绝坏尾部/CRC、隐藏/重复锚点及输出保持、稀疏检查点旧值、内存回退、取消和失卡。XML供数是软件计数，不是TF物理事务；仍须可见窗口与真实面板验收。

`xhtml_compat`用内存片段检查词法边界、分块、声明/CDATA/实体、截断根、输入错误/取消、逐分配失败及嵌套XML诊断。综合入口追加`tools/test_local_compat.py`，只从用户原书提取实际失败章，独立构造修正副本并对照1/17/4096字节短读、整本事件摘要、实际字体全帧/后续分页位置、受限池、v1/v2缓存重开及坏CRC/缺失结束根不出半帧；结果在`build-dev/local-compat/`。正常版本1包继续可用，成功容错的章单独写版本2，不能把其接管暂存当落盘。首次异常源多一遍检查，主机结果不代表设备响应或全格式兼容。

`cover_steps`用真实文件观察封面缓存每次最多4KiB写入，逐正常阶段中断后验证原像素重试不再解码；另覆盖取消暂停、低余量、来源变化、部分写后失卡、写入/同步/替换失败、模拟FAT不可覆盖的备份读取/回滚和零租约。`cover_window`检查真实窗口循环的冷提取/重开缓存像素一致、细步骤、键盘输入期间取消/重试及退出。CRC分块与系统调用计数不是时间保证；真实FAT掉电、最大同步/关闭耗时和面板响应另验。

POSIX的`cover_window`持久化场景等待三条实际`cover.cache-clean`回执，再由SDL信号处理转成正常QUIT；测试显式启用其信号处理，要求退出成功与池归零。30秒仅为卡死看门狗，不把1500ms脚本等待当作全部fsync完成上限；原同步/清理次数、三包落盘、缓存重开整帧及中断暂存检查保持。非POSIX沿原脚本退出，不以主机文件系统时序推导设备指标。

| 命令 | 产物/行为 | 证明什么 |
| --- | --- | --- |
| `python tools/dev.py docs` | 检查项目Markdown链接/代码块、分区与vendor/字体摘要 | 文档引用/复用来源有效 |
| `python tools/dev.py host` | build-host；同源C、ASan/UBSan、真实文件/HTTP、SDK-stub及Python/Node工具测试 | 主机软件行为，非真实无线/面板 |
| `python tools/dev.py sim` | build-sim-idf、build-sim/artifacts | headless PGM与调度场景 |
| `python tools/dev.py sdl` | build-sim-sdl-idf；窗口程序及dummy交互回归 | SDL自动旅程，不自动打开可见窗口 |
| `python tools/dev.py sdl-run` | build-sim-sdl-run，Release；启动可见窗口 | 人工观察同源界面，非设备性能 |
| `python tools/dev.py firmware-ci` | build-ci-s3，独立sdkconfig.ci | CI/保守时序交叉编译与size |
| `python tools/dev.py firmware-board` | build-board，独立sdkconfig.defaults | 已核对Zbit120MHz板级编译与size |
| `python tools/dev.py firmware-diag` | build-diag，sdkconfig.defaults叠加sdkconfig.diag，版本带`-diag` | 串口测试控制台诊断镜像；禁止打包发布 |
| `python tools/dev.py factory-data` | build-dev/factory-data.bin、factory-wallpaper.bin | 生成首装空镜像；不写设备，不用于默认升级 |
| `python tools/dev.py installer` | 计划测试+build-dev/installer-site | 生成本地站点；不发布、不连接设备 |
| `python tools/dev.py local-samples` | build-dev/local-samples/report.json | 本机私人书/字体独立验证，不作为公共CI依赖 |

命令日志在 `build-dev/logs/`。相同构建目录串行，不并发启动两个Ninja；执行未结束时读现有会话/日志，不因观察超时反复重启任务。`--native`可使用本地安装工具，仍要记录实际工具链及依赖版本。

设备路由测试使用固定`/sdcard`、`/data`作为硬件替身的存储根。Docker默认在一次性容器里提供；GitHub临时Linux runner在CTest前以`sudo install -d -o "$(id -u)" -g "$(id -g)" /sdcard /data`建立当前runner可写目录，测试本身以普通用户运行，不跳过断言。原生运行须使用专用、无实际设备挂载的隔离环境，不能把产品TF或已有数据目录交给这些夹具。

SDKCONFIG按环境隔离：CI与板级都有各自build目录/sdkconfig，不能拷贝CI配置覆盖板级或把本机生成sdkconfig提交成基线。120MHz只适用于核对器件。当前无线适配要求 `CONFIG_ESP_PHY_ENABLE_USB=y`；检查defaults及实际build配置，不据缓存旧构建宣布新源码已编译。

## 开发时保持可见模拟器

其他电脑首次拉取源码，不需要私人测试书/完整字库也可运行原创演示库：先`python tools/dev.py sdl-run`构建，再`powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_sim.ps1 -Demo`。依赖Docker Desktop Linux与WSLg；Windows可按上文用`py -V:Astral/CPython3.12.13`代替python。演示输入在忽略的build-dev/demo-library，状态独立保存到Docker卷的/data/demo-samples；不扫描私人目录。已有窗口会保留，重建后先`docker stop pico-sim`再启动。默认不加-Demo仍使用本机tests/testBook与tests/testFont，只操作独立副本。

缓存基础专项为`cache_file`、`page_index`、`wallpaper_cache`、`paper`、`reader_app`和`epub_app`。覆盖缺目录/坏主包有效备份、每个提交步骤中断、失卡后不清理重挂载路径、原生像素，以及索引满盘时真实进度成功保存、正常挂起/恢复；取消不是缓存MISS。主机为真实临时文件/SDK替身，不代表FAT掉电或面板验收。

TF增量索引随上述可见启动器默认启用，用户/演示缓存分别保存在Docker卷`/data/user-samples/reading`与`/data/demo-samples/reading`。可传`-NoReadingCache`切回内部窗口作策略对照，旧缓存/用户位置保留；手动运行程序时用`PN_READING_CACHE=<可重建目录>`启用，空值使用内部退路。不要将模拟器目录映射为实际设备TF卷或原件目录。

主机`page_index_tf`检查1800个边界跨512项RAM窗口、版本/布局隔离、整表SHA、重算CRC伪造、摘要读后取消续建、可信根先提交而TF未发布时中断、孤立大暂存恢复、命名空间GC/根配额与失卡归还。`reader`另检查解码检查点不能充当上一页页首。SDL`tf_index_window`用原创TXT、文字EPUB和图片EPUB，比较冷/暖、PREV/NEXT、坏/缺/不可写缓存共48对完整原生帧；这是同程序后端策略对照，非旧版或真机耗时。GitHub源码CI新增独立SDL任务，完整窗口回归可在其他电脑复跑。

`tools/test_local_tf_index.py --capture build-host/epub_app_capture --baseline <独立旧版capture> --output build-dev/local-tf-index`逐本检查私人EPUB前三页；可用`--books`和`--font`指定本地样本，原件只读。候选通过`PN_EPUB_INDEX_IDLE`显式推进真实延期写入，再跨进程重开，比较旧版/候选冷页/候选暖页的完整像素、位置、原池上限与关闭归零。报告中的host_total_ns包含开书/翻页/落盘/关闭及工具开销，不能称物理开书耗时；没有baseline时仅作同程序冷/暖对照，--limit明确为子集。

```powershell
python tools/dev.py sdl-run
powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_sim.ps1
```

第一条构建并启动；第二条可直接调用窗口启动脚本。源码重建后，先确认并停止旧 `pico-sim` 再重新启动加载新二进制；脚本会保留已运行窗口，不自动将它替换成新源码。关闭窗口也会结束对应容器。依赖WSLg；`docker logs pico-sim`查看输出，每次真实呈现PGM在 `build-dev/sim-frames/`，状态默认保存在 `pico-sim-data` Docker卷。

默认窗口显示阅读首页，B书库、H首页、C继续；书库方向键聚焦、Enter打开，PageUp/Down翻库页。管理页数字1/2/3是左返回/中首页/右确认；阅读S排版、P预览、Enter应用/Esc取消，L锁页预览与返回。脚本输入可用 `-Script 'settings,back,fonts,back,wallpaper'`；无quit时回放后仍保持可操作。

局部按下反馈开发演练可传`tools/run_sim.ps1 -Feedback`（`PN_SIM_FEEDBACK=1`）。脚本`press:X:Y,wait:MS,move:X:Y,release:X:Y,focus-lost`覆盖保持/滑出/失焦，反馈PGM以`feedback-`前缀保存，不改变普通帧序号；`PN_SIM_CLOCK`只为自动对照固定状态带。设备资格开关`CONFIG_PN_UI_FEEDBACK`默认关闭；可见PC反馈不证明FOLLOW DU光学效果或真实扫描时间，资格镜像仍按本批构建授权与面板验证流程处理。

反馈定稿演练可用`press:100:600,move:100:850,release:100:850,wait:750`，日志`settle=1`及`feedback-*-settle.pgm`是空闲维护回执；`PN_SIM_SETTLE_FAIL_ONCE=1`仅用于软件失败/重试对照。320ms安静等待与1秒重试不代表面板耗时，设备仍须验证新输入到达定稿前/中的延迟与灰区恢复。

字体连续列表软件演练传`tools/run_sim.ps1 -Follow -FontDir <工作区内至少7款字体的目录>`（`PN_SIM_FOLLOW=1`），例如`-Script 'library,fonts,press:400:820,move:400:520,wait:180,move:400:350,release:400:350,wait:1500'`。管理首页键脚本为`key-home`，`home`仅代表字母H，不能用来验证管理三键路由。`PN_SIM_FOLLOW_TRACE=1`记录阶段/offset/回执/代次；`PN_SIM_FOLLOW_CAPTURE=1`配合FRAME_DIR保存实际窗口像素，`PN_SIM_FOLLOW_FAIL_ONCE=1`仅模拟首次逐相失败。有效字体、跨页惯性、框外保持、故障恢复、失焦/导航和低预算由`font_scroll_window`检查；不能据窗口速度宣称墨水屏流畅，实际SDK构建及光学资格仍待完成。

设备列表已绑定独立`CONFIG_PN_UI_FOLLOW`资格入口，默认关闭，可与`CONFIG_PN_UI_FEEDBACK`独立组合；两套sdkconfig基线未启用。`device_list`及`device_list_no_feedback`编译真实main资格路径和原波形，以SDK替身覆盖VCOM门、归一、原相位、后缓冲、灰阶定稿、失败/欠载和实际UI退出；`device_list_boot_*`另检启动失败不注册/不推屏。`device_toc`和`device_toc_no_feedback`另用原创54项EPUB验证真实目录绑定、半程退出、排空确认及失败GC恢复；`toc_motion`检查跨页/部分行命中、分组/失败跳转、尾部当前项和2MiB退路。它们不是实际IDF交叉编译或光学验收。新批构建发布按当批用户授权执行，资格镜像和默认发布镜像明确区分。

目录连续滚动在书库打开EPUB后用同一`-Follow`入口，脚本例如`library,enter,toc,press:400:900,move:400:500,wait:180,move:400:250,release:400:250,wait:1800`。`toc_scroll_window`使用原创48章/54项目录，检查真实事件、跨页、框外保持、灰阶定稿、原位置重开、按下反馈、失焦/失败恢复和低预算。独立`--book`窗口目前保留旧分页；主产品与`--library`才绑定该资格路径。

书架焦点演练使用`library,wait:800,toc-down,toc-down,toc-up`，其中`toc-down/up`只是SDL方向键脚本别名，不要求已打开目录。`shelf_focus`逐字节对照三/四列、四种风格、列表/首页、真实灰阶缩略图及缺图卡；`shelf_focus_window`验证真实键盘事件、焦点往返像素、外置补字、选书阅读及返回。`device_management`另检实际设备底图/三键、取消/失败/代次和状态带恢复；SDK仍为替身。这些测试不证明面板光学流畅度。

本机综合测试与可见模拟器默认扫描用户提供的 `tests/testBook/` 和 `tests/testFont/`，新增文件在下次运行时自动纳入；不生成或混入示例书。`tools/run_sim.ps1`先用`tools/prepare_local_samples.py`复制到被忽略的`build-dev/user-samples/<随机ID>/`，记录原件SHA，阅读器删除/覆盖只作用于副本。原件目录已被Git忽略，私人书籍和完整字库不得加入公共fixture、固件或发布包。修改源码后重建并重启窗口，新增资源也须重启以加载新副本。

可见启动器将这批新副本另挂到固定`/samples`：程序使用`/samples/books`、`/samples/fonts`，宿主副本仍每次独立。用户样本状态/缓存位于`/data/user-samples/{state,covers,wallpaper}`，与旧生成样本分开，旧记录保留。这样重启不会只因随机副本目录变化而丢封面缓存/字体路径；未更改书籍和mtime时复用已有缓存，新增/变化文件仍须正常校验或重新提取。首次从旧`/work/build-dev/...`路径切换需要生成一次新键。

开发时可传`tools/run_sim.ps1 -CoverCacheNamespace qa-20261015`使用独立`/data/user-samples/covers-qa-20261015`目录，观察封面冷提取；同命名空间后续运行可检查重开缓存。名称只接受小写字母/数字和连字符，最长48字符；不删除默认缓存或改变阅读状态目录。仍须先停止旧窗口再启动；主机文件系统和窗口耗时不能代替实卡/面板测量。

`python tools/dev.py local-samples`按实际EPUB/TXT依次跑解压/出版物与封面缓存、字体设置、mimetype兼容/损坏拒绝、内存、跨进程阅读缓存、不支持格式和逐章正文解析；某个样本矩阵失败仍完成其余矩阵，最终非零退出，不能把开书成功当全书兼容。报告分别在`build-dev/local-samples/`、`local-font-settings/`、`local-mimetype/`、`local-memory/`、`local-cache/`、`local-unsupported/`、`local-chapters/`。

该入口也运行`tools/test_local_publication.py`，用同目录真实EPUB比较同步/分步的全部元数据与spine关联，逐步取消/恢复/关闭、介质换代，并在元信息最小的一本逐个注入分配失败；副本另检container/OPF坏CRC、截断和内部DTD拒绝。结果在`build-dev/local-publication/`；`xml`单元检查交错池、4KiB推进、源尾错误和分配清理，不替代真实输入到面板回执计时。

`tools/test_local_zip_job.py --binary build-host/zip_job_validate`也已接入综合入口，使用真实EPUB独立副本比较原同步打开与可恢复打开的全部目录/资源字节，检查每步读取配额、1/17字节短读、预取消及读取后取消继续、失效源/IO、目录快照不准入；最小样本覆盖每步放弃和逐分配失败，退出池须归零。报告在`build-dev/local-zip-job/`，FD源接口读量不是TF物理事务或整机内存。`zip_job_oracle`复跑原Python zipfile解包oracle；`zip_job`另用原始ZIP边界片段覆盖空归档/最大尾注、超过128KiB目录及大量扩展字段、ZIP64拒绝、同哈希不同名及重复名，不能作为真实书籍体验替代。

`tools/test_local_cover_priority.py`用真实EPUB/TXT每六本分组，比对同步与产品分步路径的完整缩略图/元数据/PNCV字节，检查所有可见结论完成前没有缓存write/fsync、继续阅读重复槽不重复解析、写入暂存放弃后不重解码。报告`build-dev/local-cover-priority/`；此处“可见完成”是可绘制状态，实际显示回执仍由设备/SDL控制层确认。

该工具可传`--baseline <旧cover_priority_validate>`比较同源副本的新旧缓存完整字节，记录文件端口调用与源缓存峰值；`--deny-source-cache`压低父池可选分配余量，并用独立可回收前台对象检查零驱逐、直接读取退路及像素/元数据一致。可选`--compression-probe`保留历史zlib/RLE尺寸探针，默认不执行，也不改变产品格式。端口计数/主机编码大小不能证明实卡提速。`crc32`单元以独立逐位oracle/标准向量核查任意初值与分块，`semantic_snapshot`另在每个载荷CRC块后注入取消，检查无事件/统计发布、无源XML回退及随后精确命中。

`tools/test_local_semantic_probe.py`从真实EPUB生成实际语义缓存，核查跨章新文件缺失为0载荷读取、旧单槽不匹配仅152B包头读取，且不申请完整包、命中完整包字节不变、正文事件一致、坏头/坏载荷回退、低内存/取消/失效介质清理；额外覆盖用户刀剑样本的容错章。可选`--baseline <旧epub_app_capture>`比较同书/字体/状态的首帧、跨章帧和缓存包。报告`build-dev/local-semantic-probe/`，文件端口计数不等于TF总线实际读量或响应时间。

字体设置逐本核查管理全局默认、四款实际字体的单书覆盖、全局变化保留覆盖、恢复继承、取消预览及显式系统字体；副本数字前缀固定行次序，避免把Python码点顺序误当产品拼音顺序。字体探针以整组正文码点并集检查五种字号和灰阶/二值绘制，单字体缺字数量与引擎状态分开，探针通过不等于全字覆盖；当前用户样本未含完整鸿蒙系统字库，不能据此关闭其删除保护/全字补字的实际资源验收。

内存矩阵在1/2/4/6MiB池检查普通字体，在6MiB追加全部用户字体，逐阶段保留pool begin/end/peak/attempts、九类内存及六类缓存账本；每类peak不能相加当同时峰值。除prepare结果也记录实际开书/翻页/返回状态，拒绝损坏/IO误报，单场景超时保留日志并继续其余矩阵。关闭used/live必须归零；低预算明确OOM属于退路结果，不能计为成功阅读，报告另列reading_opened、controlled_oom以及默认6MiB是否全部打开。主机的SDK heap/stack不可用；设备诊断`CONFIG_PN_PROFILE`另记录内部/PSRAM空闲、最低水位、最大连续块及任务栈，仍须采集真机日志。

跨进程缓存核对整帧、完整书源SHA、语义包和分页索引、坏包重建；百分比跳转后先翻页往返清除会话临时返回入口，避免把页脚状态差异误判为正文缓存差异。TXT不使用EPUB语义包，单独记录`.pni`，不能将`model-load`缺席当TXT无缓存。显式指定其他私人清单时可传给`tools/test_local_samples.py --manifest`，例如：

```json
{"files":[{"kind":"book","path":"tests/本机样本.epub"},{"kind":"font","path":"tests/本机字体.TTF"}]}
```

原件只读，仅复制到隔离目录。`tools/run_sim.ps1`可指定 `-Books/-Font/-FontDir/-WallpaperDir` 和 `-Timing`，容器输入须在工作区内；未指定壁纸目录时用空目录。模拟器时区Asia/Shanghai，电量为演示值，锁页预览不模拟PMU功耗。预算上限6MiB，低预算可降，不可扩大掩盖设备故障。已有公共小型边界fixture仅用于单元回归，不能替代用户资源的综合测试和可见验收。

`root_navigation_queue_window`使用既有公开边界文本，分别对四根页和反馈开/关注入扫描中第二次底栏接触，核对最终整帧与串行点击一致，并验证旧内容区触点仍拒绝。`PN_ROOT_RACE_ARTIFACTS`可保留全部日志与画面；未设置时，首次最终帧差异会先复制产物到模拟器构建目录内的独立`artifacts/root-queue-failure-*`目录并打印路径，容器退出后仍保留。第二点击及密集导航像素测试均固定容量显示输入，完整帧/次数/pending断言保留；`race-root:延迟毫秒:起点X:起点Y:终点X:终点Y`在延迟后释放起点并向终点投递独立点击，仅最终松手确认脚本。`PN_SIM_CAPTURE_CANCELLED=1`仅供竞争诊断捕获已成功显示、随后因新输入取消准备的帧；默认仍跳过这类开发截图，不改变成功显示回执。模拟等待与公开边界样本不代替真实用户整库/字体及面板验收。

`root_navigation_coalesce_window`另查四根页、反馈开/关的密集独立点击：`race-roots:延迟毫秒:首目标:后续目标1:后续目标2:末目标`使用0～3底栏编号，首目标扫描中投递另三次完整接触，核对最终完整帧与串行末目标相同，积压三目标只准备最后一个。`race-focus:延迟毫秒:起点X:起点Y:移动X:移动Y`注入失焦后保持左键的移动，最后才松手，不能把同一持有变成新点击。`PN_ROOT_COALESCE_ARTIFACTS`保留原始日志/帧；扫描中注入须在日志确认pending，不能只凭脚本字符串断言竞态发生。空闲检查须独立配置`--cover-cache`启用索引，冷缓存、释放后等待才要求建索引，不能对未初始化模块要求成功日志。共享`async`及实际`device_management`另查批次上限、后续取消、优先电源项、同一持有、多点/滑入和最终空样本的释放。

`tools/test_local_root_navigation.py --sim build-sim-sdl-idf/pn_sim`已接`local-samples`，重新复制全部用户书籍/四款字体，逐字体比较密集点击和失焦持有的最终整帧、关闭池归零及释放后空闲索引。可选`--baseline`只接受含同样竞争脚本入口的独立冻结程序；原件SHA保持，记录旧/新导航尝试次数，不能把次数直接换算为面板扫描减少或真实速度。开发工具中的700ms等待只制造积压窗口，不是产品延迟或面板实测。

## 真机串口自动测试（诊断镜像）

`python tools/dev.py firmware-diag`构建`build-diag`，在板级默认上开启`CONFIG_PN_TEST_CONSOLE`与`CONFIG_PN_PROFILE`，应用版本为`<版本>-diag`，设置页可见；产品/发布构建关闭性能计时且不编译该控制台，钩子为空。指令语法与合成时间线在`components/pn_ui/pn_test_console.c`，设备与模拟器共用；设备在输入任务的触摸采样层用合成采样替代一次读数，因此三键、阅读手势和底栏导航仍走产品原代码，但不覆盖触控芯片I2C/固件本身。截屏在显示任务两job之间读取epdiy已显示参考（后缓冲），回传board布局4bpp并逐行CRC，`known=0`表示参考未知。每个显示job完成后上报`PNT 0 display kind start end ms status`，时间为设备esp_timer毫秒。

| 指令（每行`<seq> <verb> ...`） | 作用 |
| --- | --- |
| `ping` / `version` / `status` | 连通、版本与ELF摘要、view/输入原子计数（不遍历堆） |
| `heap` | 堆最大块/最低水位与任务栈；遍历堆会屏蔽中断，只在两显示job之间执行，扫描中执行曾在真机引起欠载 |
| `tap X Y [ms]`、`long X Y [ms]`、`swipe X0 Y0 X1 Y1 [ms]` | 合成触点；Y可到1399以覆盖屏下键区 |
| `key left\|middle\|right [long]` | 屏下三键（中心80/240/400，Y=1340） |
| `power` | 合成电源短按；锁屏后唤醒仍须实体键 |
| `shot` | 回传当前已显示画面 |

刷写只写应用分区：`build-dev/device-venv/Scripts/python.exe -m esptool --chip esp32s3 -p COM3 write-flash 0x20000 build-diag/pico_nano.bin`（venv由`py -m venv build-dev/device-venv`并安装esptool创建）。打开串口会触发一次USB复位，所以`tools/device_test.py serve --port COM3`常驻持有端口并记录`build-dev/device-test/<时间>/serial.log`与`results.jsonl`；其他终端用`tools/device_test.py cmd "tap 85 1150" --brief`、`shot out.png`、`tail`。手势返回首个/最后显示job相对合成首采样的时间及期间产品计时日志。`sdkconfig.diag`另开`CONFIG_PN_DIAG_NO_AUTO_LOCK`（不自动锁屏，电源键锁屏保留）与`CONFIG_PN_DIAG_NO_LOCK_SLEEP`（锁屏只轮询不浅睡，`power`指令也可唤醒，硬件睡眠/唤醒须在非诊断镜像另测）。控制台启动后常持禁浅睡锁（产品默认空闲自动浅睡会让USB串口停止响应），并把产品日志改为整行非阻塞写入驱动、电脑不读时直接丢弃（驱动VFS逐字符50ms超时会拖住产品任务），所以诊断镜像不能用于功耗测量。进入传书无线或USB传书会断开串口；串口日志本身改变时序，结果是诊断镜像上的近似值，不等于产品镜像、光学稳定或功耗验收。模拟器脚本可用`pnt:<指令>`复用同一语法。

## EPUB 与性能诊断

`tools/test_local_foreground_open.py --binary build-host/device_menu_probe --scenarios --output build-dev/local-foreground-open`已纳入`local-samples`，在全新一次性Docker中复制全部用户书籍/字体，逐款系统字体驱动真实`start_reader`与`tick_opening`的普通EPUB冷开。要求加载后尚无可阅读app、逐轮完成且唯一正文回执，记录完整OPEN/NEXT/PREVIOUS三帧及池；八种场景检查目录阶段返回、摘要后返回、协作取消、锁屏准备、失卡、USB租约归还、首帧显示失败和错误内容身份，失败不得写最近阅读。默认只验证当前行为并保留画面，不能称新旧像素一致；传`--baseline <独立旧产品观察器>`才逐字节比较完整原始帧。测试端口显式报告已挂载的TF，只访问副本，不模拟实际卡吞吐/面板/低功耗；普通文件port保留外部改写检查。`--limit`只作明确子集，实际整库和低预算/设备峰值应分别报告。

`tools/test_local_zip_job.py --binary build-host/zip_job_validate --scan-prepare --output build-dev/local-zip-scan-job`在用户全部EPUB副本上，先以4096B/8读配额准备中央目录，再顺序喂入原书并完整finish；与原同步归档逐目录、逐资源字节对照，覆盖目录1/17字节短读、取消/重试、可选快照拒绝、失效源/IO及最小样本每步放弃/分配失败。配额只指目录job，后续顺序供数/收尾单独执行，不能当整个开书耗时保证。默认完整ZIP job模式保持，两模式均加入`local-samples`；`zip_scan_job_oracle`和`zip_job`另核对独立zipfile解包、坏头/重叠/同哈希/重复名状态。`test_local_open_steps.py --baseline-partial --baseline <独立旧探针>`可比较两版均先完成64KiB摘要再点击，明确区别于旧冷开基线。

`tools/test_local_open_steps.py --binary build-host/font_system_startup_probe --output build-dev/local-open-steps`检查用户全部EPUB×四款字体×正文/补字：真实首页任务先完成64KiB摘要，注入一次取消，再点击接续原任务，核对剩余摘要字节、完整身份、OPEN/NEXT/PREVIOUS三张原始帧及关闭池归零。可传`--baseline <独立冻结旧探针>`作跨版本冷开对照；默认明确标为同程序冷开/部分准备策略对照。全部原件SHA保持；书SHA启动次数为零不等于点击没有剩余摘要工作，报告单列`click.work_bytes`。`tools/test_local_identity_jobs.py --binary build-host/test_device_identity --output build-dev/local-identity-jobs`另对全部用户书籍和字体比较原同步与分步设备适配器的摘要、逻辑fread数及每步DMA归还；可传独立`--baseline`，PSA/DMA是替身且PSA主机堆不计入池，不能用池峰称设备总RAM。这两个命令已加入`local-samples`。真实`device_menu_probe --prepare-books`区分唯一正文回执和部分EPUB接管前的一次加载反馈，旧报告`one_present`仍兼容。

本机综合入口也运行`tools/test_local_middle_key.py --sim build-sim-sdl-idf/pn_sim`与`tools/test_local_font_samples.py --binary build-host/font_samples_validate`，使用用户原书/字库的独立副本，检查阅读中间键、排版保存和关闭池归零，以及字体列表首帧不打开外置小样、每轮最多一个与合并呈现。字体工具可加`--baseline <旧探针>`比较完整最终像素；未给旧工具时只报告本版行为及重绘一致，不当作跨版本像素对照。结果位于`build-dev/local-middle-key/`和`local-font-samples/`。公共`catalog_mount`只将挂载根`mkdir`替换为IDF6.1 FatFS的`EINVAL`，其余千本索引、真实文件、重开/取消/分配失败与原测试相同，不能代替实卡掉电或响应计时。

`font_startup`编译真实TXT/EPUB入口，检查外置补字引用沿同一个完整校验句柄进入排版键，该补字文件仅一次完整摘要；另覆盖缺失、同大小替换、主字体损坏、取消、失卡和显示失败的释放。`tools/test_local_fallback_startup.py --binary build-host/font_startup_probe`已接入`local-samples`，使用全部用户EPUB/TXT和TTF的独立副本，记录真实文件端口字节、完整摘要次数、画面摘要与池峰；可选`--baseline`对照修改前冻结探针。它不测物理TF或面板时间。阅读线的整帧SDL对照固定`PN_SIM_CLOCK=12:34`，保留全部像素判据，避免跨分钟HUD变化误报取消失败。

`font_startup_probe`另记录冷开至首屏的原书stdio打开/读取/字节/定位。`tools/test_local_fallback_startup.py --binary <新探针> --baseline <旧探针> --compare-io`比较两个均只做一次完整摘要的冻结探针，检查字体读取保持、原书读取/定位不增及全部原始帧逐字节一致；不带该选项时保留原双扫描补字优化的基线判据。原件仍只读、使用独立书/字体和状态副本，软件文件计数不代替TF/面板计时。

`identity_reuse_host/immutable`核查同路径、媒体、代次、READ资格、取消、旧句柄关闭及PC同大小外部改写；`font_identity_immutable`另确认已加载系统源不能覆盖所选引用的不匹配摘要/长度。`tools/test_local_system_startup.py --binary build-host/font_system_startup_probe --expect-reuse --output build-dev/local-system-startup`逐本逐字体检查系统源已加载后，选择该同路径为正文或补字的打开/NEXT/PREVIOUS。`font_system_startup_probe`仅将文件port切至`PN_TEST_IMMUTABLE_FILE`来复现设备源不可被外部写入的约束，其余阅读代码与主机同源；这不是SDK/TF/面板实测。传`--baseline <独立冻结旧探针>`逐完整帧、原书读取及后续翻页IO对照，要求恰好少读完整字体一次、峰值不增。普通`font_startup_probe`不加`--expect-reuse`另跑同一矩阵，要求保留完整字体核验；两组已接入`local-samples`。`--limit`只供开发子集，报告明确标记，不能当整库通过。

同一工具加`--warm-prepare --expect-reuse`检查未显示准备：记录空闲完整SHA/读取与0次状态写，点击阶段书/字体SHA及打开均0，再核对原始OPEN/NEXT/PREVIOUS全帧。没有`--baseline`时明确以同一个当前程序的普通冷开作策略对照；可另传独立冻结旧冷探针，不能把同程序冷/暖结果称作跨版本实测。`hot_host/immutable`增加未显示所有权、错误身份/路径/格式/策略、首次显示失败无记录、准备/接管取消、24个早期分配故障、菜单cache owner保护、低预算、已有root、TTL不反复重建、USB和媒体失效，显式启动字体的完整身份也在空闲补齐。测试已有公开边界样本只用于契约，用户整库矩阵仍使用原件副本。

继续加`--baseline-warm --expect-layout --baseline <独立旧探针>`可比较两版同样提前开书时的首屏交接：新版本空闲阶段真实FIRST/JUMP排版恰一次，点击不再次prepare；独立旧程序仍在点击排版。探针记录idle/click布局次数，`PN_FONT_STARTUP_PROFILE=1`输出阶段时间与池；这是主机测量，不能作TF或面板时间。`hot_host/immutable`检查重复准备不重排、布局完成后输入取消、非首屏保存后整帧恢复、显式回书首丢弃候选，以及接管后失卡不得呈现；读取核心prepared回执不代表显示成功。

再加`--paint-unshown`推进首屏正文分步绘制，独立旧基线保持仅提前排版。探针完整统计空闲绘制的原书/字体读取与FT_Load_Glyph（含RENDER标记），点击命中计数及三张全帧；不把绘制过程排除在idle计数之外。图片页保持前台渲染，预算不足允许停止可选准备并原路绘制。`hot_host/immutable`检查每步至多8节点、部分像素/取消回退、12个正文阶段分配故障后的完整帧回退、混合字号与两种阅读线、非首屏恢复/回书首及池策略/pin归还。

`test_local_menu_probe.py --prepare-books`在原17帧之后追加真实`app_main`的TXT/EPUB准备、1500ms门槛、菜单无额外字体IO、锁屏释放及一次正常OPEN显示，四实际字体均退出池/租约归零；不把内存中构造的已识别Continue选择当作已有阅读记录。此模式每款字体清空一次性容器的私有`/data/progress`，避免上一款实际OPEN写出的记录改变下一款冷菜单；原件和默认记录不动。`prepare_window`另检查真实SDL进程重开的首页空闲准备、冷开最终全帧及设置释放，固定窗口等待后必须看到真实ready/正文绘制完成/take日志，不以经过时间代替准备成功。这些入口已纳入`local-samples`/窗口回归，不替代面板计时。

像素测试可用`PN_SIM_STORAGE`固定传书页/关于页的容量显示，与`PN_SIM_CLOCK`同为显式显示输入；磁盘写入空间准入仍用真实statvfs，不受该值影响。密集导航专项固定此输入并保留全帧对照，以免测试自身或并行产物改变可用量；普通可见启动不设置它。前次未保留的差异帧仍不能据此追溯断言其原因。

新版探针另输出首屏后NEXT/PREVIOUS的`turn_io`，与冷开计数分开；`--compare-io`在前后探针均提供该行时也检查翻页原书IO不增及字体读取保持。旧探针缺少此行时不推断其翻页IO。

实体中间键在阅读中短按回主页、长按600ms打开阅读工具栏；排版/字体页短按保存成功才回原书。SDL用数字2或`key-home`模拟短按，用F10或`key-middle-long`模拟长按；此脚本别名不代替真实按住/释放资格测试。

```powershell
python tools/dev.py epub-sample --archive "build-dev/sim-library/piconano/books/05-章节目录与书签.epub" --all-resources
python tools/dev.py profile --archive "build-dev/sim-library/piconano/books/05-章节目录与书签.epub" --rounds 30
```

profile每次生成 `build-dev/profile/<随机ID>/` 隔离状态与summary.json；保留源书SHA、轮数、预算、后端、冷/热条件、失败计数和各阶段p50/p95/max。普通软件显示回调不测面板，OS缓存通常未受控。相同书/字体/布局/工具链对照，不能修改原书后宣称同样本提速。

真实设备控制器的主机诊断可在独立一次性容器运行`tools/test_local_foreground_open.py --binary build-host/device_menu_probe --output build-dev/foreground-profile --prepare-first`，并给容器传入`PN_OPEN_PROFILE=1`。夹具复制用户样本，比较加载期间提前准备后的完整首屏/翻页；`--baseline`可指定另一独立二进制做逐字节对照。`--scenarios`另覆盖取消、锁屏、换卡、USB、显示失败、错身份和多书切换。容器需全新`/sdcard`、`/data`，不要对用户挂载目录运行夹具。原`profile`入口仍调用同步开书，不能代替这个真实产品job入口。

设备诊断关注`epub.open.request/loading/first-frame`及`full-wall/trusted-wall/wait/max-step`，具体分步为`directory/identity/zip-finish/publication/metadata/state/fonts/sources`。2026-10-19本地诊断将`content.sha-read/update`和`storage.file-read/seek`改为阶段边界`diag_io`累计：calls为调用数、total_us为非重叠同类调用之和、max_us为最长单次、non_ok保留异常；空任务不输出，阶段不再输出重复堆/栈快照，按需在显示job间用heap命令采样。SHA读取回调可能含ZIP feed，不能把整个回调都当读卡时间；`idle.*`观察后台预算。墙钟、最长单步与嵌套子阶段不相加，IO汇总窗口可能包含随后的空闲校验；`diag_io`不能直接当逐次延迟分布。开书IO快照是本job累计值，汇总工具的`max_io`取最大值而非相加；截断/拼接记录排除，指定输出时旁存`.invalid.json`并报告损失数量，不能当完整日志。发布配置不输出新增高频诊断。首屏完整校验与图像质量保持，软件显示回执不是面板光学稳定；输入/显示使用esp_timer，不能将ESP日志前缀的绝对毫秒与其混算。

主机 `-Timing`/profile记录分类账本及阶段局部同时峰值；分类峰值不相加，嵌套耗时不重复相加。设备可单独用 `CONFIG_PN_PROFILE=y` 做诊断，但不自动刷机，默认发布配置关闭。诊断日志有测量成本；记录是否开启，池外堆和任务栈必须设备独立测量。

普通固件保留开书/SHA/页准备、显示reason、dirty/convert/power/scan/remember、queue/wait/receipt和关键保存分段；用来定位慢开书/翻页，不能只看总Present。热页软件和真实光学/电源性能分别比较，参考成绩及局限见[架构末节](ARCHITECTURE.md#性能利用与阅读优化)。

## 验证与开发收口

0.9 OTA源码的主机专项为 `ctest --test-dir build-host -R '^update' --output-on-failure`（`update`同源事务、`update_boot`真实启动端口的SDK替身、`update_ui`同源设置页）；默认`python tools/dev.py host`包含它们。`test_package_ota.py`用真实密码库覆盖逐字节篡改及错误密钥，`test_sync_gitee_release.py`另验证签名OTA附件与三个ZIP内的应用一致。固定IDF镜像已带cryptography48.0.1，原生Python需先 `python -m pip install -r tools/requirements-ota.txt`；未安装时不把签名测试跳过当验证成功。设备PSA/TLS及真正双槽/断电不能由主机替身替代。

国内分发工具独立复跑为 `python -m unittest discover -s tests/tools -p test_sync_gitee_release.py -v` 和 `python tools/sync_gitee_release.py --tag v0.8.0`；后者默认仅验证现有包，不连接设备、不上传。真实同步、匿名下载核验和 GitHub Actions 凭据配置见[安装与发布](INSTALLATION.md#gitee-国内分发)。这类分发/文档变更无需重新构建固件或重启产品模拟器。

`gfx_spans`以修改前逐点距离/混合为独立像素oracle，覆盖6000组裁剪/奇偶行距/灰阶/分数描边，另含实际屏幕大小卡片、退化半径与极端坐标；比较包括相邻半字节、行尾和边界保护。`settings_ui`检查兼容网络名快照更新不再提交相同画面。它们验证绘制等价与重复工作消除，不代表ESP32、TF或面板响应时间。

`settings_font`检查同池引擎移交、连续12次设置热打开零字体创建、冷/热完整像素一致、分配/取消/错池/呈现失败、压力回收、被违规填满的归还槽和最终零资源；`device_management`实际设备控制层另检查设置借用期间源交接和取消仍保留原页。`tools/test_local_settings_font.py --binary build-host/test_settings_font`已接入`local-samples`，逐款扫描用户TTF，独立复制后作为TF界面源检查，原件SHA保持，结果默认`build-dev/local-settings-font/`。这验证借用/缓存和像素，不把任意测试字体当真实鸿蒙系统资源验收。

`device_refresh`直接编译实际设备统计源码，检查全部65536组前后字节、八位计数的256种掩码、8192组混合像素字、阈值前后各一点、1/2/3字节未对齐及页脚/空/小脏区，并与冻结逐像素规则比较完整方案。无转白资格时传NULL目标抓意外读取。管理、列表/目录及无反馈资格入口仍用同一设备函数回归；这些是软件判断/内存访问验证，不代替面板扫描、残影或TF计时。

2026-10-14完整刷入批次仅改安装器/打包与文档，固件基于已发布0.6.6阅读修复源码。独立工作区从该基线完成两套IDF6.1构建/size、factory-data与22项Node/5项包Python；板级应用5026336B、20.11%槽余。既有149主机/32SDL为前批固件证据，本批没有重跑或重建产品可见界面。包、站点和离线ZIP逐文件SHA一致不代表设备迁移成功，实际备份恢复、刷入、复位与启动另验。

文档重组只跑文档/引用/归档校验及受影响工具测试，不为可逆文档修改重建固件。源码修改选择有意义的相关主机回归；阶段完成再全量host、SDL自动旅程和可见窗口。需要发行时按用户授权跑两套SDK构建/size，随后按[安装与发布](INSTALLATION.md)核对包与站点。没有刷入的构建不能称真机修复。

基线时间要写清：已发布 `0.6.5+766a137` 经146CTest/10Python/5Node、31SDL、17项安装器计划测试及两套固件构建。本页计数是历史证据，下一批以实际输出为准，不把“计数一致”当验收条件。

逐项真机验证方案、样本与长期矩阵见[开发指南](DEVELOPMENT_PLAN.md)。错误/失败/取消、资源关闭used/live=0、像素/位置一致性都需保留，不能把失败样本从统计中删掉。

USB界面可见演练程序为SDL构建的`pn_usb_preview`：空格切换准备/等待/连接/停止/失败/完成六态。它复用产品绘制，不枚举USB或读写TF。`device_usb`验证100次独占交接，`device_usb_board`用API替身编译真实板级出口，验证活动回调、延期写、PHY失败及重试；均不能替代真实电脑与设备测试。
0.6.6本批软件门：149项CTest、10项Python、5项网页与32项SDL通过；目录说明可见检查后修复为四行独立绘制，并补跑USB/版本/管理窗口三项。板级接口替身和真实USB、共享渲染和面板光学分别记录。


`root_navigation_window`以现有公开文本边界样本检查无松手已显示新页、跨页持有/滑出、内容滑入和孤立松手拒绝、四页往返完整像素、子页/失焦、两种反馈配置，以及扫描中释放后下一点击可用。`tap`检查共享按下所有权、多点/无效/两个空样本重置，`device_management`实际main检查底栏资格和过期空样本只释放不选择。脚本`close-window`发送真实窗口关闭（`quit`仍是返回键）；`race-nav:延迟毫秒:X:Y`在呈现中注入松手，仅用于软件竞争验证。它们不代替用户书库旅程、可见窗口或面板测速。


0.6.16的`cover_resident`边界与同名用户整库工具追加冷bind无封面文件读取/来源stat、逐轮最多一张PNCV、取消与USB读写隔离、最终像素及元数据对照；常驻封面命中仍复核来源。大JPEG窗口测试等待包含800ms解码安静期，不能把等待窗口长度当主机提取耗时或TF面板性能。`device_async`追加64次无恢复间隔的代次确认，睡眠适配覆盖11种通知/电平/共享中断/错误场景，用户书籍锁屏每书每字体16轮并检查扫描期间新旧电源事件；设备书库旅程追加每布局64次主页锁解锁。硬件均为端口替身，不能据此宣称真实短按成功率或待机功耗已验收。

2026-10-20首屏输入交接检查：`async`验证唯一暂存、确认前不交付/不取消、失败/换view/紧急导航丢弃、晚释放竞态；`device_async`以真实输入线程及SDK替身覆盖旧触点隔离、新点按去重、跨首屏确认的手势和左/右键语义。`recent_idle`用同源TXT/EPUB确认坏首屏不记历史、首屏/NEXT无近期记录写入、空闲延时/合并、IO失败限频/取消重试及退出保存屏障；`reader_recent`继续核对挂起/关闭前的真实记录。设备诊断的`Reader input ready t=`是首屏与模型均提交后的时刻，预备阶段的status body=1只代表可采集新手势，不能单独当首屏已确认；配合实际display回执与Reader action计数验证一次输入一次翻页。诊断合成触点仍不替代实体触控、光学或真实睡眠验收。

2026-10-20资源精简回归：`style`使用真实A/B日志逐个读取旧schema7 ID1..8，核对只有移除款的背景解析为无、其余非默认字段一致且日志序号/载荷不变，越界及坏版本继续拒绝。`builtin_art/reading_art`检查仅两个封面、ID0/4/7顺序、原生图/状态栏行带及旧ID白底，保留原图SHA。`style_ui`覆盖持久ID命令与画廊空位拒绝；SDL `reading_background_window`追加两种格式全部旧ID跨进程恢复、非默认排版及原日志字节不变。主机图片与冻结版本对照不是板级固件体积、面板或冷开耗时实测，正式构建发布须本批新确认。
