# 小纸 Pico 阅读固件 / Read Pico Reader

面向RDP-G01-W的独立ESP32-S3电子书固件，开发目录pico_nano。当前0.0.57开发预览版（未经真机验收）：Mono Glass V2界面、收藏与书籍操作、按百分比跳转、刷新策略与局部刷新、拼音排序与字母跳转、锁屏浅睡、TXT/EPUB、书架封面、锁屏壁纸设置、字体管理、局域网传书、三键与翻页设置、位置/书签、排版/字体、中文网页上传、设备热点/扫码和异步停止已接入代码与软件测试；**完整第一版尚未完成，未进行本项目烧写与真机验收。**

| 入口 | 内容 |
| --- | --- |
| [项目状态与待办](docs/PROJECT_STATUS.md) | 用户需求覆盖、T01–T14状态、下一批工作和未通过门 |
| [构建与运行](docs/BUILDING.md) | 已存在的命令、产物、SDKCONFIG隔离 |
| [网页安装器](docs/WEB_INSTALLER.md) | 浏览器安装/升级固件的写入规则与发布方式 |
| [开发环境与测试](docs/DEVELOPMENT_AND_TESTING.md) | Docker/Windows/WSL/SDL、SDK-stub、独立oracle与硬件边界 |
| [完整开发计划](docs/DEVELOPMENT_PLAN.md) | 原始任务依赖与验收要求，不以局部实现代替任务完成 |
| [总体设计](docs/DESIGN.md) | 产品目标和格式/资源路线 |
| [界面与交互](docs/UI_UX.md) | 设计目标、阅读/设置流程 |
| [界面组件与图标](docs/UI_KIT.md) | 共用控件、抗锯齿图元与线条图标集 |
| [UI 规范 V2 对照](docs/UI_SPEC_V2.md) | Mono Glass V2 设计规范的产品决定、已落地与未落地项 |
| [设备传输入口](docs/DEVICE_TRANSFER_ENTRY.md) | 热点入口、后台停止、返回原书和所有权 |
| [扫码传书](docs/TRANSFER_QR.md) | 连接热点/打开网页两码、独立解码检查 |
| [书架封面](docs/COVERS.md) | EPUB/TXT封面提取、空闲队列、TF缓存与内存边界 |
| [锁屏与壁纸缓存](docs/LOCK_SCREEN.md) | 壁纸设置页、预处理、内部A/B记录、锁屏绘制 |
| [字体管理](docs/FONT_MANAGEMENT.md) | TF字体列表/详情/全局默认/删除与缺失回退 |
| [局域网传书](docs/LAN_TRANSFER.md) | 网页保存家庭WiFi、STA传书与凭据边界 |
| [翻页与三键](docs/PAGE_TURN.md) | 手势阈值、左右手、屏下三键、设置页 |
| [锁屏浅睡](docs/POWER.md) | 电源键/自动锁屏后浅睡与按键唤醒 |
| [PC网页服务](docs/TRANSFER_WEB.md) | 真实HTTP开发入口与资源安装 |
| [格式](docs/FORMATS.md) / [排版](docs/RENDERING.md) | TXT/EPUB/PDF/FB2/CBZ目标与实现边界 |
| [字体与壁纸](docs/FONTS_AND_WALLPAPERS.md) | 字体管理与锁屏图片目标/未完成项 |
| [验证与发布](docs/VALIDATION.md) | 性能统计、可靠性与硬件发布门 |
| [依赖](docs/DEPENDENCIES.md) / [功能变化](docs/CHANGELOG.md) | 固定来源/许可和轻量版本变化 |

## 快速检查

在本目录运行，需要Python 3和可用Docker Linux引擎：

```powershell
python tools/dev.py docs
python tools/dev.py host
python tools/dev.py sdl
python tools/dev.py firmware-ci
python tools/dev.py firmware-board
```

这些命令不烧写。当前本机default context可连接Linux引擎；若默认context失效，按BUILDING核对后仅在当前Shell设置DOCKER_CONTEXT，不盲目重启。

## 硬件与安全边界

ESP32-S3、16MiB flash、8MiB PSRAM、4.7英寸E0470A01，逻辑684×1216。真实板默认120MHz时序依赖Zbit；CI配置仅编译验证。启动只读PMU VCOM，未取得有效值不推屏；不复制VCOM到NVS、不写SY7636A标定/电源时序、不自动擦NVS或格式化TF。阅读/字体/传输/USB按同一介质owner仲裁。

参考仓库D:/windssea/dev/read_pico_firmware保持只读，来源和逐文件摘要见LICENSES/reference-manifest.json。源码、必要UI字体/fixture和许可证提交Git；mockDoc样本、生成捕获、日志、build与本机过程记录忽略。进度和支持范围以PROJECT_STATUS为准，不依据模拟图或旧版本文档宣称真机通过。
