# PicoNano

基于 ESP32-S3 的独立墨水屏阅读器固件。核心、解析、字体、排版与界面在设备和 PC 模拟器中共用源码，固定目标为 RDP-G01-W/E0470A01、684×1216、16 灰阶、8MiB PSRAM、16MiB Flash。对外品牌在所有语言中统一为 **PicoNano**。

当前是 **0.8.0 开发预览**，不是完整1.0验收版本；应用与完整恢复包的源码以发布清单的`source_commit`为准。本批来自首轮真机串口自动测试：修复锁屏绘制中再按电源键导致卡死在锁屏、阅读翻页偶发扫描欠载、下一页预渲染被缓存额度拒绝及遮罩锁屏每次重解码大图；离开阅读时进度与页码索引改在首页显示后落盘；已验证文件身份在无替换租约期间复用；TXT整书探测、圆角描边、书架封面绑定与缩放加速；字体管理可恢复系统默认并修正三处显示问题。实测数据来自诊断镜像，产品镜像不含测试控制台，真机体验与功耗仍待验收。

TXT、受限重排 EPUB、字体与排版、阅读线、书签/续读、锁屏图片、AP/STA 网页传书已有源码实现。LocalSend 已有协议和 HTTP/发现基础，设备与官方客户端互通仍未验收；USB MSC已有源码和软件验证，电脑枚举/读写/弹出/串口恢复待真机；PDF、FB2、CBZ与设备OTA回滚尚未交付。用户反馈的插线无线不稳定、网页刷后自动复位及慢开书仍需设备验证，详见开发指南。

从[文档入口](docs/README.md)开始：

国内下载安装包请访问 [Gitee 发行版](https://gitee.com/windssea/pico_nano/releases)。推荐离线网页安装器：解压后运行 `py serve.py`，用电脑 Chrome/Edge 打开 `http://localhost:8765/`；刷机时无需访问 GitHub。GitHub 与 Gitee 使用同一份发布包，详情见[安装与发布](docs/INSTALLATION.md)。设备 Wi-Fi OTA 列入 **0.9** 交付目标，当前版本尚未支持。

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
