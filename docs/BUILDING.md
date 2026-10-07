# 当前工程的构建与运行

当前版本为0.0.14开发基础：具备独立ESP32-S3 TXT worker入口、受限分配器、共享4bpp绘制、主机测试、PGM捕获与可选SDL窗口，以及介质租约、显示缓冲所有权、内容SHA/A-B文件记录与TXT位置载荷、自动保存策略、每书100条书签管理/列表/改名/确认与阅读跳转/返回，以及严格TXT解码、有界分页节点和TTF原生绘制/真实TXT静态捕获及SDL交互阅读/持久续读。详细约束见 [存储与显示维护契约](STORAGE_AND_DISPLAY.md) 和 [持久位置契约](PERSISTENCE.md)。设备已接文件书架、TXT选择入口和内部LittleFS；未上板验证，[阅读状态契约](READING_STATE.md)列出书签/历史的实现边界。完整书库、封面提取、正文排版/字体栅格、字体安装、壁纸管理、传输、PDF和OTA产品流程尚未实现。不要将工程测试图视为阅读器功能验收。

## 最短入口：Windows加Docker Desktop

在pico_nano根目录运行，需Python 3与已启动的Docker Desktop Linux引擎。脚本使用已固定摘要的ESP-IDF v6.1容器；第一次运行会下载工具镜像。命令不烧写设备。

```powershell
python tools/dev.py docs
python tools/dev.py host
python tools/dev.py sim
python tools/dev.py firmware-ci
python tools/dev.py firmware-board
```

host启用ASan/UBSan并执行共享C源测试；sim执行捕获/预算失败测试并生成 `build-sim/artifacts/pattern.pgm`。PGM为684×1216无损8位灰度快照，来自同一4bpp绘制代码，各档为0到255步进17。固件产物分别为 `build-ci-s3/pico_nano.bin` 和 `build-board/pico_nano.bin`，两个SDKCONFIG完全隔离。完整命令日志在被忽略的 `build-dev/logs/`；失败立即非0退出。

Windows路径与Linux容器路径由脚本转换，参数经列表传递，不依赖PowerShell对-D参数的解析。构建产物中的Linux绝对路径不能直接当Windows烧写命令使用。

## Linux/WSL原生与可选桌面窗口

安装C11编译器、CMake、Ninja、Python 3；SDL窗口另需SDL2开发包和图形会话（例如WSLg）。直接使用独立主机工程，不加载IDF板级组件：

```bash
cmake -S tests/host -B build-host-native -G Ninja -DPN_SANITIZERS=ON
cmake --build build-host-native
ctest --test-dir build-host-native --output-on-failure

cmake -S simulator -B build-sim-native -G Ninja -DPN_SIM_SDL=ON
cmake --build build-sim-native
ctest --test-dir build-sim-native --output-on-failure
./build-sim-native/pn_sim
./build-sim-native/pn_sim --headless --capture build-sim-native/pattern.pgm
```

SDL默认窗口显示工程图；--book进入共享应用控制器，可键盘/底部按钮翻页与字号并关闭保存。通用JSON脚本、三键/完整书架尚未实现；板级触摸入口已接但未实测；ownership是固定调度场景。容器headless不需要SDL或窗口服务。SDL的ARGB显示缓冲明确属于主机开销，不混入设备PSRAM预算。

模拟器CLI目前支持 `--headless`、`--capture FILE.pgm`、`--budget BYTES`，及固定场景 `--scenario ownership`（过期扫描完成/新帧捕获）。headless必须提供capture，预算包含分配头，超限失败且不创建捕获文件。未启用SDL时请求窗口会明确报错；ownership场景默认三缓冲/2MiB预算，普通模式默认一缓冲/1MiB预算；未来media/script/budget-profile命令尚未提供。交互阅读/--state-dir、--book/--font边界见 [阅读会话](READER_SESSION.md)。

## 工程固件的硬件边界

启动只初始化NVS及BSP，读取一次PMU VCOM，然后把相同灰阶图通过BSP方向映射写入面板并GC16推屏。未读取VCOM时停止显示并提示出厂标定需求；当前工程未提供标定UI。不自动擦除NVS、不自动格式化TF，不写SY7636A标定参数。日志报告呈现结果、耗时、分配峰值和空闲堆。

BSP内部仍有显示缓冲等分配；当前pn_pool峰值只代表共用逻辑图的受限分配，不是整机PSRAM峰值。固定25°C是工程阶段参考条件，后续温度适配和实测必须完成。120MHz真机配置仍取决于Zbit硬件，CI配置只用于编译。

目前已编译两个配置，未烧写/启动验证。真机执行前记录板型、flash型号与串口，使用IDF生成的完整flash_args及正确路径；不把单独app.bin从地址0烧入。QEMU/Wokwi工程尚未接入，不能用本BSP工程直接运行证明整板仿真。

## 维护与自动检查

`python -m unittest discover -s tests/tools -v`检查验证工具；`python tools/check_docs.py`检查文档相对链接/围栏、16MiB分区范围/对齐/重叠，以及118个复用文件的摘要和各组件LICENSE。复用源码如需有目的的修复，先更新来源/修改说明，不能直接改摘要掩盖漂移。

GitHub Actions已定义主机测试/截图及ci/defaults编译矩阵，尚未在远端运行。产品性能和可靠性仍按 [验证门](VALIDATION.md) 上板确认；长期路线与模拟器扩展见 [开发与测试设计](DEVELOPMENT_AND_TESTING.md)。

文本编码与分页的当前实现边界见 [文本内核](TEXT_CORE.md)。host还会独立核对全部GBK映射和生成表，不能只通过编译就称支持完整阅读。

真实TXT捕获命令与--book/--font/--page/--size、字体预算/许可证及仍未接入的界面流程见 [字体port](FONT_PORT.md)。

可复跑SDL dummy/ASan测试入口为python tools/dev.py sdl，初始化镜像为python tools/dev.py factory-data（不烧写、不加入普通flash）；硬件接入详见 [设备入口](DEVICE_PORT.md)。

书架PC窗口可用 `pn_sim --library DIR --state-dir STATE`，操作与限制见 [书架契约](CATALOG.md)。

TXT书签交互已经接到设备触摸与两种PC窗口，见 [书签界面](BOOKMARK_UI.md)。

最近20条及继续入口已经接设备/PC；持久格式和限制见 [最近历史](RECENT_READING.md)。

TXT与EPUB逐书字号/行距/段距/首行缩进/字距/边距/清残影已接草稿、正文预览和应用/取消，见 [排版设置](TYPESETTING_SETTINGS.md)。

格式基础新增ZIP资源流，验证和边界见 [EPUB容器层](EPUB_CONTAINER.md)。

真实本机EPUB可用 `python tools/dev.py epub-sample --archive "mockDoc/绍宋.epub" --all-resources` 核对全部ZIP资源；只读原文件，不输出正文，样本和报告不会进入Git。该入口尚不测试EPUB阅读界面。

container/OPF的真实同源结构解析可用host工具epub_dump，契约与可复跑测试见 [出版物结构](EPUB_PUBLICATION.md)。

epub_dump增加--toc以解析NCX/nav层级与章节/锚点引用，支持边界见 [目录模型](EPUB_NAVIGATION.md)；产品中文目录界面见[目录界面](EPUB_TOC_UI.md)。

body_probe可读取全部spine正文，输出统计/字符CRC或查询id锚点，见 [正文流](EPUB_BODY.md)；不输出正文，不做阅读呈现或续读保存。

epub_capture可用实际TTF生成有界文本页，详见 [EPUB分页](EPUB_PAGINATION.md)；原生交互应用、PNG/JPEG和受限CSS现已接入，完整支持边界见[EPUB应用](EPUB_APP.md)。

PNG已接同源逐行解码、透明合成与真实EPUB捕获；JPEG和受限CSS边界另见[EPUB应用](EPUB_APP.md)，PNG契约详见 [PNG绘制](PNG_RENDERING.md)。
