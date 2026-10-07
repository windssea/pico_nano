# 依赖与源码来源

新建共享核心、UI基础与模拟器源文件使用Apache-2.0。复用的板级组件来自 `MindReset/read_pico_firmware` 提交 `28cde682a4468a581c278761922724f57d976418`，逐文件SHA-256记录于 `LICENSES/reference-manifest.json`。本阶段只复制所需组件，排除examples/build等目录，没有修改复用源代码；源文件原有版权、许可证与冻结决策保留。

| 组件 | 许可与边界 |
| --- | --- |
| read_pico、read_pico_pmu、cst836u、sc7a20h、fca9555、sy7636a、e0470_epaper_waveform | Apache-2.0，以各目录LICENSE及文件声明为准；波形表保留原样 |
| epdiy裁剪fork | LGPL-3.0-or-later；裁剪与历史修改说明在其LICENSE中，发行需准备适用的源码/重链接材料 |
| pwm_audio | Apache-2.0，保留原版权声明 |
| ESP-IDF | v6.1.0及配套工具链；dependencies.lock固定本工程IDF版本，独立组件许可证以IDF发行文件为准 |
| SDL2 | 可选主机显示依赖，zlib许可；不链接进设备固件 |
| 主机CMake/Ninja/Python | 构建和测试工具，不作为固件运行依赖 |

参考仓库保持不变。当前未导入参考TTF、字体光栅器、书籍解析器或PDF引擎；不声明相应功能或许可路径已解决。MuPDF仍为待通过技术与发行许可门的候选。

已验证的参考环境：固定容器 `espressif/idf:v6.1@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`，容器主机GCC 13.3.0、IDF Xtensa GCC 15.2.0；SDL编译另在GCC 14.2.0/CMake 3.31.7/SDL2 2.32.8环境完成。工具版本不是设备性能证据。

## 生成的GBK数据

pn_text/pn_gbk.generated.h由本项目tools/gen_gbk.py调用Python严格gbk codec生成Unicode映射事实表；不复制Python库源码，不改参考生成表。当前生成产物通过固定IDF容器Python3.12.3逐项核对；生成器与独立codec对照均在host CTest运行，变化须重新核验编码/位置测试。

## FreeType与常驻字体

FreeType 2.14.3选择FTL，完整源、附带许可和固定归档/文件摘要保留于components/pn_freetype/vendor及LICENSES/freetype-manifest.json；裁剪仅在外部构建模块清单与配置，不修改vendor。

Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved.

UI字体由OFL-1.1的Noto Sans SC固定源生成Read Pico UI Regular子集，保留assets/fonts/OFL.txt与ui-font-manifest；本子集继续以OFL发行，不属于Apache代码。预算、生成与支持边界见 [字体port](FONT_PORT.md)。

## LittleFS设备port

固定esp_littlefs 1.20.3/8274371dc5912196f66ac3e71dbb6291760cb8b0（MIT）及其核心adad0fbbcf5382c20978d07f94f9c13be9041c1b（BSD-3-Clause），完整原许可/vendor116文件与摘要保留。采用本地固定源码避免组件注册表网络阻断，配置不修改vendor；几何与初始化边界见 [设备接入](DEVICE_PORT.md)。

## zlib资源解压

固定zlib1.3.2，官方zlib.net/zlib-1.3.2.tar.gz及SHA256 bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16。完整254原文件与许可证保留，摘要在LICENSES/zlib-manifest.json；仅构建inflate相关六个源文件，外部定义Z_SOLO/Z_PREFIX，所有inflate分配走调用方pn_pool。不改vendor，不使用系统zlib或ROM不同解码器替换同源测试。初始评估1.3.1未作为最终依赖，已换为官方1.3.2审计修订版本并复跑测试；旧源码只在ignored build-dev。此裁剪不是安全沙箱，恶意语料/时间片仍须后续验收。

## Expat出版物XML

固定官方Expat2.9.0归档，SHA256 1e6371862cc31999b368c3b89b49994f0677e1bab5f1b2b85ae3741f5d803051。178原文件与COPYING和附带许可保持原样，LICENSES/expat-manifest.json纳入自动核对；运行库选MIT，原构建文件等附带许可一并保留。只构建xmlparse/xmlrole/xmltok/xcs，外部配置与allocator，不改vendor。输入、实体、随机盐和预算边界见 [出版物结构](EPUB_PUBLICATION.md)。

## libspng扫描线图片

固定libspng0.7.4，262个归档原文件、BSD-2-Clause LICENSE及其他资料附带许可保留，LICENSES/spng-manifest.json逐文件核对。外部配置通用实现、自定义分配和只读编码拒绝，不修改vendor，不扩展zlib六个inflate源。支持边界、资源CRC、缩放与预算见 [PNG图片层](PNG_RENDERING.md)。


## libjpeg-turbo3.1.4.1

官方源码包固定SHA256 ecae8008e2cc9ade2f2c1bb9d5e6d4fb73e7c433866a056bd82980741571a022，来源 [官方发行](https://github.com/libjpeg-turbo/libjpeg-turbo/releases/tag/3.1.4.1)。完整vendor633文件与原IJG/BSD/zlib附带条款保留；manifest及check_docs核对每个哈希。通用decode-only编译选原wrapper，不改源码，系统内存端口另在pn_jpeg_memory.c，以pn_pool替代jmemnobs，无backing文件。功能与未完集成见 [JPEG层](JPEG_RENDERING.md)。
