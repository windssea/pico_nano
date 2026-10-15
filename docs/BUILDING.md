# PicoNano 构建、运行与验证

更新：2026-10-14。这里只记录现有可复跑入口；当前实现/未完工作分别见[架构](ARCHITECTURE.md)和[开发指南](DEVELOPMENT_PLAN.md)。固件构建、串口写入与真机验收是不同阶段，默认工具命令不烧录。

## Windows 环境

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

## 日常命令和产物

0.6.12追加`text_inspect`、`shelf_gesture`及`shelf_gesture_window`专项；`tools/test_local_text_inspect.py --binary build-host/test_text_inspect`只读核查用户三本TXT的一遍读取、独立逐字符检查点与Python完整SHA，`tools/test_local_shelf.py --sim build-sim-sdl-idf/pn_sim`核查用户全书库三/四列、左右滑动、封面冷暖完整字节与菜单往返；两者已纳入`local-samples`。这些文件读取/CPU计数不代替真实TF、触控和面板计时。

语义持久缓存的同源检查为`semantic_snapshot`（事件/结构校验）、`semantic_file`（真实文件/配额/租约）、`semantic_disk`（真实关闭重开、短章/长段、整书SHA、完整帧/位置、损坏/空间不足/2MiB及同路径换内容），窗口旅程为`semantic_cache_window`（两进程、SHA保留、命中不再正文建模、损坏重建和工具菜单抑制）。缓存目录是内部状态root下`semantic`，不在TF上传目录；这些是软件检查，不是面板首显/稳定计时。

`semantic_steps`观察真实文件端口每次最多一次4KiB写入，并把flush/fsync/close/rename分成不同调用；覆盖暂停续写、部分写后放弃、写入/同步/替换失败和低空间/介质失效时旧包保持。`semantic_disk`另检查实际阅读器的新输入、重绘、热挂起及关闭优先释放暂存，安静后可重试；BUSY不能当作已存。底层系统调用的真实耗时不由这些软件检查证明。

`semantic_snapshot`另固定分步前版本1载荷摘要，逐准备阶段检查取消后重试、旧包未被提前交付、pin/回收及前台热复放、存储忙/端口身份变化、低内存、跨4096字节字符串和介质变化；完整事件复放与原源对照。`resource`检查基路径/片段/合并路径取消仍是`PN_CANCELLED`且输出不变，坏UTF-8仍拒绝。细阶段计时为`epub.snapshot.count/alloc/zero/header/records/crc/header-crc/offer`，设备需诊断配置，主机需Timing；这些不代替真实面板和最长系统调用测量。

`xhtml_pass`将可重置冷解析与原双遍通用sink逐事件、样式、位置和全章统计对照，观察XML次数和章节供数字节；覆盖超过128KiB的流式章、后置样式隐藏无效资源、多个前置head、小页完成仍拒绝坏尾部/CRC、隐藏/重复锚点及输出保持、稀疏检查点旧值、内存回退、取消和失卡。XML供数是软件计数，不是TF物理事务；仍须可见窗口与真实面板验收。

`xhtml_compat`用内存片段检查词法边界、分块、声明/CDATA/实体、截断根、输入错误/取消、逐分配失败及嵌套XML诊断。综合入口追加`tools/test_local_compat.py`，只从用户原书提取实际失败章，独立构造修正副本并对照1/17/4096字节短读、整本事件摘要、实际字体全帧/后续分页位置、受限池、v1/v2缓存重开及坏CRC/缺失结束根不出半帧；结果在`build-dev/local-compat/`。正常版本1包继续可用，成功容错的章单独写版本2，不能把其接管暂存当落盘。首次异常源多一遍检查，主机结果不代表设备响应或全格式兼容。

`cover_steps`用真实文件观察封面缓存每次最多4KiB写入，逐正常阶段中断后验证原像素重试不再解码；另覆盖取消暂停、低余量、来源变化、部分写后失卡、写入/同步/替换失败、模拟FAT不可覆盖的备份读取/回滚和零租约。`cover_window`检查真实窗口循环的冷提取/重开缓存像素一致、细步骤、键盘输入期间取消/重试及退出。CRC分块与系统调用计数不是时间保证；真实FAT掉电、最大同步/关闭耗时和面板响应另验。

| 命令 | 产物/行为 | 证明什么 |
| --- | --- | --- |
| `python tools/dev.py docs` | 检查项目Markdown链接/代码块、分区与vendor/字体摘要 | 文档引用/复用来源有效 |
| `python tools/dev.py host` | build-host；同源C、ASan/UBSan、真实文件/HTTP、SDK-stub及Python/Node工具测试 | 主机软件行为，非真实无线/面板 |
| `python tools/dev.py sim` | build-sim-idf、build-sim/artifacts | headless PGM与调度场景 |
| `python tools/dev.py sdl` | build-sim-sdl-idf；窗口程序及dummy交互回归 | SDL自动旅程，不自动打开可见窗口 |
| `python tools/dev.py sdl-run` | build-sim-sdl-run，Release；启动可见窗口 | 人工观察同源界面，非设备性能 |
| `python tools/dev.py firmware-ci` | build-ci-s3，独立sdkconfig.ci | CI/保守时序交叉编译与size |
| `python tools/dev.py firmware-board` | build-board，独立sdkconfig.defaults | 已核对Zbit120MHz板级编译与size |
| `python tools/dev.py factory-data` | build-dev/factory-data.bin、factory-wallpaper.bin | 生成首装空镜像；不写设备，不用于默认升级 |
| `python tools/dev.py installer` | 计划测试+build-dev/installer-site | 生成本地站点；不发布、不连接设备 |
| `python tools/dev.py local-samples` | build-dev/local-samples/report.json | 本机私人书/字体独立验证，不作为公共CI依赖 |

命令日志在 `build-dev/logs/`。相同构建目录串行，不并发启动两个Ninja；执行未结束时读现有会话/日志，不因观察超时反复重启任务。`--native`可使用本地安装工具，仍要记录实际工具链及依赖版本。

SDKCONFIG按环境隔离：CI与板级都有各自build目录/sdkconfig，不能拷贝CI配置覆盖板级或把本机生成sdkconfig提交成基线。120MHz只适用于核对器件。无线互斥适配拒绝保留旧 `CONFIG_ESP_PHY_ENABLE_USB=y`；检查defaults及实际build配置，不据缓存旧构建宣布新源码已编译。

## 开发时保持可见模拟器

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

`tools/test_local_semantic_probe.py`从真实EPUB生成实际语义缓存，核查跨章未命中仅152B包头读取且不申请完整包、命中完整包字节不变、正文事件一致、坏头/坏载荷回退、低内存/取消/失效介质清理；额外覆盖用户刀剑样本的容错章。可选`--baseline <旧epub_app_capture>`比较同书/字体/状态的首帧、跨章帧和缓存包。报告`build-dev/local-semantic-probe/`，文件端口计数不等于TF总线实际读量或响应时间。

字体设置逐本核查管理全局默认、四款实际字体的单书覆盖、全局变化保留覆盖、恢复继承、取消预览及显式系统字体；副本数字前缀固定行次序，避免把Python码点顺序误当产品拼音顺序。字体探针以整组正文码点并集检查五种字号和灰阶/二值绘制，单字体缺字数量与引擎状态分开，探针通过不等于全字覆盖；当前用户样本未含完整鸿蒙系统字库，不能据此关闭其删除保护/全字补字的实际资源验收。

内存矩阵在1/2/4/6MiB池检查普通字体，在6MiB追加全部用户字体，逐阶段保留pool begin/end/peak/attempts、九类内存及六类缓存账本；每类peak不能相加当同时峰值。除prepare结果也记录实际开书/翻页/返回状态，拒绝损坏/IO误报，单场景超时保留日志并继续其余矩阵。关闭used/live必须归零；低预算明确OOM属于退路结果，不能计为成功阅读，报告另列reading_opened、controlled_oom以及默认6MiB是否全部打开。主机的SDK heap/stack不可用；设备诊断`CONFIG_PN_PROFILE`另记录内部/PSRAM空闲、最低水位、最大连续块及任务栈，仍须采集真机日志。

跨进程缓存核对整帧、完整书源SHA、语义包和分页索引、坏包重建；百分比跳转后先翻页往返清除会话临时返回入口，避免把页脚状态差异误判为正文缓存差异。TXT不使用EPUB语义包，单独记录`.pni`，不能将`model-load`缺席当TXT无缓存。显式指定其他私人清单时可传给`tools/test_local_samples.py --manifest`，例如：

```json
{"files":[{"kind":"book","path":"tests/本机样本.epub"},{"kind":"font","path":"tests/本机字体.TTF"}]}
```

原件只读，仅复制到隔离目录。`tools/run_sim.ps1`可指定 `-Books/-Font/-FontDir/-WallpaperDir` 和 `-Timing`，容器输入须在工作区内；未指定壁纸目录时用空目录。模拟器时区Asia/Shanghai，电量为演示值，锁页预览不模拟PMU功耗。预算上限6MiB，低预算可降，不可扩大掩盖设备故障。已有公共小型边界fixture仅用于单元回归，不能替代用户资源的综合测试和可见验收。

## EPUB 与性能诊断

本机综合入口也运行`tools/test_local_middle_key.py --sim build-sim-sdl-idf/pn_sim`与`tools/test_local_font_samples.py --binary build-host/font_samples_validate`，使用用户原书/字库的独立副本，检查阅读中间键、排版保存和关闭池归零，以及字体列表首帧不打开外置小样、每轮最多一个与合并呈现。字体工具可加`--baseline <旧探针>`比较完整最终像素；未给旧工具时只报告本版行为及重绘一致，不当作跨版本像素对照。结果位于`build-dev/local-middle-key/`和`local-font-samples/`。公共`catalog_mount`只将挂载根`mkdir`替换为IDF6.1 FatFS的`EINVAL`，其余千本索引、真实文件、重开/取消/分配失败与原测试相同，不能代替实卡掉电或响应计时。

实体中间键在阅读中短按回主页、长按600ms打开阅读工具栏；排版/字体页短按保存成功才回原书。SDL用数字2或`key-home`模拟短按，用F10或`key-middle-long`模拟长按；此脚本别名不代替真实按住/释放资格测试。

```powershell
python tools/dev.py epub-sample --archive "build-dev/sim-library/piconano/books/05-章节目录与书签.epub" --all-resources
python tools/dev.py profile --archive "build-dev/sim-library/piconano/books/05-章节目录与书签.epub" --rounds 30
```

profile每次生成 `build-dev/profile/<随机ID>/` 隔离状态与summary.json；保留源书SHA、轮数、预算、后端、冷/热条件、失败计数和各阶段p50/p95/max。普通软件显示回调不测面板，OS缓存通常未受控。相同书/字体/布局/工具链对照，不能修改原书后宣称同样本提速。

主机 `-Timing`/profile记录分类账本及阶段局部同时峰值；分类峰值不相加，嵌套耗时不重复相加。设备可单独用 `CONFIG_PN_PROFILE=y` 做诊断，但不自动刷机，默认发布配置关闭。诊断日志有测量成本；记录是否开启，池外堆和任务栈必须设备独立测量。

普通固件保留开书/SHA/页准备、显示reason、dirty/convert/power/scan/remember、queue/wait/receipt和关键保存分段；用来定位慢开书/翻页，不能只看总Present。热页软件和真实光学/电源性能分别比较，参考成绩及局限见[架构末节](ARCHITECTURE.md#性能利用与阅读优化)。

## 验证与开发收口

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
