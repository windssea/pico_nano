# PicoNano

基于 ESP32-S3 的独立墨水屏阅读器固件。核心、解析、字体、排版与界面在设备和 PC 模拟器中共用源码，固定目标为 RDP-G01-W/E0470A01、684×1216、16 灰阶、8MiB PSRAM、16MiB Flash。对外品牌在所有语言中统一为 **PicoNano**。

当前源码目标版本为 **1.0.9**。本版改进界面对比度、近期保存与缓存调度，继续优化开书校验和首次翻页预绘，并移除LocalSend；网页与USB传书保留。界面采用更清晰的卡片、文字、描边和选中态，封面原图及其灰阶转换保持。原生16灰阶、完整来源校验及内存上限保持；相关诊断设备观察与未验收范围见[真机报告](docs/READING_DEVICE_TEST_REPORT.md)和[开发指南](docs/DEVELOPMENT_PLAN.md)。正式发布及下载身份以发行清单为准。

开发与模拟器入口见[构建文档](docs/BUILDING.md#开发时保持可见模拟器)，可运行`tools/run_sim.ps1 -Demo`使用原创演示资源，无需私人书籍或完整字库。

2026-10-28完成阅读48相GC16清残影的有限真机观察和系统级代码审查；软件与设备验证范围、后续优化顺序见[开发指南](docs/DEVELOPMENT_PLAN.md#2026-10-28系统级代码审查与优化顺序)。

提供保留数据升级包、完整安装包、离线网页安装器与签名OTA附件；精确源码、镜像摘要和适用范围以包内`manifest.json`及[安装文档](docs/INSTALLATION.md)为准。正式镜像与诊断真机测试分别登记，不将软件回归或诊断截图作为正式版的断电、睡眠、光学和功耗验收。

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
| [ZXing载荷说明](https://github.com/zxing/zxing/wiki/Barcode-Contents) | Wi-Fi二维码内容格式参考。 |
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
