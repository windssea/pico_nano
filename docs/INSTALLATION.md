# PicoNano 安装、异机升级与发布

更新：2026-10-14。默认通过电脑 Chrome/Edge 的 Web Serial 升级，在线入口为[网页安装器](https://windssea.github.io/pico_nano/)。当前默认固件 `0.6.1+7c51387`（开机右下角与“设置 → 存储与关于”显示版本号）。网页安装与设备无线OTA是不同能力，当前没有已交付的OTA原子回滚。

## 另一台电脑网页升级

1. 退出阅读器无线传书/对时，等待USB数据接口恢复；用数据线接到另一台电脑，关闭占用串口的程序。
2. 在电脑Chrome/Edge打开上述HTTPS站点，选择带源码后缀的最新专用升级版本，核对型号RDP-G01-W、ESP32-S3、16MiB Flash。手机浏览器是否有串口能力不能假定，不把手机上传页当固件安装页。
3. 连接真实端口；安装器只读核对已知旧设备分区表，计划应仅包含应用 `0x20000` 一项。分区不匹配或短读必须停止，不能改用旧首次安装包覆盖。
4. 开始写入，确认设备端MD5校验，再观察复位/USB重新枚举/正常开机。写入校验成功、自动复位触发和产品正常运行分别记录。
5. 开书/翻页/锁解锁/传书/保存重开验证，尤其记录插线与拔线的网页响应及上传失败原因；没有验收的数据不要称稳定修复。

也可用发布目录中 `PicoNano-web-installer-<提交>.zip`：解压后运行 `py serve.py`，保持终端，在Chrome/Edge打开 `http://localhost:8765/`。这只需Python提供本地站点，不需ESP-IDF/Docker/esptool；不能直接双击HTML使用Web Serial。

## 保留布局升级的写入契约

当前专用升级以[已知旧设备表](../releases/pico_nano-0.0.57-6840523-upgrade/expected-partition-table.bin)为基准：从 `0x8000` 只读3072字节并严格比较，匹配后只写 `0x20000` 应用，不写bootloader、分区表、otadata、NVS、内部数据/壁纸或TF。清除/初始化选项禁用。适用前提是该已知布局、当前ota_0路径，不能推广到其他板型/布局/ota_1运行设备。

仓库新分区与旧设备NVS大小不同，不能用 `build-board/flash_args` 的完整写入计划替代升级，不执行erase-flash。串口写应用中断可能无法启动，需要按板级已验证方式重新进入下载模式并重写；它没有设备OTA切槽/回滚保证。已有16MiB备份单独保管，不放进公开升级包。

## 命令行异机升级

下载对应发布目录的 `PicoNano-<提交>-upgrade.zip`，解压到普通可写目录，例如 `C:\PicoNano`。另一台电脑只需Python与指定esptool，不需本仓库或SDK。

```powershell
cd C:\PicoNano
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install esptool==5.3.1
.\.venv\Scripts\python.exe flash_upgrade.py
```

最后一条只检查包内SHA，不连接设备，应显示 `Package checks passed`。列出端口，把COM5替换为实际端口：

```powershell
.\.venv\Scripts\python.exe -m serial.tools.list_ports -v
.\.venv\Scripts\python.exe flash_upgrade.py --port COM5 --flash
```

默认115200。命令行脚本从0x8000读回4KiB保存 `device-partition-readback.bin`，检查读回长度及前3072字节与已知表逐字节一致，匹配后写应用/校验/复位。重试前另存读回文件；不匹配停止，不擦整片。手动进入下载模式的板级方法必须确认，电源键不等同BOOT；只有板上实际存在BOOT/RESET且该流程已验证时才用相应按键。可按实际串口通道选择脚本 `--before no-reset/default-reset`，不盲装串口桥驱动。

## 自动复位原因与处理

旧 esptool-js0.5.7 的 `after("hard_reset")` 只走RTS策略，缺少原生S3 USB退出强制下载模式的RTC/watchdog序列。自有 [installer/reset.js](../installer/reset.js) 先清RTC `FORCE_DOWNLOAD_BOOT`位；识别Espressif原生USB303A:1001后使用RTC watchdog复位，其他串口走RTS。不修改vendor、不写Flash/eFuse/PMU。

watchdog触发后USB可能在返回响应前断开，Windows端口号也可能变化；界面区分“写入校验成功”和“自动复位未完成”。这条原因来自代码路径，复位适配已有软件回归，真实网页复位/新电脑重新枚举/正常启动仍待用户复验。触发前失败明确提示，不能把超时直接当作已正常开机。

无端口先确认数据线、接口、电源与USB恢复；一直Connecting确认端口和板级下载模式。写入哈希已过但末尾串口报错时，先观察正常开机/重新枚举，必要时按板级方法退出下载模式。写入中断重新进下载模式保留日志再升级，不据复位错误执行擦除。

## 首次安装与旧包

旧完整首装包仍在历史发布目录，但不作为当前已安装设备的默认升级。它会写应用/otadata/bootloader和分区表，可能初始化新的NVS扇区/空内部镜像；用户明确选择清除或初始化会丢失对应记录。LittleFS头部识别不代表FS健康，头部全FF也不代表整分区为空。无独立型号/备份/布局确认不使用此路径。

旧完整方案的详细写入矩阵保留在[安装器归档](archive/2026-10-14/WEB_INSTALLER.md)，作为历史说明；当前新工作应先评估分区迁移及恢复，不直接覆盖旧设备。现有默认应用升级不执行上述首装路径。

## 开发者发布流程

用户要求：每批fix完成先询问是否构建并发布，收到授权后执行。源提交、固件构建、发布包和Pages部署均要可核对；不能把未构建的优化写进已发布manifest。

授权后先完成相关软件/可见窗口回归，提交需要发布的源码；复跑两套配置/size，确认board配置120MHz、USB/RF互斥及非诊断构建，slot余量目标至少10%。再打专用升级包：

```powershell
python tools/dev.py firmware-ci
python tools/dev.py firmware-board
python tools/package_upgrade.py --reference releases/pico_nano-0.0.57-6840523-upgrade/expected-partition-table.bin
python tools/dev.py installer
```

[package_upgrade.py](../tools/package_upgrade.py) 要求已登记源码干净，按 `pn_brand.h` 的 `PN_PRODUCT_VERSION` 生成 `releases/pico_nano-<版本>-<源码提交>-upgrade/` 的应用、已知分区样本、脚本、README、manifest/SHA及命令行ZIP。`tools/dev.py installer`测试并生成本地站点；[installer_site.py](../tools/installer_site.py) 支持 `--serve 8765`。现有离线网页ZIP是另行封装站点并附serve.py的发布产物，没有独立自动打包入口，后续需补齐可复跑工具；构建文件和参考表摘要必须匹配包清单。

提交并推送发布目录后，现有 [installer-pages.yml](../.github/workflows/installer-pages.yml) 通过用户已启用的GitHub Pages重建。核对工作流成功、在线默认源码后缀、应用/分区样本摘要和复位脚本，再给用户刷入。源码提交和发布提交区分，文件名版本相同不表示二进制相同。离线网页ZIP由生成的站点加原 `serve.py` 打包，并在 `PACKAGE_SHA256.txt` 记录两个ZIP摘要。

## 版本与交付证据

已发布 `0.6.1+7c51387` 板级应用4964336字节，6MiB槽约21%余量，SHA-256 `b4d2ce952fcc0d5a63d64727a7f326667f3914bebad602636a2ffbf0d704dbb6`；该值只适用于该发布，后续必须从新manifest读取。在 `0.6+a0a9a97`（4957088字节，长章语义窗口、紧凑语义模型、字形预热、四路字形缓存、整书SHA DMA块与版本号）基础上增加动态调频、去除已显示帧副本、遮罩锁屏平面缓存、ZIP本地头随SHA顺序校验、阅读线深浅与左右半屏点按。插线稳定性、纸屏效果及时间/复位仍需设备验证。

本次文档归档不重新生成既有发布包或installer站点，旧包SHA/脚本/README保持原样。后续包的README使用本页，仓库相对链接自动转换为该包源码提交的GitHub链接，离线解压后仍可查阅引用，不继续指向已归档的独立异机指南。
