# PicoNano

基于 ESP32-S3 的独立墨水屏阅读器固件。核心、解析、字体、排版与界面在设备和 PC 模拟器中共用源码，固定目标为 RDP-G01-W/E0470A01、684×1216、16 灰阶、8MiB PSRAM、16MiB Flash。对外品牌在所有语言中统一为 **PicoNano**。

当前版本为 **1.0.1 正式版**，精简内置阅读背景与默认封面，兼容旧排版背景ID，完整校验和保留图片画质不变。此前1.0.0已整合“远山与书页”首页、正文阅读背景、壁纸/遮罩缩略图与原生灰阶缓存、网页图片按用途压缩，以及首屏输入交接与近期记录空闲保存。保留本地书签锁屏、中式黄历、阅读时长统计和完整书籍/字体校验；详细变化见[变更记录](docs/CHANGELOG.md)。

提供保留数据升级包、完整安装包与离线网页安装器，精确源码以包内 `manifest.json` 的 `source_commit` 为准。产品镜像关闭测试控制台；开发设备继续保留独立诊断固件。**1.0.1 正式版已完成真机验证**，本版提供 USB 安装包。

从[文档入口](docs/README.md)开始：

国内下载安装包请访问 [Gitee 发行版](https://gitee.com/windssea/pico_nano/releases)。推荐离线网页安装器：解压后运行 `py serve.py`，用电脑 Chrome/Edge 打开 `http://localhost:8765/`；刷机时无需访问 GitHub。GitHub 与 Gitee 使用同一份发布包，详情见[安装与发布](docs/INSTALLATION.md)。当前发行版通过电脑 USB 安装，不提供设备 Wi-Fi OTA 安装包。

| 目的 | 现行文档 |
| --- | --- |
| 查硬件、资源上限与安全约束 | [硬件规格](docs/HARDWARE.md) |
| 理解最新实现及重点技术 | [架构与实现](docs/ARCHITECTURE.md) |
| 确定下一步问题、需求和技术方案 | [开发指南](docs/DEVELOPMENT_PLAN.md) |
| 开发、模拟、测试与性能测量 | [构建与运行](docs/BUILDING.md) |
| 网页安装、异机烧录与发布规则 | [安装与发布](docs/INSTALLATION.md) |
| 查询跨通道资源目录契约 | [存储卡目录](docs/SD_CARD_LAYOUT.md) |
| 查近期变化或旧专题 | [变更记录](docs/CHANGELOG.md)、[历史归档](docs/archive/2026-10-14/README.md) |

最小开发入口为 `python tools/dev.py docs`、`python tools/dev.py host`；界面开发使用可见模拟器 `python tools/dev.py sdl-run`，修改源码后重启窗口。Windows 的真实 Python 路径、Docker、两套 SDKCONFIG 和验证分层见构建文档。

参考仓库只读，复用源码身份及许可保留。自身代码见 [LICENSE](LICENSE)，第三方来源以 [LICENSES](LICENSES) 中的固定清单及组件许可证为准。VCOM 只从 PMU 读取，无有效值不推屏；默认恢复不格式化、不擦 NVS，USB 与应用不得并行访问 TF。

## 鸣谢

感谢小纸阅读器与开源社区的作者和维护者。以下汇总现行文档、历史归档及来源清单中记录的参考项目；实际复用代码、资源与设计参考分别注明。

### 硬件来源与阅读器参考

| 项目 | 本项目中的来源或参考内容 |
| --- | --- |
| [MindReset/read_pico_firmware](https://github.com/MindReset/read_pico_firmware) | 小纸阅读器官方固件；板级支持、PMU、触控、传感器和屏幕波形等复用来源，固定提交及逐文件摘要见[来源清单](LICENSES/reference-manifest.json)。 |
| [wegooo-cell/read-pico-reader](https://github.com/wegooo-cell/read-pico-reader) | 阅读交互、翻页刷新、预排与预绘制、传书及系统调度的源码分析参考。 |
| [yiifanrecyclable/PicoReader-ifEdition](https://github.com/yiifanrecyclable/PicoReader-ifEdition) | 全页刷新、局部输入反馈及周期维护策略参考。 |
| [chinoryunqin/RickyOS](https://github.com/chinoryunqin/RickyOS) | 字体缓存、内存预算和章节持久模型的源码分析参考。 |
| [CloudZao/Metalio-E-INK4](https://github.com/CloudZao/Metalio-E-INK4) | 字形与页面预热、后台解析及输入反馈的设计参考。 |
| [446599/whiteos](https://github.com/446599/whiteos) | 产品展示与交互设计参考。 |

参考内容的固定提交、采纳范围和取舍见[阅读响应记录](docs/archive/2026-10-14/READING_RESPONSE_PLAN.md)及[来源与决策](docs/archive/2026-10-14/SOURCES_AND_DECISIONS.md)。设计参考不表示整套固件移植或代码复用，也不表示相关项目对PicoNano的背书。

### 开源组件、协议与资源

| 项目 | 用途与来源 |
| --- | --- |
| [ESP-IDF](https://github.com/espressif/esp-idf) | ESP32-S3固件平台，以及随SDK提供的[FreeRTOS](https://www.freertos.org/)、[FatFs](https://elm-chan.org/fsw/ff/)、[lwIP](https://savannah.nongnu.org/projects/lwip/)和[Mbed TLS](https://github.com/Mbed-TLS/mbedtls)等组件。 |
| [epdiy](https://github.com/vroland/epdiy) | 并行墨水屏驱动；当前裁剪fork经官方参考固件引入，差异及LGPL声明见[组件说明](components/epdiy/README.md)。 |
| [FreeType](https://freetype.org/) | 字体解析与光栅化，采用FTL；[固定来源](LICENSES/freetype-manifest.json)。 |
| [LittleFS](https://github.com/littlefs-project/littlefs) / [esp_littlefs](https://github.com/joltwallet/esp_littlefs) | 内部存储文件系统与ESP-IDF适配；[固定来源](LICENSES/littlefs-manifest.json)。 |
| [zlib](https://zlib.net/) / [Expat](https://github.com/libexpat/libexpat) | 压缩资源解码与XML解析；[zlib清单](LICENSES/zlib-manifest.json)、[Expat清单](LICENSES/expat-manifest.json)。 |
| [libspng](https://github.com/randy408/libspng) / [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) | PNG/JPEG图像解码；[PNG来源](LICENSES/spng-manifest.json)、[JPEG来源](LICENSES/jpeg-manifest.json)。 |
| [cJSON](https://github.com/DaveGamble/cJSON) / [Nayuki QR-Code-generator](https://github.com/nayuki/QR-Code-generator) | JSON解析与二维码生成；[JSON来源](LICENSES/cjson-manifest.json)、[QR来源](LICENSES/qr-manifest.json)。 |
| [TinyUSB](https://github.com/hathach/tinyusb) / [esp_tinyusb](https://components.espressif.com/components/espressif/esp_tinyusb) | USB设备栈与ESP-IDF适配；[组件与许可清单](LICENSES/usb-manifest.json)。 |
| [esptool](https://github.com/espressif/esptool) / [esptool-js](https://github.com/espressif/esptool-js) / [pako](https://github.com/nodeca/pako) | USB安装工具、网页串口安装及其随包解压依赖；[网页工具来源](LICENSES/esptool-js-manifest.json)。 |
| [LocalSend protocol](https://github.com/localsend/protocol) / [ZXing载荷说明](https://github.com/zxing/zxing/wiki/Barcode-Contents) | 局域网传输协议与Wi-Fi二维码内容格式参考；本项目接收端独立实现。 |
| [lunar-javascript](https://github.com/6tail/lunar-javascript) | 离线中式黄历数据的生成来源；[固定提交与MIT许可](LICENSES/calendar-manifest.json)。 |
| [Noto Sans SC / Google Fonts](https://github.com/google/fonts/tree/main/ofl/notosanssc) | 内置中文与应急字库的OFL源字体；[UI字库清单](LICENSES/ui-font-manifest.json)、[正文字库清单](LICENSES/reading-font-manifest.json)。 |
| [HarmonyOS Sans / 华为](https://developer.huawei.com/consumer/cn/design/resource) | 原始字体资源；使用范围、摘要及原许可见[字体来源清单](LICENSES/harmony-font-manifest.json)。 |
| [SDL2](https://github.com/libsdl-org/SDL/tree/SDL2) / [fontTools](https://github.com/fonttools/fonttools) | PC模拟器显示与字体资源生成工具。 |

### 资料、工具与历史选型参考

- [ESP工具链资料](https://github.com/espressif/esp-toolchain-docs)、[QEMU](https://www.qemu.org/)、[esp-emulator](https://github.com/espressif/esp-emulator)、[Wokwi](https://docs.wokwi.com/getting-started/supported-hardware)：芯片仿真与开发环境评估资料。
- [MuPDF](https://mupdf.com/releases)、[LVGL](https://lvgl.io/)：历史引擎或UI框架选型参考；当前固件未集成。 [calibre](https://manual.calibre-ebook.com/en/conversion.html#pdf-conversion)：外部书籍转换资料，不捆绑进入固件。
- [stb](https://github.com/nothings/stb)：历史图像工具及来源保留约束中提及的项目；当前PNG/JPEG解码采用上述libspng与libjpeg-turbo。
- [W3C EPUB](https://www.w3.org/TR/epub-33/)与[CSS](https://www.w3.org/TR/css-cascade/)、[NIST SHA-256规范](https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf)、[GNU Binutils链接器资料](https://sourceware.org/binutils/docs/ld/Options.html)：格式、摘要和链接机制的规范依据。
- [CMake](https://cmake.org/)、[Ninja](https://ninja-build.org/)、[Python](https://www.python.org/)、[Node.js](https://nodejs.org/)、[Docker](https://www.docker.com/)：构建、资源生成和回归环境工具。

各项目版权、原许可证和固定来源保持不变，以上鸣谢不替代[LICENSES](LICENSES)及组件内的许可声明。
