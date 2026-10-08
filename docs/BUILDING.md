# 构建与运行入口

当前开发版本0.0.48。能力与待办统一见 [项目状态](PROJECT_STATUS.md)，环境、测试分层和证据边界见 [开发与测试](DEVELOPMENT_AND_TESTING.md)。本页只列已经存在的运行入口，不把拟建命令当成实现。

## Windows与Docker

在 `D:/windssea/dev/pico_nano` 执行，需要Python 3与Docker Desktop Linux引擎。固定工具镜像：`espressif/idf:v6.1@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`。

```powershell
python tools/dev.py docs
python tools/dev.py host
python tools/dev.py sim
python tools/dev.py sdl
python tools/dev.py firmware-ci
python tools/dev.py firmware-board
```

命令均不烧写。host执行同源C、ASan/UBSan、真实文件/HTTP、SDK-stub和工具测试；sim为基础headless捕获与调度场景；sdl构建可选窗口模拟器并通过dummy驱动测试交互。Windows Docker里的dummy测试不打开可见窗口。

若Docker连接失败，先读 `docker context ls`、`docker desktop status` 和 `docker --context default version`。本机当前default可以连接Linux引擎；旧desktop-linux管道可能不可用。仅在确认context指向可用Linux引擎后，给当前Shell设置：

```powershell
$env:DOCKER_CONTEXT='default'
python tools/dev.py host
```

这是当前Shell选择，不持久修改用户配置。不因观察超时重启构建或引擎；依据进程/会话的实际状态判断。

## 产物与SDK隔离

| 入口 | 目录 | 用途 |
| --- | --- | --- |
| host | build-host | 同源C可执行测试及原生HTTP worker |
| sim | build-sim-idf、build-sim/artifacts | 基础PGM捕获 |
| sdl | build-sim-sdl-idf | SDL交互测试/窗口程序 |
| firmware-ci | build-ci-s3 | sdkconfig.ci，默认时序编译验证 |
| firmware-board | build-board | sdkconfig.defaults，真机Zbit 120MHz配置 |
| factory-data | build-dev/factory-data.bin | 单独生成内部数据文件系统镜像，不自动烧写 |
| docs | build-dev/logs/docs-0.log | 文档/分区/依赖与字体摘要核对 |

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

mockDoc为用户本机样本，Git不提供；先自行放入合法测试文件。单书用 `--book PATH`，EPUB入口按扩展名分派。`--headless --capture FILE.pgm`、`--budget BYTES`、`--scenario ownership`等以simulator/main.c为准；没有通用JSON脚本CLI。窗口脚本测试使用测试专用环境变量，不能当真实用户交互验收。

## 功能入口与样本

- 阅读/续读/目录/书签/排版/字体：见 [阅读会话](READER_SESSION.md)、[EPUB应用](EPUB_APP.md)、[阅读状态](READING_STATE.md)。
- PC传书网页：见 [网页服务](TRANSFER_WEB.md)，使用独立测试书库，不能与阅读模拟器同时访问同一根。
- 设备热点/退出返回：见 [设备入口](DEVICE_TRANSFER_ENTRY.md)；扫码及独立解码见 [扫码传书](TRANSFER_QR.md)。
- EPUB容器全资源检查：`python tools/dev.py epub-sample --archive "mockDoc/绍宋.epub" --all-resources`，只读原文件，报告留build-dev。
- 字体子集/fixture：tools/subset_ui_font.py、tools/subset_fallback_fixture.py，源哈希必须匹配；不能手改字体或表。

主设备启动阅读与书架，热点已装配；仍未真机验证。未取得有效PMU VCOM不推屏，不擦NVS、不自动格式化，不写SY7636A标定。当前设备呈现固定25°C工程值，温度/波形/功耗还需实测。
