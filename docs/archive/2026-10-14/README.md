# 2026-10-14 文档归档

本批保留归档前工作区中的全部已登记项目 Markdown（含未提交的阅读优化说明）及仓库 README。这里的状态、方案、版本、测试计数和品牌描述属于历史记录；当前实现以[现行文档入口](../../README.md)为准，不把旧目标当成已交付能力。

可浏览副本加注历史归档标识，并重定位 Markdown 相对链接；原正文保留。图片继续引用现行 docs 资源目录；复用组件、许可证和既有发布包未移动。被忽略的 HANDOFF.local.md、docs/local、本机样本和构建记录留在原位置，不进入公开归档。

[原始快照 ZIP](originals.zip)保存每个文件归档前的完整字节及原路径；[清单](manifest.json)记录原始/浏览副本 SHA-256、源 HEAD 和 ZIP 摘要。ZIP 的源 HEAD 不表示所有文件均已提交。

共 86 份原始文档。需要专题推导或原需求编号时从下表查找；后续修订写入现行主文档，历史副本不继续维护。

| 历史文档 | 原路径 |
| --- | --- |
| [PicoNano 阅读固件](REPOSITORY_README.md) | `README.md` |
| [技术架构与维护契约](ARCHITECTURE.md) | `docs/ARCHITECTURE.md` |
| [异步输入、显示回执与邻页预排](ASYNC_READING.md) | `docs/ASYNC_READING.md` |
| [TXT书签界面与编辑](BOOKMARK_UI.md) | `docs/BOOKMARK_UI.md` |
| [PicoNano 品牌与默认图](BRANDING.md) | `docs/BRANDING.md` |
| [构建与运行入口](BUILDING.md) | `docs/BUILDING.md` |
| [文件书架与选择阅读](CATALOG.md) | `docs/CATALOG.md` |
| [功能变化](CHANGELOG.md) | `docs/CHANGELOG.md` |
| [书架封面与缓存](COVERS.md) | `docs/COVERS.md` |
| [日期与网络对时](DATE_TIME.md) | `docs/DATE_TIME.md` |
| [依赖与源码来源](DEPENDENCIES.md) | `docs/DEPENDENCIES.md` |
| [PicoNano 独立阅读固件总体设计](DESIGN.md) | `docs/DESIGN.md` |
| [开发环境与测试说明](DEVELOPMENT_AND_TESTING.md) | `docs/DEVELOPMENT_AND_TESTING.md` |
| [PicoNano 阅读固件开发计划](DEVELOPMENT_PLAN.md) | `docs/DEVELOPMENT_PLAN.md` |
| [设备TXT入口与内部存储接入](DEVICE_PORT.md) | `docs/DEVICE_PORT.md` |
| [设备热点传输入口](DEVICE_TRANSFER_ENTRY.md) | `docs/DEVICE_TRANSFER_ENTRY.md` |
| [ESP-IDF设备HTTP上传适配](DEVICE_TRANSFER_HTTP.md) | `docs/DEVICE_TRANSFER_HTTP.md` |
| [设备上传存储适配](DEVICE_TRANSFER_STORAGE.md) | `docs/DEVICE_TRANSFER_STORAGE.md` |
| [设备WiFi会话生命周期](DEVICE_WIFI.md) | `docs/DEVICE_WIFI.md` |
| [原生EPUB应用与PC窗口](EPUB_APP.md) | `docs/EPUB_APP.md` |
| [XHTML正文事件与语义位置](EPUB_BODY.md) | `docs/EPUB_BODY.md` |
| [EPUB语义书签](EPUB_BOOKMARKS.md) | `docs/EPUB_BOOKMARKS.md` |
| [EPUB容器与本地资源层](EPUB_CONTAINER.md) | `docs/EPUB_CONTAINER.md` |
| [EPUB目录模型](EPUB_NAVIGATION.md) | `docs/EPUB_NAVIGATION.md` |
| [EPUB页节点与真实捕获](EPUB_PAGINATION.md) | `docs/EPUB_PAGINATION.md` |
| [EPUB语义位置持久化与保存屏障](EPUB_PERSISTENCE.md) | `docs/EPUB_PERSISTENCE.md` |
| [EPUB出版物结构](EPUB_PUBLICATION.md) | `docs/EPUB_PUBLICATION.md` |
| [TXT/EPUB混合书架、最近阅读与设备入口](EPUB_ROUTING.md) | `docs/EPUB_ROUTING.md` |
| [原生EPUB导航会话与显示回执](EPUB_SESSION.md) | `docs/EPUB_SESSION.md` |
| [EPUB作者样式与图片版式](EPUB_STYLES.md) | `docs/EPUB_STYLES.md` |
| [中文EPUB目录界面与跳转](EPUB_TOC_UI.md) | `docs/EPUB_TOC_UI.md` |
| [在另一台电脑升级当前阅读器](FLASH_ON_ANOTHER_PC.md) | `docs/FLASH_ON_ANOTHER_PC.md` |
| [字体上传与锁屏壁纸](FONTS_AND_WALLPAPERS.md) | `docs/FONTS_AND_WALLPAPERS.md` |
| [正文字体回退](FONT_FALLBACK.md) | `docs/FONT_FALLBACK.md` |
| [字体目录、信息与样例预览](FONT_INSPECTION.md) | `docs/FONT_INSPECTION.md` |
| [字体管理与缺失回退](FONT_MANAGEMENT.md) | `docs/FONT_MANAGEMENT.md` |
| [阅读字体选择](FONT_PICKER_UI.md) | `docs/FONT_PICKER_UI.md` |
| [字体引擎与真实TXT捕获](FONT_PORT.md) | `docs/FONT_PORT.md` |
| [全局与逐书字体选择记录](FONT_PREFERENCES.md) | `docs/FONT_PREFERENCES.md` |
| [阅读中的字体预览与逐书应用](FONT_SWITCHING.md) | `docs/FONT_SWITCHING.md` |
| [格式、元数据与封面设计](FORMATS.md) | `docs/FORMATS.md` |
| [PicoNano 硬件基线](HARDWARE.md) | `docs/HARDWARE.md` |
| [首次上板与恢复准备](HARDWARE_BRINGUP.md) | `docs/HARDWARE_BRINGUP.md` |
| [出版物PNG/JPEG图片资源](IMAGE_RESOURCES.md) | `docs/IMAGE_RESOURCES.md` |
| [JPEG原生解码层](JPEG_RENDERING.md) | `docs/JPEG_RENDERING.md` |
| [家庭网络与局域网传书](LAN_TRANSFER.md) | `docs/LAN_TRANSFER.md` |
| [LocalSend 接收模式](LOCALSEND.md) | `docs/LOCALSEND.md` |
| [LocalSend PC HTTP 开发入口](LOCALSEND_PC.md) | `docs/LOCALSEND_PC.md` |
| [LocalSend 元数据与会话核心](LOCALSEND_SESSION.md) | `docs/LOCALSEND_SESSION.md` |
| [附近传书确认与状态界面](LOCALSEND_UI.md) | `docs/LOCALSEND_UI.md` |
| [锁屏与壁纸缓存](LOCK_SCREEN.md) | `docs/LOCK_SCREEN.md` |
| [内存、缓存、索引与响应优化](MEMORY_AND_RESPONSE.md) | `docs/MEMORY_AND_RESPONSE.md` |
| [翻页手势、屏下三键与设置页](PAGE_TURN.md) | `docs/PAGE_TURN.md` |
| [内容身份与持久位置维护契约](PERSISTENCE.md) | `docs/PERSISTENCE.md` |
| [原生PNG与灰阶图片](PNG_RENDERING.md) | `docs/PNG_RENDERING.md` |
| [锁屏浅睡与自动锁屏](POWER.md) | `docs/POWER.md` |
| [产品需求与用户流程](PRODUCT_REQUIREMENTS.md) | `docs/PRODUCT_REQUIREMENTS.md` |
| [项目状态与待办](PROJECT_STATUS.md) | `docs/PROJECT_STATUS.md` |
| [TXT阅读会话、PC交互与续读契约](READER_SESSION.md) | `docs/READER_SESSION.md` |
| [阅读首页与独立书库](READING_HOME.md) | `docs/READING_HOME.md` |
| [阅读索引与浅睡热恢复](READING_INDEX.md) | `docs/READING_INDEX.md` |
| [阅读响应与清残影开发计划](READING_RESPONSE_PLAN.md) | `docs/READING_RESPONSE_PLAN.md` |
| [上次位置、自动保存与书签维护契约](READING_STATE.md) | `docs/READING_STATE.md` |
| [最近20本与继续阅读](RECENT_READING.md) | `docs/RECENT_READING.md` |
| [字体、段落、分页与墨水屏刷新](RENDERING.md) | `docs/RENDERING.md` |
| [存储卡目录与上传规则](SD_CARD_LAYOUT.md) | `docs/SD_CARD_LAYOUT.md` |
| [书架外观](SHELF_APPEARANCE.md) | `docs/SHELF_APPEARANCE.md` |
| [依据、取舍与技术门](SOURCES_AND_DECISIONS.md) | `docs/SOURCES_AND_DECISIONS.md` |
| [存储租约与显示任务维护契约](STORAGE_AND_DISPLAY.md) | `docs/STORAGE_AND_DISPLAY.md` |
| [TXT解码、源映射与分页维护契约](TEXT_CORE.md) | `docs/TEXT_CORE.md` |
| [快速传书与导入方案](TRANSFER.md) | `docs/TRANSFER.md` |
| [扫码传书](TRANSFER_QR.md) | `docs/TRANSFER_QR.md` |
| [整文件流式接收](TRANSFER_RECEIVE.md) | `docs/TRANSFER_RECEIVE.md` |
| [同源串行传输服务](TRANSFER_SERVICE.md) | `docs/TRANSFER_SERVICE.md` |
| [中文传输网页与PC服务](TRANSFER_WEB.md) | `docs/TRANSFER_WEB.md` |
| [有界传输工作任务](TRANSFER_WORKER.md) | `docs/TRANSFER_WORKER.md` |
| [逐书排版设置与草稿](TYPESETTING_SETTINGS.md) | `docs/TYPESETTING_SETTINGS.md` |
| [界面组件与图标](UI_KIT.md) | `docs/UI_KIT.md` |
| [Pico Mono Glass V2 落地对照](UI_SPEC_V2.md) | `docs/UI_SPEC_V2.md` |
| [界面与交互规范](UI_UX.md) | `docs/UI_UX.md` |
| [上传文件安装与恢复](UPLOAD_FILES.md) | `docs/UPLOAD_FILES.md` |
| [上传持久事务内核](UPLOAD_TRANSACTION.md) | `docs/UPLOAD_TRANSACTION.md` |
| [PicoNano 1.0 开发执行计划](V1_DEVELOPMENT_PLAN.md) | `docs/V1_DEVELOPMENT_PLAN.md` |
| [PicoNano 1.0 产品与验收计划](V1_PRODUCT_PLAN.md) | `docs/V1_PRODUCT_PLAN.md` |
| [验证、性能与发布门槛](VALIDATION.md) | `docs/VALIDATION.md` |
| [网页安装器](WEB_INSTALLER.md) | `docs/WEB_INSTALLER.md` |
