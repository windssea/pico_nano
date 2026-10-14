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

默认样本为 `build-dev/sim-library/piconano/`，生成原创TXT/EPUB、OFL字体子集和透明PNG几何图。`tools/make_sim_library.py`补缺而不覆盖已有文件，manifest记录SHA与来源。私人原书/TTF可在被忽略的 `build-dev/sim-library/local-inputs.json` 配置：

```json
{"files":[{"kind":"book","path":"tests/本机样本.epub"},{"kind":"font","path":"tests/本机字体.TTF"}]}
```

原件只读，仅复制到隔离目录；私人小说/完整字库不嵌入固件、不成为公开fixture。`tools/run_sim.ps1`可指定 `-Books/-Font/-FontDir/-WallpaperDir` 和 `-Timing`，容器输入须在工作区内。模拟器时区Asia/Shanghai，电量为演示值，锁页预览不模拟PMU功耗。预算上限6MiB，低预算可降，不可扩大掩盖设备故障。

## EPUB 与性能诊断

```powershell
python tools/dev.py epub-sample --archive "build-dev/sim-library/piconano/books/05-章节目录与书签.epub" --all-resources
python tools/dev.py profile --archive "build-dev/sim-library/piconano/books/05-章节目录与书签.epub" --rounds 30
```

profile每次生成 `build-dev/profile/<随机ID>/` 隔离状态与summary.json；保留源书SHA、轮数、预算、后端、冷/热条件、失败计数和各阶段p50/p95/max。普通软件显示回调不测面板，OS缓存通常未受控。相同书/字体/布局/工具链对照，不能修改原书后宣称同样本提速。

主机 `-Timing`/profile记录分类账本及阶段局部同时峰值；分类峰值不相加，嵌套耗时不重复相加。设备可单独用 `CONFIG_PN_PROFILE=y` 做诊断，但不自动刷机，默认发布配置关闭。诊断日志有测量成本；记录是否开启，池外堆和任务栈必须设备独立测量。

普通固件保留开书/SHA/页准备、显示reason、dirty/convert/power/scan/remember、queue/wait/receipt和关键保存分段；用来定位慢开书/翻页，不能只看总Present。热页软件和真实光学/电源性能分别比较，参考成绩及局限见[架构末节](ARCHITECTURE.md#性能利用与阅读优化)。

## 验证与开发收口

文档重组只跑文档/引用/归档校验及受影响工具测试，不为可逆文档修改重建固件。源码修改选择有意义的相关主机回归；阶段完成再全量host、SDL自动旅程和可见窗口。需要发行时按用户授权跑两套SDK构建/size，随后按[安装与发布](INSTALLATION.md)核对包与站点。没有刷入的构建不能称真机修复。

基线时间要写清：已发布 `0.6+a0a9a97` 经144CTest/10Python/5Node、31SDL、17项安装器计划测试及两套固件构建。本页计数是历史证据，下一批以实际输出为准，不把“计数一致”当验收条件。

逐项真机验证方案、样本与长期矩阵见[开发指南](DEVELOPMENT_PLAN.md)。错误/失败/取消、资源关闭used/live=0、像素/位置一致性都需保留，不能把失败样本从统计中删掉。
