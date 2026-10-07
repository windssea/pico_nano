# pico_nano开发入口

本仓库是独立阅读器工程，参考仓库read_pico_firmware只读。先读README.md、docs/BUILDING.md和docs/ARCHITECTURE.md；长期任务见docs/DEVELOPMENT_PLAN.md，当前未完工作见被忽略的docs/HANDOFF.local.md。

- 用户已授权在本目录开发。按阶段持续实现；未经用户明确授权不启动子agent。
- 使用ESP-IDF v6.1、ESP32-S3；CI与板级SDKCONFIG隔离。可复跑入口为python tools/dev.py，详见BUILDING。
- 产品名中文“小纸 Pico”，英文/日文“Read Pico”；pico_nano仅内部目录/工程代号。
- VCOM从PMU读取，产品路径不写入或复制到NVS；不写SY7636A标定/电源时序寄存器。不读取有效VCOM则不推屏。
- 默认挂载/恢复不能格式化或静默擦除NVS。未来USB和应用不能并行访问TF。
- 复用组件目前保持原样；摘要和来源在LICENSES/reference-manifest.json。禁止手改波形、stb与生成fallback表，保留原许可证。基础改动不可借整理注释修改vendor组件。
- 新代码注释中英双语，公开API与成员使用///。render只绘制，硬件副作用留适配/控制层。
- 核心、排版、字体与UI使用主机/固件同源，PC只替换port。主机性能及截图不能当面板或功耗实测。
- 计划原始日志/过程审查存忽略的docs/local或build目录；长期行为写功能文档，变化只在docs/CHANGELOG.md轻量记录。
- 不把编译、模拟器测试、真机启动、产品验收混为一谈。实测未通过前不宣称PDF/传输/阅读等计划功能已支持。
