# pico_nano开发入口

本仓库是独立阅读器工程，参考仓库read_pico_firmware只读。文档总入口为docs/README.md；先读README.md、docs/BUILDING.md、docs/HARDWARE.md和docs/ARCHITECTURE.md；问题、需求和后续方案统一见docs/DEVELOPMENT_PLAN.md，当前未完操作见被忽略的docs/HANDOFF.local.md。docs/archive只作历史证据，不以旧计划覆盖当前实现。

- 用户已授权在本目录开发。按阶段持续实现；未经用户明确授权不启动子agent。
- 使用ESP-IDF v6.1、ESP32-S3；CI与板级SDKCONFIG隔离。可复跑入口为python tools/dev.py，详见BUILDING。
- 产品对外品牌统一为“PicoNano”（所有语言）；pico_nano仅内部目录/工程代号。复用硬件、许可证和资源身份保持原名。
- VCOM从PMU读取，产品路径不写入或复制到NVS；不写SY7636A标定/电源时序寄存器。不读取有效VCOM则不推屏。
- 默认挂载/恢复不能格式化或静默擦除NVS。未来USB和应用不能并行访问TF。
- 上传资源必须遵守docs/SD_CARD_LAYOUT.md；所有通道复用pn_upload_directory和共享安装事务，客户端不能选择目录，图片明确分为封面/锁屏。目录常量在pn_sd_layout.h。
- 复用组件目前保持原样；摘要和来源在LICENSES/reference-manifest.json。禁止手改波形、stb与生成fallback表，保留原许可证。基础改动不可借整理注释修改vendor组件。
- 新代码注释中英双语，公开API与成员使用///。render只绘制，硬件副作用留适配/控制层。
- 核心、排版、字体与UI使用主机/固件同源，PC只替换port。主机性能及截图不能当面板或功耗实测。
- 本地开发调试保持可见模拟器窗口，入口见docs/BUILDING.md与tools/run_sim.ps1；源码重建后重启窗口加载新代码，不以dummy测试替代用户观察。
- 计划原始日志/过程审查存忽略的docs/local或build目录；长期行为写现行架构，未完需求写开发指南，优先更新已有主文档；变化只在docs/CHANGELOG.md轻量记录。用户已授权较大的阶段提交完成验证后构建、提交并推送新版本，无需逐批再次询问；不自动刷写设备。
- 不把编译、模拟器测试、真机启动、产品验收混为一谈。实测未通过前不宣称PDF/传输/阅读等计划功能已支持。
