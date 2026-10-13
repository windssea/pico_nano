# PicoNano 阅读固件

面向RDP-G01-W的独立ESP32-S3电子书固件，开发目录pico_nano。当前0.0.57开发预览版（未经完整真机验收）：Mono Glass V2界面、收藏与书籍操作、按百分比跳转、刷新策略与局部刷新、拼音排序与字母跳转、锁屏浅睡、TXT/EPUB、书架封面、锁屏壁纸设置、字体管理、局域网传书、三键与翻页设置、位置/书签、排版/字体、中文网页上传、设备热点/扫码和异步停止已接入代码与软件测试；**完整第一版尚未完成。用户提供的2026-10-12历史包含刷入、启动布局恢复和真机交互问题，不能等同整体验收；2026-10-13这批管理键、操作响应、大图及章缓存改动未刷机。**

| 入口 | 内容 |
| --- | --- |
| [品牌与默认图](docs/BRANDING.md) | PicoNano 名称、开机 logo、默认壁纸与裁剪规则 |
| [项目状态与待办](docs/PROJECT_STATUS.md) | 用户需求覆盖、T01–T14状态、下一批工作和未通过门 |
| [1.0产品与验收计划](docs/V1_PRODUCT_PLAN.md) | 1.0范围、传书/字体/PNG与书封锁屏、约300ppi清晰度、沉浸阅读、书架方案与性能门；微信读书留后续 |
| [1.0开发执行计划](docs/V1_DEVELOPMENT_PLAN.md) | 从当前代码推进的W00–W12工作包、依赖、阶段出口、测试与发布要求 |
| [构建与运行](docs/BUILDING.md) | 已存在的命令、产物、SDKCONFIG隔离 |
| [网页安装器](docs/WEB_INSTALLER.md) | 浏览器安装/升级固件的写入规则与发布方式 |
| [首次上板清单](docs/HARDWARE_BRINGUP.md) | 备份、启动、显示、存储、睡眠与无线验证顺序 |
| [LocalSend接收模式](docs/LOCALSEND.md) | 官方客户端互通设计、整文件接收与验收边界（尚未实现） |
| [存储卡目录](docs/SD_CARD_LAYOUT.md) | 各上传模式共用的资源分类、固定安装目录与封面关联 |
| [整文件接收](docs/TRANSFER_RECEIVE.md) | LocalSend前置基础层：缺省SHA、短读、共享安装与取消（尚无协议互通） |
| [LocalSend会话核心](docs/LOCALSEND_SESSION.md) | 确认代次、授权令牌、JSON边界与取消/恢复（尚无HTTP服务） |
| [开发环境与测试](docs/DEVELOPMENT_AND_TESTING.md) | Docker/Windows/WSL/SDL、SDK-stub、独立oracle与硬件边界 |
| [原始开发计划与任务明细](docs/DEVELOPMENT_PLAN.md) | 保留T01–T14详细要求；当前1.0执行顺序见新版计划，不以局部实现代替任务完成 |
| [总体设计](docs/DESIGN.md) | 产品目标和格式/资源路线 |
| [内存与响应](docs/MEMORY_AND_RESPONSE.md) | 字体Flash/RAM、整机帧与缓存账本、大书冷开/长章及分阶段优化 |
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

这些命令不烧写。2026-10-12本机W00已复跑，当前desktop-linux可连接Linux引擎；Windows真实Python入口、可见模拟器默认样本与资源目录见BUILDING，不依赖应用商店python别名或缺失的mockDoc。只在当前Shell选择已核对的Docker context，不盲目重启。

## 硬件与安全边界

ESP32-S3、16MiB flash、8MiB PSRAM、4.7英寸E0470A01，逻辑684×1216。真实板默认120MHz时序依赖Zbit；CI配置仅编译验证。启动只读PMU VCOM，未取得有效值不推屏；不复制VCOM到NVS、不写SY7636A标定/电源时序、不自动擦NVS或格式化TF。阅读/字体/传输/USB按同一介质owner仲裁。

参考仓库D:/windssea/dev/read_pico_firmware保持只读，来源和逐文件摘要见LICENSES/reference-manifest.json。源码、必要UI字体/fixture和许可证提交Git；mockDoc样本、生成捕获、日志、build与本机过程记录忽略。进度和支持范围以PROJECT_STATUS为准，不依据模拟图或旧版本文档宣称真机通过。
