# PicoNano

基于 ESP32-S3 的独立墨水屏阅读器固件。核心、解析、字体、排版与界面在设备和 PC 模拟器中共用源码，固定目标为 RDP-G01-W/E0470A01、684×1216、16 灰阶、8MiB PSRAM、16MiB Flash。对外品牌在所有语言中统一为 **PicoNano**。

当前版本为 **1.0.1 正式版**，精简内置阅读背景与默认封面，兼容旧排版背景ID，完整校验和保留图片画质不变。此前1.0.0已整合“远山与书页”首页、正文阅读背景、壁纸/遮罩缩略图与原生灰阶缓存、网页图片按用途压缩，以及首屏输入交接与近期记录空闲保存。保留本地书签锁屏、中式黄历、阅读时长统计和完整书籍/字体校验；详细变化见[变更记录](docs/CHANGELOG.md)。

提供保留数据升级包、完整安装包与离线网页安装器，精确源码以包内 `manifest.json` 的 `source_commit` 为准。产品镜像关闭测试控制台；开发设备继续保留独立诊断固件。软件与诊断回归不替代正式镜像的真实睡眠、光学和功耗验收，设备 Wi-Fi OTA 仍处于资格验证阶段，本版只发布 USB 安装包。

从[文档入口](docs/README.md)开始：

国内下载安装包请访问 [Gitee 发行版](https://gitee.com/windssea/pico_nano/releases)。推荐离线网页安装器：解压后运行 `py serve.py`，用电脑 Chrome/Edge 打开 `http://localhost:8765/`；刷机时无需访问 GitHub。GitHub 与 Gitee 使用同一份发布包，详情见[安装与发布](docs/INSTALLATION.md)。当前发行版通过电脑 USB 安装；设备 Wi-Fi OTA 仍待专项验收。

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
