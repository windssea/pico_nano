# 依据、取舍与技术门

日期：2026-10-05。来源核对用于设计，不代表新固件实测。参考仓库 `D:/windssea/dev/read_pico_firmware`，提交 `28cde682a4468a581c278761922724f57d976418`。

## 1. 本地依据

| 来源（参考仓库内） | 已确认内容 | 新项目如何使用 |
| --- | --- | --- |
| AGENTS.md、README.md、docs/ONBOARDING.md | 板型、IDF v6.1、VCOM/电源规则、页面与注释契约 | 保留硬件约束；产品 UI 重新设计 |
| main/app_main.c、main/app/app_loop.c | 启动、手势取消、菜单、卡失效、字体重试 | 新建异步 shell；沿用事件边界思路 |
| main/display.c | GL16 全像素、GC16 周期、8秒轨空闲、12MHz欠载恢复 | 唯一显示 owner，恢复目标页 |
| e0470_epaper_waveform.h | 36/37 相、约430ms、8灰阶约360ms | 性能目标按波形约束，不编造毫秒级整页刷新 |
| sdkconfig.defaults、read_pico_flash_hpm.c | 120MHz/HPM Zbit绑定与温漂说明 | 初始验证保留来源，另测保守档，不改厂表 |
| main/book/book_source/layout/progress/store | 按章加载、增量页、路径大小进度、挂载可格式化 | 拆分复用；改内容身份与非破坏挂载 |
| main/font/ttf_font.c/.h | TTF按需读取、度量/缓存、gvar、1.5MiB cache | 双 font context、预算、轴泛化、边界校验 |
| read_pico_transfer.c、transfer_font.h、upload.html | AP/STA、part/backup、名字和TTF校验 | 提升为持久分块会话，保留真实网页测试 |
| components/epdiy/LICENSE | 裁剪 fork、LGPL-3.0-or-later | 记录来源、保留许可与发布所需材料 |
| main/book/vendor/README.md、sdcard/README.md | JPEG/PNG解码器与字体许可 | 移植必须保留对应许可证/版权 |

原仓库不得因本项目设计被修改。新工程复用时逐组件复制并建立来源 manifest，不能把未使用 demo、私有日志与整套生成表维护职责一起搬过去。

## 2. 官方资料（2026-10-05 查阅）

- [W3C EPUB 3.3](https://www.w3.org/TR/epub-33/)：EPUB 使用 HTML/CSS 等资源，区分重排与固定布局；本项目明确为受限重排子集，不宣称全规范阅读器。
- [ESP-IDF v6.1 TinyUSB MSC 示例](https://github.com/espressif/esp-idf/blob/v6.1/examples/peripherals/usb/device/tusb_msc/README.md)：MSC 的介质由应用或主机访问，不能同时访问。此例证实软件能力，不能证明本板 USB 走线与恢复已验证。
- [MuPDF 官方 Releases](https://mupdf.com/releases)：AGPL 与商业授权；只列候选，不宣称 ESP32-S3 移植成功或指定未经核验的版本。
- [calibre PDF 转换说明](https://manual.calibre-ebook.com/en/conversion.html#pdf-conversion)：PDF 提取为重排文本有结构保真限制，所以转换不能替代用户的 PDF 原生阅读需求。

- [ESP-IDF v6.1 QEMU](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/tools/qemu.html)与 [官方外设表](https://github.com/espressif/esp-toolchain-docs/blob/main/qemu/README.md)：S3可做部分芯片仿真，虚拟显示不能证明EPD/无线/USB整板可用。
- [ESP-IDF Linux host](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/host-apps.html)：实验性、组件有限；本项目核心采用独立CMake共享源测试。
- [Wokwi支持芯片](https://docs.wokwi.com/getting-started/supported-hardware)与 [IDF接入](https://docs.wokwi.com/vscode/esp-idf)：支持S3，整板所需外设模型需另核验。
- [esp-emulator Beta](https://github.com/espressif/esp-emulator)：官方声明支持S3；当前只作为另行评估候选，不列发布必过门。

依赖引入时再次核对版本、构建选项与许可，固定 release/commit，记录 SHA，不使用浮动 latest。桌面工具与固件依赖的许可证分别列出，不能把商业授权假设当成已经购买。

## 3. 决策记录

| 决策 | 当前选项与理由 | 验证门 | 门未通过 |
| --- | --- | --- | --- |
| UI 框架 | 原生 C，细控 damage/波形与预算 | M0 少量设置页与 LVGL 资源对比 | 有明确收益再换，文档与接口同步 |
| PDF | 原生独立引擎候选 MuPDF | 中文/扫描/复杂页、峰值、映像、许可 | 不能发行声称PDF完整版，重新评估 |
| 字体 | TTF、双 context、UI常驻 | 字体/gvar/内存与缺字测试 | 静态字体先交付，可变轴不假支持 |
| 传书 | AP/STA 网页为主、MSC为辅 | ACK持久性、吞吐、手机兼容、USB硬件 | 提升已实测通道，明示MSC状态 |
| 身份 | SHA-256 内容＋路径实例分离 | 外部替换/复制/换卡/恢复 | 摘要未完成不恢复旧进度 |
| 存储 | 非破坏挂载、A/B日志与事务 | 故障点断电/拔卡 | 只读恢复，不静默格式化 |
| 120MHz | 仅已识别Zbit实验基线 | 多板、低高温、持续PSRAM/显示/WiFi | 若无法稳定，采用验证后的保守频率档；原仓默认不动 |
| OTA | 两个6MiB槽 | 实际PDF映像≤槽的90%且回滚成功 | 裁剪引擎或调整产品资源，不省回滚 |
| 字体上传 | 网页/USB导入TF，设备预览确认选择 | 32MiB限额、字形边界、使用中替换/删除 | 保留旧字体与UI常驻字库，缺卡明确提示 |
| 锁屏壁纸 | 原图TF，处理后A/B位图独立内部1MiB缓存 | 分区实际空间、坏图/断电/拔卡/唤醒 | 保留旧缓存或系统默认，不阻塞保存与睡眠 |

## 4. 风险登记

| 风险 | 严重度 | 早期证据 | 控制 |
| --- | --- | --- | --- |
| PDF引擎超8MiB/6MiB映像或许可不匹配 | 最高 | M0/M1技术样机 | 最先验证，禁止最后才移植 |
| PDF CJK字体/过滤器不兼容 | 高 | 40本PDF有分层样本 | 错误公开、单页恢复、可选转换 |
| PSRAM带宽影响显示队列 | 高 | 翻页＋预绘制、WiFi负载欠载 | 并行限额、prefill与安全pclk |
| MSPI温漂不稳定 | 高 | 冷热启动、长时间压力 | 专门硬件门，保守时序备选 |
| 不可信书/字体触发解析器错误 | 高 | fuzz/ASan/UBSan与限额样本 | 有界读取、budget allocator、错误释放 |
| FAT掉电覆盖丢书 | 高 | commit各阶段故障注入与真实断电 | backup＋持久phase恢复，不假定rename全事务原子 |
| USB与固件同时写卡 | 高 | lease互斥测试 | 交接/卸载/重新扫描状态机 |
| 封面耗时拖慢首页 | 中 | 冷1000本书库 | 元数据先显示，封面队列分批 |
| 章节/位置重排漂移 | 高 | 字体段落切换、解析器升级 | 源映射与锚点、layout版本 |
| 手机无网热点跳出/熄屏 | 中 | iOS/Android真实手机 | 两步二维码、说明、续传 |

## 5. 仍需证据的结论

PDF 是否能达到常规样本预算、USB是否可用、可变字体通用轴范围、实际可辨灰阶与残影、120MHz产品温度范围、真实电池续航，全部为未验证。每项在 DEVELOPMENT_PLAN 与 VALIDATION 有前置任务和通过条件。本项目不承诺“所有常见文件无条件支持”或未测的天数续航。
