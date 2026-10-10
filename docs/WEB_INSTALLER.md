# 网页安装器

在电脑的 Chrome 或 Edge 里通过 Web Serial 安装或升级固件，用户不必安装 esptool、不必记偏移。源码在 `installer/`，静态站点由 `tools/installer_site.py` 从 `releases/` 生成，GitHub Pages 工作流 `.github/workflows/installer-pages.yml` 发布。**安装器与它写入的固件都未经真机验证。**

## 组成

| 部分 | 作用 |
| --- | --- |
| `installer/plan.js` | 纯逻辑：解析分区表、识别 LittleFS、生成写入计划、MD5；不碰串口 |
| `installer/app.js` | 串口流程与界面：连接、读设备、备份、写入、校验、重启 |
| `installer/vendor/esptool-js-0.5.7/` | 固定版本的 esptool-js 打包文件（Apache-2.0），摘要见 `LICENSES/esptool-js-manifest.json` |
| `tools/installer_site.py` | 校验 `releases/pico_nano-<版本>-esp32s3/` 的 `SHA256SUMS` 与 `flash_args`，复制固件并写 `firmware/manifest.json` |
| `tests/installer/plan.test.mjs` | 写入计划与 MD5 的主机测试 |

## 流程

1. 读取清单，下载所选版本的文件，按清单 SHA-256 校验。
2. 连接后确认芯片为 ESP32-S3、闪存 16 MiB；读取 0x8000 分区表，以及 data、wallpaper 分区开头 8 KiB。芯片检测不能证明设备型号，用户必须勾选确认是 RDP-G01-W。
3. 生成计划并展示每段地址、大小与原因。首次安装默认勾选整片备份（按 1 MiB 分块读出 16 MiB，保存为下载文件）。
4. 一次 `writeFlash`（flash 参数全部 keep，不改写引导程序头），每段写后由设备端 MD5 校验；最后硬复位。

## 写入规则

偏移全部取自本固件分区表，只有引导程序 0x0 与分区表 0x8000 固定。

- **升级**（设备分区表与本固件逐字节相同）：写应用（ota_0）与 otadata；引导程序先比对设备 MD5，相同则跳过；不写分区表，NVS 全部保留。
- **首次安装**（其他布局）：写应用、otadata、引导程序，分区表最后写。新 NVS 范围（0x9000–0xF000）中，旧分区表里本就是 NVS 的扇区保留，其余扇区写 0xFF，避免旧系统的非 NVS 字节被当成 NVS 页导致 `nvs_flash_init` 失败；读不出旧分区表时整段清空。
- data、wallpaper 分区开头不是 LittleFS 超级块时写入首装空镜像；已是 LittleFS 则不动，阅读进度、书签与壁纸保留。
- “清除设置区”选项把整段 NVS 写 0xFF，仅用于安装后串口报 `NVS unavailable`、固件停在启动阶段的情况。
- 不整片擦除，不写 assets、phy_init 与 ota_1，不碰存储卡，不写 SY7636A 或 PMU；VCOM 仍只由固件从 PMU 读取。

断电或中断时设备停在下载模式，可重新安装或写回整片备份。已知风险：原厂 NVS 与本固件 IDF 版本的格式兼容性未验证，若不兼容需按上面选项清除。

## 本地运行

```powershell
py tools/dev.py installer
py tools/installer_site.py --serve 8765
```

前者跑计划测试并生成 `build-dev/installer-site`；后者在 `http://localhost:8765/` 提供页面（Web Serial 要求 https 或 localhost）。发布新版本：把新的 `releases/pico_nano-<版本>-esp32s3/` 提交到 `codex/reader-foundation`，Pages 工作流自动重建；仓库需在 Settings → Pages 选择 GitHub Actions 作为来源。
