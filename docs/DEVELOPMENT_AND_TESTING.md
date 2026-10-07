# 开发环境、PC模拟与测试流程

日期：2026-10-05。本文定义后续实现必须交付的开发入口，与 [开发计划](DEVELOPMENT_PLAN.md)、[验证与发布门](VALIDATION.md) 配合使用。

**本文件主要定义长期开发契约。0.0.8已建立工程、共享分配/绘制及租约/显示所有权、内容身份、持久记录、自动保存、单TXT书签、严格文本解码/分页与原生字体/阅读会话及PC交互事件、设备启动替身和真实LittleFS核心恢复测试、headless灰阶捕获/固定ownership场景与可选SDL后端，并编译两种固件配置；其余计划目录、场景回放、阅读UI、QEMU/Wokwi接入尚未实现。** 当前可执行命令与边界以 [构建与运行](BUILDING.md) 为准；未烧写或真机验证。

## 1. ESP32-S3能否在PC上模拟

可以，但分两类：PC直接运行共享C代码，或模拟Xtensa芯片执行交叉编译固件。两者都不能完整重现这块阅读器的墨水屏和电源硬件。

| 层级 | 本项目用途 | 能覆盖 | 必须另测 |
| --- | --- | --- | --- |
| PC原生核心测试 | 必选，日常及CI | 格式解析、分页、字形、预算、位置、文件事务、失败释放 | Xtensa差异、RTOS竞争、真实介质持久性 |
| PC桌面阅读器模拟器 | 必选，优先开发 | 真实UI和排版、书架、字体、壁纸、键触交互、截图与故障回放 | 面板光学残影、硬件帧率与电流 |
| Espressif QEMU | 可选，芯片级冒烟 | S3指令、部分内存/外设、启动、目标二进制与GDB | 原板I2C、SD、WiFi、USB与EPD驱动链 |
| Wokwi | 可选，交互样机 | 支持的S3与虚拟外设、ESP-IDF构建产物 | Read Pico整板模型与实物电源/波形 |
| 真机 | 必选，持续迭代 | 实际刷新、内存/带宽、触摸、PMU、传输、休眠与升级 | 不用于替代可快速穷举的主机测试 |

[ESP-IDF v6.1 QEMU指南](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/tools/qemu.html)明确支持ESP32-S3，并提供Windows x86_64预编译工具。其虚拟RGB framebuffer是测试设备，不是Read Pico面板模型。[官方外设支持表](https://github.com/espressif/esp-toolchain-docs/blob/main/qemu/README.md)目前列出S3双核、Flash、OPI PSRAM等，WiFi、USB、I2C及S3 SD/MMC未支持；因此不能把参考固件直接启动成功作为预期。

[Wokwi支持列表](https://docs.wokwi.com/getting-started/supported-hardware)包含ESP32-S3；[ESP-IDF接入说明](https://docs.wokwi.com/vscode/esp-idf)允许加载完整烧录映像并配置Octal PSRAM。这只证实工具支持芯片，不证实本板PMU、触摸芯片、SY7636A及EPD扫描链存在对应模型。接入前做最小样机核验，并核对当前使用条款与所需服务权限。

[ESP-IDF Linux host target](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/host-apps.html)仍是实验功能，组件支持有限，调度仿真有边界。本项目以独立CMake核心测试和显式port适配为主，不让整套板级BSP成为PC构建依赖。官方另有 [esp-emulator Beta](https://github.com/espressif/esp-emulator) 宣布支持S3，可单独评估；尚未验证其与本项目IDF/驱动组合，暂不纳入发布必过门。

## 2. Windows开发机配置

建议使用Windows上的ESP-IDF v6.1终端负责真机编译、COM口烧录与监视；WSL2 Ubuntu负责Clang/GCC、ASan/UBSan、CTest与桌面模拟器。WSLg可提供SDL窗口。CMake原生Windows模拟器作为后续便利入口，CI参考环境固定Linux，避免先投入多个平台适配。

| 工具 | 用途与版本约束 |
| --- | --- |
| Git、ESP-IDF v6.1及配套工具链 | 固件构建；先检查idf.py --version，不混用其他IDF终端 |
| CMake、Ninja、Clang/GCC、zlib开发包 | 共享C组件与主机测试；实现时在DEPENDENCIES中锁定验证版本 |
| SDL2开发包 | 显示4bpp framebuffer、输入映射；不引入另一套排版引擎 |
| Python 3、Node.js受支持LTS | 样本生成、结果整理、真实网页脚本测试 |
| QEMU Xtensa、GDB | 可选目标级调试，采用IDF匹配版本 |
| 真机、专用测试TF卡、USB线 | 至少一板日常开发；发布按VALIDATION的两板/多卡门执行 |
| 电流测量与固定摄影设备 | 耗电、可见延迟与残影验收 |

主机工程最好置于WSL Linux文件系统以减少小文件编译开销，通过Git与Windows工程同步；不同工作副本不能混用同一个build目录，也不能同时修改同一测试介质。源代码和工具链由版本管理同步，用户书籍、字体及私有凭据不进入仓库。

## 3. 先做共享代码的桌面模拟器

### 3.1 目录与依赖

以下目录在T01/T05建立，不要求先完成所有格式：

```text
simulator/CMakeLists.txt       链接真实pn_core/ui/shell/reader等组件
simulator/main.c               配置、窗口、回放与退出入口
simulator/port/                时钟、文件、任务执行器、预算与持久存储替身
simulator/display_sdl.c        4bpp转显示像素、damage与截图
simulator/input_sdl.c          鼠标/键盘转换成公共输入事件
simulator/scenarios/           交互、慢刷新、缺卡与故障脚本
simulator/golden/              审核后的规范画面及元数据
sim-data/sd/                   本机测试书籍/字体/壁纸，不提交私有资源
sim-data/internal/             本机NVS、日志、壁纸槽替身
build-sim/artifacts/           截图、事件、内存与显示job记录
```

直接链接固件使用的解析器、字体光栅、绘制原语和页面控制器。PC版只替换硬件与OS边界，不用网页HTML另写一套“看起来类似”的书架。主机字体不能悄悄替换为操作系统字体；固定字库摘要、字号、字重轴、布局版本后才能做golden比较。

T01交付窗口/灰阶测试图/时钟/预算基础；T05交付导航、键触与显示job；T06/T07接真实字体和TXT/EPUB；T08完成封面、设置与字体壁纸完整旅程；T10通过PDF技术门后共享PDF视口。每阶段模拟器只展示已经实现的能力。

### 3.2 输入、刷新与截图

- 逻辑画布固定684×1216，缩放窗口只改变显示比例；鼠标坐标反算到逻辑像素。横屏沿用真实方向变换。
- 鼠标点击/拖动产生设备同契约输入；键盘1/2/3映射KEY1/2/3，按住2触发600ms长按，方向键是明确标注的测试快捷入口。多指动作使用脚本注入或后续支持的触控设备。
- `present`记录区域、波形模式、job token与完成时间。即时显示用于审图；慢刷新模式可注入排队/扫描耗时，检查反向翻页、取消和缓冲所有权。注入430ms只是软件策略测试条件，不代表面板实测。
- 对4bpp原始像素及转换后的PNG分别保存；golden比较原始像素，避免窗口缩放、系统DPI和显示色彩造成误差。像素相同只能证明渲染一致，不能证明纸屏观感。
- 截图回归包括书架空/满/长标题、正文、排版预览、缺字、目录、传书错误、字体管理、壁纸裁切与锁屏。固定时间、电量、随机种子和语料，禁止用扩大容差隐藏变化。

### 3.3 资源与故障模型

PC也使用pn_alloc池和ARCHITECTURE文本/PDF预算，统计UI、字体、解码器、帧缓冲、worker中间结果与峰值；引擎内部malloc必须可追踪/限额。OS栈、SDL及测试工具开销另列，不能混成设备占用。主机64位结构大小与目标32位不同，最终预算仍以目标构建和真机为准；文件大小、偏移及磁盘格式用固定宽度与显式溢出检查。

替身必须支持：第N次分配失败、卡移除/更换epoch、短读/短写、满空间、同步失败、事务阶段进程终止、损坏A/B记录、延迟/乱序完成、显示欠载、锁屏遇未保存状态。测试重启后读取同一目录验证恢复，不能仅在进程内回滚后声称断电恢复。主机文件系统fsync/rename行为不等价于FAT与flash掉电，真机仍验证关键phase。

单线程确定性执行器便于复现事件；另做真实线程与目标双核压力检查。预算模型不能模拟PSRAM带宽、DMA能力和缓存一致性，双核数据竞争不能用单线程通过作结论。

## 4. 日常开发与命令入口

命令属于未来实现契约；T01创建CMake/脚本后才可执行。当前运行会因缺工程失败，不应安装工具来掩盖缺实现。所有artifact保存到被忽略的build目录。

主机核心与网页测试，WSL/Linux工程根目录：

```bash
cmake -S tests/host -B build-host -G Ninja -DPN_SANITIZERS=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
ctest --test-dir build-host -L storage --output-on-failure
python tools/check_docs.py
node tests/web/transfer.test.js
```

CTest标签拟定为core/storage/format/font/layout/display/ui/transfer/pdf；`-L`筛选标签，`-R`筛选测试名称。失败保留样本、随机种子、事件脚本与回溯，不只留下最终“FAIL”。

桌面模拟器，拟定CLI与构建开关：

```bash
cmake -S simulator -B build-sim -G Ninja -DPN_SANITIZERS=ON
cmake --build build-sim
./build-sim/pn_sim --media sim-data/sd --internal sim-data/internal --budget-profile text
./build-sim/pn_sim --headless --script simulator/scenarios/reading.json --capture build-sim/artifacts
ctest --test-dir build-sim -L ui --output-on-failure
```

headless模式不用窗口/GPU也能绘制、记录并截图；脚本显式指定媒体、内部数据和预算profile。脚本包含版本、fixture摘要、输入与虚拟时间、故障点、预期页面/锚点/资源结果；配置不存在或断言失败退出非0。PDF profile不等于已完成移植，需T02/T10通过才能加入对应场景。

固件CI配置与真机默认必须各用独立SDKCONFIG和build目录；`SDKCONFIG_DEFAULTS`只提供默认值，已有sdkconfig不会自动被覆盖。以下PowerShell命令以目标开发路径为例，执行时需工程、配置和分区已实现：

```powershell
idf.py --version
idf.py -B build-ci -DSDKCONFIG=D:/windssea/dev/pico_nano/build-ci/sdkconfig -DSDKCONFIG_DEFAULTS=sdkconfig.ci set-target esp32s3
idf.py -B build-ci -DSDKCONFIG=D:/windssea/dev/pico_nano/build-ci/sdkconfig -DSDKCONFIG_DEFAULTS=sdkconfig.ci build
idf.py -B build-ci -DSDKCONFIG=D:/windssea/dev/pico_nano/build-ci/sdkconfig -DSDKCONFIG_DEFAULTS=sdkconfig.ci size

idf.py -B build-board -DSDKCONFIG=D:/windssea/dev/pico_nano/build-board/sdkconfig -DSDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32s3
idf.py -B build-board -DSDKCONFIG=D:/windssea/dev/pico_nano/build-board/sdkconfig -DSDKCONFIG_DEFAULTS=sdkconfig.defaults build
idf.py -B build-board -DSDKCONFIG=D:/windssea/dev/pico_nano/build-board/sdkconfig -DSDKCONFIG_DEFAULTS=sdkconfig.defaults -p COM7 flash monitor
```

COM7需改为已确认的设备串口；保存板型/flash识别与构建配置，刷写地址交给IDF产物，不手填猜测地址。CI时序只作编译验证，不能作为本板产品默认；120MHz仍需Zbit及真机验证。开发中不烧写eFuse，不改VCOM产品行为。

## 5. QEMU与Wokwi接入方法

### QEMU：独立模拟配置，先启动最小程序

在T01之后另建 `examples/qemu_smoke/`，依赖最少组件；`sdkconfig.qemu`与port选择排除真实I2C/PMU/SD/EPD初始化，以虚拟输入、存储和显示替身接公共组件。先验证IDF hello-world、S3目标和PSRAM模型，再逐步加状态机与业务。禁止为模拟器启动而放松正式固件硬件检查。

ESP-IDF v6.1终端中，按官方安装并重新导出环境；PowerShell安装入口示例：

```powershell
python "$env:IDF_PATH/tools/idf_tools.py" install qemu-xtensa
```

以下是独立smoke工程建立后的官方运行入口，在其工程目录使用：

```text
idf.py set-target esp32s3
idf.py qemu monitor
idf.py qemu gdb
idf.py qemu --graphics monitor
```

graphics模式需应用接入官方 `espressif/esp_lcd_qemu_rgb` 或适合的虚拟显示适配，不会自动显示epdiy扫描结果。固定QEMU版本并记录模型；虚拟Flash与真实分区一致时可检查启动/日志，但实际文件系统掉电仍另测。QEMU缺失外设时标记未覆盖，不把替身成功称为WiFi/USB验证。

### Wokwi：作为可选演示和交互调试入口

建立独立smoke工程的 `wokwi.toml` 与 `diagram.json`，指定ESP32-S3板；固件引用实际build目录的 `flasher_args.json`，ELF引用实际项目名；Octal PSRAM按官方设置 `psramType: octal`。完整烧录描述比单独app.bin更能保留分区与额外映像。外设逐一验证，支持的虚拟屏只能接模拟display port，不与真实EPD接线混淆。可视化演示、联网能力与CI服务使用条件在选用时核对，不预设全部免费或可离线。

## 6. 每个功能如何开发和验证

1. 先明确输入、成功画面、失败状态、资源上限和验收样本。例如换字体须保持原文锚点，上传成功须经过校验与入库。
2. 纯逻辑先在共享源主机入口实现并验证：文本连续性、位置、元数据、事务、预算和释放。为可复现风险保留回归；简单外观调整用截图审查即可，不写机械重复实现的测试。
3. 在桌面模拟器走完整操作链，核对按键、应用/取消、返回来源、错误提示和截图；用慢显示和故障脚本验证取消及资源回收。
4. 每次相关改动编译两个固件配置并检查映像/分区大小；重要IDF契约可加QEMU smoke，但QEMU不是整板测试的替代。
5. 尽早上板，测该功能的真实内存峰值、显示与输入行为；PDF技术门在完整书架前完成，不能等到最后才发现8MiB内存不足。
6. 跨平台传输用真实浏览器/手机/网络测，端到端校验传入后内容SHA与解析结果；故障注入后重启并复核旧资源。字体上传后实际选用，壁纸上传后实际裁切/应用/拔卡锁屏，不能只测HTTP 200。
7. 达到VALIDATION门再发布；持久行为更新功能文档，临时日志留docs/local/build。分别报告主机通过、编译通过、模拟通过、上板通过及未覆盖项。

本机PC可跑与固件共用导入逻辑的HTTP替身，方便手机提前试网页与拖放，使用测试目录与独立端口；网络/TLS适配和AP行为仍需设备。如果后续启用LocalSend兼容接收，增加真实Android/iOS/Windows客户端的发现、授权、书/图/字体分类、取消、摘要、TLS堆与单文件并发测试；当前设计主通道仍是网页方案，LocalSend互通尚未实现或验证，也不冒称具有协议外的分块续传。

## 7. CI与测试分工

| 触发 | 自动检查 | 产物/判定 |
| --- | --- | --- |
| 每次相关提交 | Linux共享源CTest、ASan/UBSan、文档链接、网页真实脚本 | 非0即失败，保留回溯与fixture |
| UI/字体/排版改动 | headless模拟器场景、4bpp golden、布局/锚点断言 | 审核差异；不自动更新基准掩盖问题 |
| 固件改动 | IDF v6.1两配置交叉编译、size/partition、依赖许可 | 不超过槽/预算；不得声称已上板 |
| 可选芯片smoke | 固定QEMU配置启动与串口断言 | 超时/异常失败，缺外设列未覆盖 |
| 夜间/专项 | 分配失败、事务故障点、parser fuzz、长序列 | 累计fuzz时长与新固定回归 |
| 真机里程碑 | 显示/传输/功耗/断电/升级/多板多卡 | 固件SHA、条件、原始数据及支持结论 |

CI缺真实WiFi/触摸/墨水屏时不伪造结果；没有实体测试设备的流水线只能通过“软件和编译”门。对性能的最终判断使用真机p50/p95/最大与失败数；桌面吞吐及模拟CPU时间仅供回归定位。现有18–28人周包含基础host入口；本次将可视模拟器、自动截图和故障回放明确化，工作量暂修订为约19–30人周，复杂板级仿真模型不纳入必选范围。
