# 网页安装器

在电脑的 Chrome 或 Edge 里通过 Web Serial 安装或升级固件，用户不必安装 esptool、不必记偏移。源码在 `installer/`，静态站点由 `tools/installer_site.py` 从 `releases/` 生成，GitHub Pages 工作流 `.github/workflows/installer-pages.yml` 发布。**安装器与它写入的固件都未经真机验证。**

## 组成

2026-10-14发布 `0.0.57+6840523`：增加无线信号、断连/恢复计数和SDK原因诊断；设置→存储与关于→日期与时间支持手动非阻塞联网校时，取消/超时释放无线后才提交并核验PMU时钟。板级应用4952800bytes，升级槽余21%，仍只写0x20000。已发布的传输恢复、锁屏遮罩、小TXT和阅读线修复均保留；DNS/SNTP、插线传输与设备走时尚未验收。

2026-10-14发布 `0.0.57+0908ed3` 保留布局应用升级：同IP STA重连保留上传会话、卡检测读取错误与拔卡分开、自动网页会话替代配对码、透明遮罩仅锁屏、小TXT省额外loading刷新，以及逐书阅读线设置。板级应用4934304bytes，仍只写0x20000，不改变旧分区或数据；构建与软件验证通过，设备无线、开书时长和纸屏效果等待用户刷入复验。

2026-10-13新增 `0.0.57+c696890` 保留布局升级，固件来源为提交 c696890：只下载应用和已核对旧分区表样本，严格匹配设备布局后只写 `0x20000` 应用；不读取内部文件系统，不写 otadata/bootloader/分区表/NVS/数据。清除和初始化选项禁用。仅适用于当前已从 ota_0 启动的阅读器，不用于首次安装。原有完整安装包与规则仍单独保留。具体操作见 [异机烧录](FLASH_ON_ANOTHER_PC.md)。

GitHub Pages已由用户启用，在线入口为 `https://windssea.github.io/pico_nano/`，后续默认由网页交付。此前c696890在线摘要校验通过，用户已实际网页烧录并反馈自动重启失败；该反馈只证明进入烧录流程，不能等同全部产品验收。新RTC复位与USB无线互斥固件仍待本次刷入复验。发布目录也提供本地网页ZIP（解压后执行`py serve.py`，打开`http://localhost:8765/`）。

| 部分 | 作用 |
| --- | --- |
| `installer/plan.js` | 纯逻辑：解析分区表、识别 LittleFS、生成写入计划、MD5；不碰串口 |
| `installer/app.js` | 串口流程与界面：连接、读设备、备份、写入、校验、重启 |
| `installer/vendor/esptool-js-0.5.7/` | 固定版本的 esptool-js 打包文件（Apache-2.0），摘要见 `LICENSES/esptool-js-manifest.json` |
| `tools/installer_site.py` | 校验完整发布的 SHA256SUMS/flash_args，或专用升级发布的 manifest 摘要，复制固件并写站点清单 |
| `tests/installer/plan.test.mjs` | 写入计划与 MD5 的主机测试 |

## 流程

2026-10-13复位修正：旧esptool-js 0.5.7的 `after("hard_reset")` 仅走RTS策略，没有命令行esptool 5.3.1针对ESP32-S3的清除强制下载标志及watchdog序列。本项目新增自有 `reset.js`，不修改vendor：所有S3复位先仅清RTC FORCE_DOWNLOAD_BOOT位；识别Espressif原生USB VID/PID 303A:1001后使用命令行同样的RTC watchdog寄存器序列，其他串口仍走RTS。watchdog触发后USB可能先断开再返回响应，界面提示观察启动；触发前失败则明确区分“固件已写入校验通过”和“自动复位未完成”，不把复位失败误报成固件写入失败。寄存器仅用于ESP系统复位，不写Flash/eFuse/PMU。

这些分支已有模拟传输回归，网页实际复位、Windows重新枚举和固件启动仍由用户验证。新无线固件传书时暂停USB数据；请先退出传书，等USB恢复，再连接安装器。

1. 读取清单，下载所选版本的文件，按清单 SHA-256 校验。
2. 连接后确认芯片为 ESP32-S3、闪存 16 MiB；读取 0x8000 分区表，以及 data、wallpaper 分区开头 8 KiB。无法识别头部时再读取完整分区，确认是否全为 0xFF。芯片检测不能证明设备型号，用户必须勾选确认是 RDP-G01-W。
3. 生成计划并展示每段地址、大小与原因。首次安装默认勾选整片备份（按 1 MiB 分块读出 16 MiB，保存为下载文件）。
4. 一次 `writeFlash`（flash 参数全部 keep，不改写引导程序头），每段写后由设备端 MD5 校验；最后硬复位。

## 写入规则

偏移全部取自本固件分区表，只有引导程序 0x0 与分区表 0x8000 固定。

- **升级**（设备分区表与本固件逐字节相同）：写应用（ota_0）与 otadata；引导程序先比对设备 MD5，相同则跳过；不写分区表，NVS 全部保留。
- **首次安装**（其他布局）：写应用、otadata、引导程序，分区表最后写。新 NVS 范围（0x9000–0xF000）中，旧分区表里本就是 NVS 的扇区保留，其余扇区写 0xFF，避免旧系统的非 NVS 字节被当成 NVS 页导致 `nvs_flash_init` 失败；读不出旧分区表时整段清空。
- data、wallpaper 分区有 LittleFS 标记时保留（这只识别格式，不证明文件系统完好）。完整分区全为 0xFF 时写入首装空镜像；短读、未知格式或损坏头部默认保留，并提示可能无法保存。不能仅凭头部空白认定整个分区为空。
- “初始化无法识别的内部数据与壁纸分区”默认不勾选；明确选择后才覆盖无法识别的分区，不覆盖已识别的 LittleFS。此操作会丢失原有阅读记录、设置或壁纸，建议先整片备份。安装器尚不提供备份恢复按钮。
- “清除设置区”选项把整段 NVS 写 0xFF，仅用于安装后串口报 `NVS unavailable`、固件停在启动阶段的情况。
- 不整片擦除，不写 assets、phy_init 与 ota_1，不碰存储卡，不写 SY7636A 或 PMU；VCOM 仍只由固件从 PMU 读取。

断电或中断后可能无法正常启动；不能保证设备自动进入下载模式。按板级方式手动进入下载模式后，可重新安装或用外部工具写回整片备份。串口安装不具备设备OTA的原子切槽与自动回滚保证。已知风险：原厂 NVS 与本固件 IDF 版本的格式兼容性未验证，若不兼容需按上面选项清除。

## 本地运行

```powershell
py tools/dev.py installer
py tools/installer_site.py --serve 8765
```

前者跑计划测试并生成 `build-dev/installer-site`；后者在 `http://localhost:8765/` 提供页面（Web Serial 要求 https 或 localhost）。发布新版本：把新的 `releases/pico_nano-<版本>-esp32s3/` 提交到 `main`，Pages 工作流自动重建；仓库需在 Settings → Pages 选择 GitHub Actions 作为来源。
