# 开发环境与测试说明

当前基线0.0.56。可运行命令统一见 [BUILDING](BUILDING.md)，功能/待办见 [PROJECT_STATUS](PROJECT_STATUS.md)，发布门保留在 [VALIDATION](VALIDATION.md)。以下区分“代码运行在哪里”和“能证明什么”。

## ESP32-S3与PC模拟的区别

PC运行同源C阅读、字体、布局、状态机及UI，替换文件/显示/时钟等port；SDL显示相同4bpp画面。PC不会执行真实ESP32-S3固件，也不能模拟真实墨水屏波形、PMU、PSRAM带宽、射频、温度或功耗。

部分设备模块在主机用显式SDK stub运行：真实handler、监督任务、阅读位置和文件worker与模拟网络/驱动组合。真实SDK分支由两套ESP-IDF构建检查。QEMU/Wokwi仍未接入，不能承诺现有固件在其中启动；此前文档中的示意命令不是已交付入口。

## 环境清单

| 环境 | 当前使用/要求 | 说明 |
| --- | --- | --- |
| Windows主机 | Python 3.12.8、PowerShell | 文档检查、字体生成、独立图像/QR核对 |
| Docker Linux | Docker Engine 29.2.1；本机default context | 固定IDF镜像，实际连接状态必须现场核对 |
| ESP工具链 | ESP-IDF v6.1固定镜像摘要 | 同一版本的CI和Zbit板级配置，不混用SDKCONFIG |
| 主机测试 | 容器C11/CMake/Ninja/Python、ASan/UBSan、pthread | 原生Linux；代码依赖POSIX，Windows原生构建未验证 |
| SDL | tools/docker构建pico-nano-sdl:dev | 固定IDF基础镜像+SDL2；dummy测试不产生可见窗口 |
| 可见PC窗口 | Linux/WSL+SDL2+图形会话 | 同源阅读器，当前Windows Docker不代替WSLg窗口环境 |
| 字体工具 | fontTools 4.51.0、固定Noto源SHA | 子集/OFL/家族改名，manifest记录可复现版本 |
| JS摘要oracle | Node v24.19.0（本机） | tests/tools/test_transfer_sha.mjs与crypto比较；固件无Node依赖 |
| QR独立oracle | 实际cv2模块4.13.0、headless发行4.13.0.92（本机） | tools/test_qr_decode.py准确比较演练UTF8载荷；不是手机相机实测 |
| 原始书/字体 | mockDoc/绍宋.epub、LXGWWenKai-Regular.ttf | 私有样本不提交，原文件只读，副本/报告在忽略目录 |
| 真机 | RDP-G01-W、S3/16MiB flash/8MiB PSRAM | 尚未烧写本项目；硬件与性能门待完成 |

本机同时存在opencv-python/contrib 4.12.0.88和headless 4.13.0.92，实际导入cv2为4.13.0；为避免复现时混包，推荐独立测试venv仅安装opencv-python-headless==4.13.0.92。没有清理或改变用户全局环境。

这些是当前观察版本，非自动升级要求。部署测试环境应记录自己的版本；缺Node/OpenCV时只能说未执行对应oracle，不能把缺失当通过。软件包不安装到产品固件。

## 可复跑测试分层

| 层次/入口 | 检查内容 | 不能证明 |
| --- | --- | --- |
| docs | 链接/围栏、16MiB分区、原始vendor/许可/字体摘要 | 功能运行与硬件安全 |
| host CTest | 解码/布局/字体、身份/A-B状态、书签/样式、显示回执、租约/故障注入 | 真机性能/掉电/屏幕 |
| host真实文件恢复 | 同步点进程终止、恢复/覆盖/备份、配额/空间、格式损坏拒绝 | FAT/SD控制器内缓存断电保证 |
| transfer_http | 实际TCP、认证/来源、重启续传、资源安装与HTTP退出drain | ESP SDK解析器和无线速度 |
| device_* SDK-stub | 同源设备main/handler/监督任务、VCOM启动门、资源移交/停止、READ禁止写 | 实际FreeRTOS调度、RF/ADC/SDK网络 |
| SDL CTest | dummy窗口、输入脚本、书库/目录/书签/排版/字体交互 | 真实触摸/三键、面板或可见窗口人工验收 |
| sim headless | PGM、预算失败、固定ownership场景 | 完整产品交互与S3执行 |
| JS/QR独立oracle | 摘要边界独立比较、矩阵解码原文准确性 | 浏览器/手机与真实墨水屏全部兼容 |
| firmware-ci/board | 实际SDK编译/链接、分区空间与size | 已烧写、启动、无线或功耗验收 |

仓库已有.github/workflows/build.yml配置，但本轮没有推送或取得托管CI运行结果；本机firmware-ci是CI配置的Docker编译，不是GitHub绿灯。

当前全量计数由本次CTest输出为准，不在测试计划中硬编码旧版本数量。失败应先定位源/fixture/环境差异；修复后重跑受影响检查，涉及共享行为再跑全量。不要为通过测试削弱断言；字体子集改变“原来缺字”的前提时，用固定稀有fixture继续检查真正的回退。

## 日常与故障检查

1. 阅读目标AGENTS和功能维护契约，确认共享media/pool/显示owner的责任。
2. 新能力先在主机建立实际行为/失败用例，复用同源实现，避免另写假产品。
3. 按BUILDING运行host及受影响SDL、两SDK和docs；保存完整日志于build-dev/logs。
4. 观察超时不等于失败。先检查同一进程/会话；只有明确终止或源码/生命周期变化才重新运行。
5. 原始样本只读，输出使用独立build-dev根；CMake/Linux ELF与实际固件产物区别报告。
6. 核对Git暂存文件，只提交源/必要资产/测试/长期文档与许可证。过程日志、样本、capture、build、临时计划不提交。

Linux/WSL原生host可用ctest -R筛选名称，或ctest -L筛选已有标签；不是所有测试都带标签。字体全文件扫描/资源CRC等可耗时，不用短观察超时重启同一任务。现有报告不得把pn_pool峰值当整机内存，SDL ARGB开销不混入S3预算。

## 专项重跑

- PC HTTP：先host构建pn_transfer_host，再按TRANSFER_WEB启动；默认回环，配对码仅当前会话，退出等待已接收请求。测试根不能复用mockDoc或正在阅读的书库。
- 摘要：`node tests/tools/test_transfer_sha.mjs`，验证块边界/填充/任意分块；浏览器页面无需安全上下文WebCrypto。
- 二维码：按TRANSFER_QR生成PGM，再用本机OpenCV独立解码；捕获是演练口令，截图不是设备照片。
- EPUB：epub-sample核对全部ZIP资源；epub_dump/body_probe/epub_capture等边界见对应格式维护文档，不把单一容器检查当阅读验收。
- UI字体：固定源哈希，fontTools按manifest；--replay复现已保存字符集，扩展新UI时正常生成并复核glyph/许可/相关回退fixture。

## 真机验收仍待执行

先确认Zbit时序、已标定PMU VCOM与板型，再按独立维护流程烧写；本轮开发命令不烧写或擦分区。不得用CI默认时序替代真机配置，不写SY标定，不自动格式化/擦NVS。

必须补齐安全启动与错误提示、实际TXT/EPUB/字体与位置返回、手机AP/STA/扫码上传、拔卡/停止/掉电恢复、任务栈与PSRAM、刷新温度/残影、输入响应、睡眠/唤醒/功耗和USB/升级门。性能目标、样本集及统计规则见VALIDATION；没有测量结果时标注“未验证”，不以编译或模拟图替代。
