> **历史归档（2026-10-14）**：本文的状态和方案只作历史证据，当前规格、实现与后续工作以[现行文档](../../README.md)为准。

# 构建与运行入口

当前开发版本0.0.57。能力与待办统一见 [项目状态](PROJECT_STATUS.md)，环境、测试分层和证据边界见 [开发与测试](DEVELOPMENT_AND_TESTING.md)。本页只列已经存在的运行入口，不把拟建命令当成实现。

2026-10-12本机W00入口已复跑：当前工作区`D:/dev/windssea/pico_nano`，Docker `desktop-linux`可连接Linux引擎。Windows的`python`/`python3`为应用商店别名；先用`py -0p`查看真实解释器，本机稳定入口为`py -V:Astral/CPython3.12.13 tools/dev.py ACTION`。其他主机按自己的真实解释器执行，不修改全局Python或Docker配置。

新版窗口默认进入阅读首页；键盘B进入书库、H回首页、C继续阅读，脚本对应`library/home/continue`。书库上/下显示轻量焦点，回车打开；PageUp/Down分页。四栏导航可直接点击，阅读返回保持进入来源。

管理页数字1/2/3对应左返回、中首页、右确认，脚本为`key-left/key-home/key-confirm`。阅读弹层仍可用方向键选择当前项；旧测试脚本`font-primary/font-fallback`使用独立功能键事件，不占用管理三键。重建后重启可见窗口加载新代码。

## 开发时保留可见模拟器

界面开发与本地调试时先启动窗口，观察同源代码的显示效果：

```powershell
py -V:Astral/CPython3.12.13 tools/dev.py sdl-run
powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_sim.ps1
```

默认使用忽略目录build-dev/sim-library/piconano/books：4本TXT（UTF8/BOM/UTF16/GBK）与4本原创EPUB（EPUB2/3、目录、插图、长书名），本地原小说存在时只补缺失副本。默认正文使用内置阅读字体，不要求mockDoc。fonts目录提供带OFL许可证的阅读子集样本，wallpapers目录提供原生尺寸透明PNG几何样本；原创文字/图案为CC0-1.0。manifest.json记录来源与SHA，已有本机修改保留且不误标为原创许可。私人小说不随仓库交付；窗口操作只作用副本。

可单独用真实Python运行tools/make_sim_library.py补齐样本，复跑不覆盖已有文件；启动脚本用SDL容器内Python生成，不受Windows别名影响。可用-Books/-Font/-FontDir/-WallpaperDir指定工作区内输入，支持工作区内绝对路径并转换为容器路径。默认字体/图片目录分别为build-dev/sim-library/piconano/fonts、wallpapers；-Font为空时使用内置默认字体。脚本保留已运行的pico-sim窗口，不自动关闭；源码重建后执行docker stop pico-sim再启动加载新二进制，关闭窗口也会结束容器。

开发观察可加`-Script 'settings,back,fonts,back,wallpaper'`，以真实SDL窗口回放输入；不写quit时脚本结束后窗口继续可操作。书架/正文按L预览已保存锁屏，L/回车/Esc返回最近在读书；预览使用共用A/B读取与绘制，先保存并关闭正文，不模拟PMU睡眠或功耗。窗口时区为Asia/Shanghai，电量仍为演示值。

逐本打开/翻页/返回的本地复跑（Docker Linux）：

```powershell
docker run --rm --mount type=bind,source=D:\dev\windssea\pico_nano,target=/work -w /work pico-nano-sdl:dev python3 tools/test_sim_library.py build-sim-sdl-run/pn_sim build-dev/sim-library/piconano/books build-dev/sim-library/piconano/fonts/ReadPicoSample.ttf
```

本机样本可在忽略文件 `build-dev/sim-library/local-inputs.json` 中配置，格式如下；`path` 可用仓库相对路径，资源按固定类型复制到 books/fonts，原件只读，复跑保留已有副本：

```json
{"files":[{"kind":"book","path":"tests/本机样本.epub"},{"kind":"font","path":"tests/本机字体.TTF"}]}
```

启动脚本自动读取这份本机配置，也可用 `tools/make_sim_library.py --local-inputs PATH` 明确指定。私人书籍与字体只在本机运行，不嵌入固件，不成为公共CI依赖；manifest记录来源、摘要及本机许可边界。`--font-dir` 同时供字体管理和阅读选择页使用，与已保存的正文字体所在目录无关。

单独复跑本机资源：`py -V:Astral/CPython3.12.13 tools/dev.py local-samples`。入口构建同源字体探针/EPUB容器及SDL程序，比较EPUB全部资源，按正文码点检查不同字号的灰阶/二值字形，并逐字体验证目录、跳转、翻页和续读；报告存于忽略目录 `build-dev/local-samples/report.json`。缺字计数是该样本的主字体覆盖诊断，不能当全字库或面板清晰度验收。可见窗口仍须另行观察。

默认6MiB阅读预算不扩大。帧备份只供正文关闭工具栏时恢复，书架/设置不分配，返回书架时释放；避免大渐进式JPEG首帧和缩略图被无用备份挤占。打开失败会在书架提示，具体状态与阶段可用 docker logs pico-sim 查看。大型图片仍有预算限制，PC结果不证明设备可解码所有大图。

依赖 Docker Linux 引擎与 WSLg；以 docker version 实际可连的 context 为准。2026-10-11本机 desktop-linux 可用，default 管道不可用，可在当前Shell设置 $env:DOCKER_CONTEXT='desktop-linux'，不改变全局配置。日志用 docker logs pico-sim 查看；每次呈现的PGM在忽略目录 build-dev/sim-frames，阅读状态与封面缓存在 pico-sim-data Docker卷中。dummy自动测试与可见窗口分开，窗口观感不等于真实面板观感。

## Windows与Docker

在当前工作区根目录执行，需要真实Python 3与Docker Desktop Linux引擎。固定工具镜像：`espressif/idf:v6.1@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`。本机完整py标签见页首；下面通用python写法适用于PATH中已有真实解释器的环境。

```powershell
python tools/dev.py docs
python tools/dev.py host
python tools/dev.py sim
python tools/dev.py sdl
python tools/dev.py firmware-ci
python tools/dev.py firmware-board
```

命令均不烧写。host执行同源C、ASan/UBSan、真实文件/HTTP、SDK-stub和工具测试；sim为基础headless捕获与调度场景；sdl构建可选窗口模拟器并通过dummy驱动测试交互。Windows Docker里的dummy测试不打开可见窗口。

若Docker连接失败，先读 `docker context ls`、`docker desktop status`，再用 `docker --context desktop-linux version` 或 `docker --context default version` 核对各管道。仅在确认context指向可用Linux引擎后，给当前Shell设置（本次desktop-linux可用）：

```powershell
$env:DOCKER_CONTEXT='desktop-linux'
python tools/dev.py host
```

这是当前Shell选择，不持久修改用户配置。不因观察超时重启构建或引擎；依据进程/会话的实际状态判断。

## 产物与SDK隔离

在另一台电脑升级已安装 PicoNano 的当前阅读器，见 [异机烧录步骤](FLASH_ON_ANOTHER_PC.md)。该专用升级包核对设备旧分区表后只写应用，不能用默认完整烧写计划替代。

后续默认Web交付：先提交固件源码，完成板级build/size和软件回归，再运行 `python tools/package_upgrade.py --reference releases/pico_nano-0.0.57-c696890-upgrade/expected-partition-table.bin`，随后 `python tools/dev.py installer` 验证和生成站点。升级清单以源码提交及构建时间区分0.0.57开发构建，最新构建默认排在前面；本机旧SDKCONFIG若仍启用ESP_PHY_ENABLE_USB，须按新版defaults关闭，该适配编译时拒绝保留旧选项。CI配置仍独立。

### 内存与阶段耗时诊断

`tools/run_sim.ps1 -Timing`保留可见窗口，输出`profile`阶段行及`memory`分类行。分类字段为当前字节/累计峰值/成功分配/失败分配；各类峰值不可相加为同时峰值。阶段记录begin/end/本阶段同时peak/请求数，嵌套阶段耗时不可相加，主机池外堆与栈明确不可用。设备可单独选择`CONFIG_PN_PROFILE=y`编译诊断，默认关闭；此选项不自动刷机，也不把真实堆数据替换为PC估算。

同源软件准备测量入口（TXT/EPUB原件只读，状态按轮独立，显示回调只用于软件诊断；另行观察可见窗口）：

```powershell
py -V:Astral/CPython3.12.13 tools/dev.py profile --archive "build-dev/sim-library/piconano/books/05-章节目录与书签.epub" --rounds 30
py -V:Astral/CPython3.12.13 tools/summarize_profile.py build-dev/logs/profile-2.log
```

每次在忽略的`build-dev/profile/<随机ID>`创建状态与summary.json，记录书籍SHA、轮数、预算、ASan/UBSan、新会话/OS缓存条件、backend、各阶段p50/p95/max及所有状态计数。非零/EMPTY/取消不被过滤，不能把没有工作当成读取失败；日志在`build-dev/logs/profile-*.log`。相同目录构建须串行，不能两个Ninja同时写同一archive。先明确同源构建/字体/样本和冷/热条件，硬件与主机分别比较。控制要求与首批证据见[内存与响应](MEMORY_AND_RESPONSE.md)。

| 入口 | 目录 | 用途 |
| --- | --- | --- |
| host | build-host | 同源C可执行测试及原生HTTP worker |
| sim | build-sim-idf、build-sim/artifacts | 基础PGM捕获 |
| sdl | build-sim-sdl-idf | SDL交互测试/窗口程序 |
| firmware-ci | build-ci-s3 | sdkconfig.ci，默认时序编译验证 |
| firmware-board | build-board | sdkconfig.defaults，真机Zbit 120MHz配置 |
| factory-data | build-dev/factory-data.bin、factory-wallpaper.bin | 单独生成内部数据与壁纸分区空镜像，不自动烧写 |
| docs | build-dev/logs/docs-0.log | 文档/分区/依赖与字体摘要核对 |
| installer | build-dev/installer-site | 网页安装器计划测试（Node）与静态站点，不连接设备；见 [网页安装器](WEB_INSTALLER.md) |

两SDKCONFIG独立生成，不能把CI时序当产品默认。产物、日志、样本与截图都忽略；必要UI字体、测试fixture、许可证与依赖清单保留。Windows不能直接执行Linux ELF，容器绝对路径也不能直接当Windows烧写路径。

## Linux/WSL可见模拟器

需要C11编译器、CMake、Ninja、Python 3；窗口另需SDL2开发包和图形会话（例如WSLg）。

```bash
cmake -S tests/host -B build-host-native -G Ninja -DPN_SANITIZERS=ON
cmake --build build-host-native
ctest --test-dir build-host-native --output-on-failure
cmake -S simulator -B build-sim-native -G Ninja -DPN_SIM_SDL=ON -DPN_SANITIZERS=ON
cmake --build build-sim-native
./build-sim-native/pn_sim --library mockDoc --font mockDoc/LXGWWenKai-Regular.ttf --state-dir build-dev/sim-state
```

书架封面上点右键等同设备上的长按（打开书籍操作面板），窗口脚本用 `rtap:X:Y`；状态带时间取本机时间。书架右上的电量在PC上取 `PN_SIM_BATTERY`（0–100，默认85，仅演示值，负数则不显示）。审查界面时可设环境变量 `PN_SIM_FRAME_DIR=DIR`，书库窗口每次呈现另存一张 `frame-NNNN.pgm`（只是开发辅助，不影响显示）。

异步验证使用书库入口，窗口线程和产品 worker 分离。`tools/run_sim.ps1 -Timing -PresentDelayMs 600` 可在可见窗口加入人工显示等待，验证等待时仍接收输入；默认等待为0。`burst:40:next`、`race-next-home:300`、`race-next-tools:300` 是竞争测试脚本，行为与限制见 [异步阅读](ASYNC_READING.md)。人工等待及PC结果不能当面板性能。

书库窗口可加 `--cover-cache DIR` 把封面缩略图缓存到指定目录；不给时只解码不写盘，见 [书架封面](COVERS.md)；`--wallpaper-dir DIR [--wallpaper-store DIR]` 启用壁纸设置页（书架按W），见 [锁屏与壁纸缓存](LOCK_SCREEN.md)；`--font-dir DIR` 启用字体管理页（书架按F），见 [字体管理](FONT_MANAGEMENT.md)；书架按P进入设置页，见 [翻页与三键](PAGE_TURN.md)；按J打开字母跳转。mockDoc为用户本机样本，Git不提供；先自行放入合法测试文件。单书用 `--book PATH`，EPUB入口按扩展名分派。`--headless --capture FILE.pgm`、`--budget BYTES`、`--scenario ownership`等以simulator/main.c为准；没有通用JSON脚本CLI。窗口脚本测试使用测试专用环境变量，不能当真实用户交互验收。

## 功能入口与样本

- 阅读/续读/目录/书签/排版/字体：见 [阅读会话](READER_SESSION.md)、[EPUB应用](EPUB_APP.md)、[阅读状态](READING_STATE.md)。
- PC传书网页：见 [网页服务](TRANSFER_WEB.md)，使用独立测试书库，不能与阅读模拟器同时访问同一根。
- LocalSend PC HTTP：见 [开发入口](LOCALSEND_PC.md)，本机控制台确认，独立测试根；尚未验证官方客户端或设备互通。
- 附近传书界面预览：见 [中文确认与状态](LOCALSEND_UI.md)，`tools/run_localsend_preview.ps1` 打开独立可点击SDL窗口；无网络或文件安装。
- 设备热点/退出返回：见 [设备入口](DEVICE_TRANSFER_ENTRY.md)；扫码及独立解码见 [扫码传书](TRANSFER_QR.md)。
- EPUB容器全资源检查：`python tools/dev.py epub-sample --archive "mockDoc/绍宋.epub" --all-resources`，只读原文件，报告留build-dev。
- 字体子集/fixture：tools/subset_ui_font.py、tools/subset_fallback_fixture.py，源哈希必须匹配；不能手改字体或表。

主设备启动阅读与书架，热点已装配；仍未真机验证。未取得有效PMU VCOM不推屏，不擦NVS、不自动格式化，不写SY7636A标定。当前设备呈现固定25°C工程值，温度/波形/功耗还需实测。

## 缓存压力复跑

`tools/run_sim.ps1 -Timing -BudgetKiB 2048`以2MiB池启动可见窗口；默认6144KiB，限制范围256–6144KiB，过小预算可能无法容纳必需帧或字体。先`tools/dev.py sdl-run`重建，再停止旧`pico-sim`并重启。可见入口使用行缓冲输出，`docker logs pico-sim`可即时核对导航回执。kind 0/1/2的cache日志为邻页/字形/封面快照与准入拒绝、压力淘汰计数；共享预算和统计边界见[内存与响应](MEMORY_AND_RESPONSE.md)。低预算窗口是主机软件验证，不是设备PSRAM或面板指标。


硬件资源与模拟约束以[硬件基线](HARDWARE.md)为准。`pn_hardware.h`共享6MiB池上限和32KiB产品任务栈，模拟器命令行也拒绝超限；固件阅读组件生成`.su`栈报告并拒绝大于16KiB的函数栈帧。普通板级固件输出关键EPUB/摘要耗时、缓存及栈水位，CONFIG_PN_PROFILE额外开启全部阶段/分类堆诊断。

章节语义缓存为kind 3，384KiB单项、复用原1MiB共享总额；超限流式回退。`host`新增semantic_cache和epub_hotpath零XML热翻页回归，`sdl`校验四类共享总额。profile入口新增同一保存位置的冷重开阶段`epub.resume-open`（仍完整SHA），使用各轮独立状态；不是浅睡resume，也不测面板。普通设备子阶段仅输出简洁profile行：epub.zip/publication、model-build/replay、layout、raster.body/footer、display.queue/wait/receipt、storage.read/sync/progress/recent/page-index。子阶段包含于外层，不能重复相加，诊断输出也会影响外层耗时。

设备`Display stages`以毫秒拆分dirty（脏区比较）、convert（格式转换）、power（扫描配置/上电）、scan（清屏/波形/欠载恢复）、remember（记下成功帧及收尾）；`display.wait`包含显示排队与实际扫描，`display.receipt`还包含产品侧备份。仅显示owner输出面板分段，不跨线程扫描产品分配池。


最新阅读响应/刷新阶段见[开发计划](READING_RESPONSE_PLAN.md)。普通固件`Present`行包含用途与reason（reading/management/requested/maintenance/manual/unknown-reference/underrun），用来核对全刷触发；display分段和保存分段仍分别记录。主机font_composite使用旧逐像素oracle比较全部灰阶值，reader_toolbar比较成功/失败后的原帧；这些不等于光学残影、实际TF延迟或设备栈水位。
