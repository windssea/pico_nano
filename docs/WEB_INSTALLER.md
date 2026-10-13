# 网页安装器

在电脑的 Chrome 或 Edge 里通过 Web Serial 安装或升级固件，用户不必安装 esptool、不必记偏移。源码在 `installer/`，静态站点由 `tools/installer_site.py` 从 `releases/` 生成，GitHub Pages 工作流 `.github/workflows/installer-pages.yml` 发布。**安装器与它写入的固件都未经真机验证。**

## 组成

2026-10-13新增 `0.0.57+c696890` 保留布局升级，固件来源为提交 c696890：只下载应用和已核对旧分区表样本，严格匹配设备布局后只写 `0x20000` 应用；不读取内部文件系统，不写 otadata/bootloader/分区表/NVS/数据。清除和初始化选项禁用。仅适用于当前已从 ota_0 启动的阅读器，不用于首次安装。原有完整安装包与规则仍单独保留。具体操作见 [异机烧录](FLASH_ON_ANOTHER_PC.md)。

GitHub Pages此前部署返回404，部署日志提示确认是否启用Pages；在线地址尚不可用。管理员须在仓库Settings → Pages选择GitHub Actions来源，再重跑工作流。可先使用发布目录里的本地网页ZIP（解压后执行`py serve.py`，打开`http://localhost:8765/`）。本轮网页计划/控制器软件测试通过，但浏览器串口连接、实际写入和复位启动仍待用户实测；当前esptool-js复位请求不能保证原生USB自动退出下载模式。

| 部分 | 作用 |
| --- | --- |
| `installer/plan.js` | 纯逻辑：解析分区表、识别 LittleFS、生成写入计划、MD5；不碰串口 |
| `installer/app.js` | 串口流程与界面：连接、读设备、备份、写入、校验、重启 |
| `installer/vendor/esptool-js-0.5.7/` | 固定版本的 esptool-js 打包文件（Apache-2.0），摘要见 `LICENSES/esptool-js-manifest.json` |
| `tools/installer_site.py` | 校验完整发布的 SHA256SUMS/flash_args，或专用升级发布的 manifest 摘要，复制固件并写站点清单 |
| `tests/installer/plan.test.mjs` | 写入计划与 MD5 的主机测试 |

## 流程

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
