> **历史归档（2026-10-14）**：本文的状态和方案只作历史证据，当前规格、实现与后续工作以[现行文档](../../README.md)为准。

# 在另一台电脑升级当前阅读器

适用于已经刷入 PicoNano、沿用本机已核对旧分区布局的 RDP-G01-W。最新包必须由当前源码执行 `python tools/dev.py firmware-board` 生成，不能拿 CI 配置当板级固件。版本号目前仍为 0.0.57，以包内 manifest 的完整源码提交和应用 SHA-256 区分构建。

升级包只含应用、设备旧分区表校验样本、摘要清单、`flash_upgrade.py` 与说明。只写 ota_0 的 `0x20000`，不写 bootloader、分区表、otadata、NVS、内部数据和壁纸，不访问 TF 或标定。该流程沿用此前启动成功的 ota_0；不能用于原厂首次安装、其他设备或已切换至 ota_1 的设备。设备旧 NVS 为 20KiB，当前仓库默认表为 24KiB，因此本次不能使用完整 IDF flash_args 或旧网页安装器的首次安装计划。

## 网页升级

新网页安装器提供 `0.0.57+源码提交` 专用升级，默认选择最新构建，核对已知旧分区表后只写应用，禁用清除设置和初始化数据选项。不能选择旧 `0.0.57` 首次安装包。网页串口连接与本次实际烧录尚待用户验证。

在线站点已启用：`https://windssea.github.io/pico_nano/`。发布后核对默认版本与摘要，再在电脑Chrome/Edge中连接设备。升级前先退出无线传书，等待USB串口恢复。

无需等待在线站点：下载本发布目录里的 `PicoNano-web-installer-<源码提交>.zip`，解压，在该目录执行 `py serve.py`，保持终端打开，在电脑 Chrome/Edge 打开 `http://localhost:8765/`。选择带源码提交后缀的最新版本，连接设备，核对计划应只有 `0x20000` 一项，然后开始升级。此方式只需 Python 启动本地网页，不需要安装 esptool；不能直接双击 HTML 使用 Web Serial。

## Windows 操作

1. 将升级 ZIP 复制到另一台电脑并解压到普通可写目录，例如 `C:\PicoNano`，不要在压缩包内运行。另存原设备完整 16MiB 备份；备份不放在升级 ZIP 中。
2. 安装 Python 3.12，打开 PowerShell，进入解压目录，建立专用工具环境：

```powershell
cd C:\PicoNano
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install esptool==5.3.1
.\.venv\Scripts\python.exe flash_upgrade.py
```

最后一条只检查包内固件和分区样本 SHA-256，不连接设备；应显示 `Package checks passed`。另一台电脑不需要代码仓库、ESP-IDF、Docker 或 LocalSend。

3. 用可传数据的 USB 线连接阅读器，关闭占用串口的程序。在设备管理器的“端口(COM 和 LPT)”中确认新增端口；也可列出：

```powershell
.\.venv\Scripts\python.exe -m serial.tools.list_ports -v
```

4. 将下面的 COM5 换成另一台电脑实际端口，执行升级：

```powershell
.\.venv\Scripts\python.exe flash_upgrade.py --port COM5 --flash
```

默认 115200 波特率。脚本先只读 `0x8000` 的分区表并保存 `device-partition-readback.bin`，与已知设备布局逐字节比较；不同或短读立即停止，应用不会写入。匹配后写应用，esptool 自动校验设备端摘要，再用 watchdog reset 退出原生 USB 下载模式。看到 `Hash of data verified` 表示应用写入校验通过；是否正常启动另行观察。

## 连接失败与重试

- 没有新增端口：换数据线/电脑 USB 口，核对设备是否开机。如系统出现未知 USB 设备，按实际硬件 VID/PID 选择驱动，不盲装串口桥驱动。
- 一直 Connecting：确认端口，按该板已验证的方式进入下载模式；若实际板上有 BOOT/RESET，则按住 BOOT、短按 RESET 后松开 BOOT。电源键不等同 BOOT。手动进入后可加 `--before no-reset`；外接 USB 串口桥可尝试 `--before default-reset`。
- 重试前将已有 `device-partition-readback.bin` 改名保存，再执行命令。分区不匹配时停止，不尝试整片擦除或覆盖分区。
- 写入哈希已通过，但 watchdog 复位时 Windows 重新枚举导致末尾报串口异常：拔线、正常开机，观察是否启动；端口号可能改变。若仍停在下载模式，按板级复位方式退出，不据复位报错直接认定写入失败。
- 写入中断：保持电量，重新进入下载模式、确认端口，保留旧读回记录后重新运行升级。不要执行 erase-flash。

命令行为依据 [Espressif esptool 基础命令](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html) 与 [原生 USB 下载模式排障](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/troubleshooting.html)。本脚本的自动化测试验证校验门和写入范围，尚未在另一台电脑上实际烧录。

## 刷后验证

开机、开书翻页、检查末行与进度圆点是否一次更新；退出传书，确认不再停在“正在关闭”；返回、保存并重开书核对进度。分开记录固件构建、烧录校验、启动与产品实测。

最新无线修正版暂停传书期间的USB数据接口，退出后恢复；供电/充电保持。该方案针对SDK注明的USB PHY/WiFi干扰，实际插线网页响应改善待用户刷入验证，未通过前仍可拔线传书。LocalSend发现尚未确认，Windows UDP 53317被保留是另一项问题。
