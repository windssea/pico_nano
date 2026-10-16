# PicoNano

基于 ESP32-S3 的独立墨水屏阅读器固件。核心、解析、字体、排版与界面在设备和 PC 模拟器中共用源码，固定目标为 RDP-G01-W/E0470A01、684×1216、16 灰阶、8MiB PSRAM、16MiB Flash。对外品牌在所有语言中统一为 **PicoNano**。

当前开发树为 **0.9.2 OTA 测试预览**，不是完整1.0验收版本；应用与完整恢复包的源码以发布清单的`source_commit`为准。本批加入Gitee签名更新、非活动槽写入与读回校验、显式重启切槽和启动确认/失败回滚逻辑；0.9.1修正首启确认不再依赖TF卡、等待确认时允许自动锁屏、下载超时/断点续传，以及用过OTA后的USB保留升级；0.9.2可在设置的“WiFi 网络”中用屏幕键盘直接配置家庭WiFi，保存前先测试连接。首次启用须通过完整USB包迁移兼容引导程序；真实设备的TLS、双槽互换、断电与回滚仍待验收，不将源码或主机测试当作设备OTA已交付。此前0.8的阅读与菜单修复保留，产品镜像不含测试控制台。

TXT、受限重排 EPUB、字体与排版、阅读线、书签/续读、锁屏图片、AP/STA 网页传书已有源码实现。LocalSend 已有协议和 HTTP/发现基础，设备与官方客户端互通仍未验收；USB MSC已有源码和软件验证，电脑枚举/读写/弹出/串口恢复待真机；PDF、FB2、CBZ与设备OTA回滚尚未交付。用户反馈的插线无线不稳定、网页刷后自动复位及慢开书仍需设备验证，详见开发指南。

从[文档入口](docs/README.md)开始：

国内下载安装包请访问 [Gitee 发行版](https://gitee.com/windssea/pico_nano/releases)。推荐离线网页安装器：解压后运行 `py serve.py`，用电脑 Chrome/Edge 打开 `http://localhost:8765/`；刷机时无需访问 GitHub。GitHub 与 Gitee 使用同一份发布包，详情见[安装与发布](docs/INSTALLATION.md)。0.9设备Wi-Fi OTA处于资格验证阶段；已发布0.8仍使用电脑USB安装。

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
