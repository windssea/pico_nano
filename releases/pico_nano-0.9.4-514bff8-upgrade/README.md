# PicoNano 安装、异机升级与发布

更新：2026-10-16。默认通过电脑 Chrome/Edge 的 Web Serial 升级，在线入口为[网页安装器](https://windssea.github.io/pico_nano/)。0.9.3 OTA迁移测试预览为 `0.9.3+c871ac5`（接替 `0.9.2+2999e6d`），稳定分发版本仍为 `0.8.0+cfb5c2f`。0.9首次OTA准备需备份后使用同源码完整刷入，普通保留升级不会替换旧bootloader。网页安装与设备无线OTA是不同能力，当前没有已交付的OTA原子回滚。

0.9.3固件源`c871ac52b8f0bc64df986c26c97a7928935d6617`修正翻页“描边感”（同起点GL16派生时序），新增水波纹翻页、排版字重、CFF轮廓OTF、字体管理中的系统字体，书库封面首帧一次成画，LocalSend另开53317端口并改善局域网传书稳定性。227项主机C、43项SDL、25项Node/9项Python安装器及离线站点下载核对通过；两套ESP-IDF6.1完整构建/size与S3镜像校验通过。板级应用5402864B，6MiB槽余888592B（14.12%），SHA-256为`4aee0e8dd8eda1639b62de04ec1be59d914b8bf383f40521fee84aa84313bee9`；板级引导仍与0.9.0逐字节相同，未通过硬件资格。只发布三个USB安装ZIP与摘要，不发布OTA清单；未连接设备或刷写，水波纹/翻页观感、传书与LocalSend待真机验证。

0.9.2固件源`2999e6dfbc5a35db164b5cafc6f29a07608a8cdc`在设置“WiFi 网络”中加入屏幕键盘直接配置家庭WiFi，保存前先测试连接（取得IP才保存，失败保留旧记录，可显式不验证保存）。224项主机C、43项SDL、33项Python工具、25项Node/9项Python安装器及离线站点下载核对通过；两套ESP-IDF6.1完整构建/size与S3镜像校验通过。板级应用5321920B，6MiB槽余969536B（15.41%），SHA-256为`c9b49db52ed1ffc6e6ff4039f6eb11481eb20ae1d4da43c36233d7d77a004e4c`；板级引导仍与0.9.0逐字节相同，未通过硬件资格。同样只发布三个USB安装ZIP与摘要，不发布OTA清单；未连接设备或刷写，真机WiFi连接测试待验。

0.9.1固件源`502aab60b9058ed91cddd40d191a3280fba0dc19`修正OTA评审问题：首启确认只看固件自身（无TF卡、坏书或提示页不再误回滚），等待确认时允许自动锁屏，下载10秒等待/连续超时容忍及Range续传，用过OTA后的USB保留升级写完ota_0再清空otadata。222项主机C、33项Python工具、25项Node/9项Python安装器及离线站点下载核对通过；两套ESP-IDF6.1完整构建/size与S3镜像校验通过。板级应用5311856B，6MiB槽余979600B（15.57%），SHA-256为`e3045ac96fb717d5ad275dc4e5b67284884ce334bca84c2e405aee8ab9504a4a`；板级引导与0.9.0逐字节相同（21232B，`80193053e7c398d196e87db7b63a1eaf008ea5f368d35f918c094d5b804652e1`），仍未通过USB迁移/断电回滚资格。本批同样只发布三个USB安装ZIP与摘要，不发布设备可消费的OTA清单；未连接设备或刷写。

0.9.0固件源`8b5b6c069e42bc91f189c4731952a92b28c8efb6`已通过隔离的CI/板级ESP-IDF6.1完整构建、size与S3镜像校验。板级应用5310336B，6MiB槽余981120B（15.59%），SHA-256为`ab5d72a00fb4e4196b890fee0e7a460c1dfe56f6e4776a267fce750d9d386484`；两配置均开启回滚/OTA并使用同一正式公钥，诊断控制台关闭。13项相关主机复验、30项工具、25项Node/9项Python安装器及离线站点实际下载核对通过。测试引导21232B、SHA-256为`80193053e7c398d196e87db7b63a1eaf008ea5f368d35f918c094d5b804652e1`，尚未通过USB迁移/断电回滚资格；本批仅提供三个USB安装ZIP与摘要，不将本地候选签名清单发布到稳定OTA源。首次完整迁移与之后的真实OTA验收分别记录，设备由用户自行刷写。

0.8.0聚合首轮串口诊断镜像真机测试后的修复：锁屏卡死与遮罩缓存、阶段日志PSRAM堆遍历欠载、下一页预渲染缓存额度、阅读退出延后落盘、已验证身份备忘、TXT探测快速路径、EPUB加载页内推进开书、圆角描边与书架封面绑定/缩放、字体管理恢复系统默认与界面修正。219项主机、43项SDL及EPUB/TXT锁屏验证通过；真机数据来自诊断镜像，非产品镜像验收。原波形、完整身份与6MiB池保持；新增6MiB池2MiB缓存总额度。本次按用户明确授权构建发布，设备由用户自行刷写。

0.8.0精确固件源`cfb5c2fb9189ee76591f79b94b865a985095e547`；两套ESP-IDF6.1完整构建/size与板级image-info通过，两套配置均确认诊断控制台、禁自动锁屏与PROFILE关闭，镜像不含控制台字符串。板级应用5123488B，6MiB槽余1167968B（18.57%），SHA-256为`be40592abb0fc6cab691482781af0ad2360878b7ea476bf3dad476fb38cba899`。24项Node/9项Python安装器检查、升级/完整包同应用及离线站点逐文件核验通过；未知/短表仍拒绝。发布时未刷写设备，清单保持未通过硬件验收。

0.6.16将书库首显的封面读取移到逐槽空闲任务，索引缓存写入保留系统字体及热会话，减少切页重载；修正锁屏暂停代次确认和新电源键交接，排版双列单页，增加阅读时间电量、封面格式标签并加深木质层板。固件源`c36c30227b938416e4096829fad1f691a0893862`，210主机/40SDL、10Python/5Node工具、24Node/9Python安装器、用户书籍/字体矩阵及两套SDK构建/size与镜像核验通过；应用5101344B，槽余1190112B，SHA-256为`64243f3fe6eca19b3aa6ebc859a72b86165ed12eed3534f14472fadf7d024724`。升级/完整包共用同一应用，离线包仅含本版本两模式；0.6.8～0.6.15已知完整表及legacy的网页升级计划均仅写0x20000应用，未知/短表拒绝。软件回归含832次正文、192次主页锁解锁与104组内存（101进入阅读、3明确低预算OOM，默认6MiB全部打开）；未接串口或刷写，实机首次切页、短按解锁成功率、TF/面板/功耗仍由用户验证。

0.6.15复用已验证的RAM封面，返回首页/书库时缺失才读PNCV，离页暂停的缓存写入可在返回后恢复；独页且匹配出版物声明的封面按原图比例适配正文区并水平、垂直居中，正文夹图和页脚保持。固件源`e09fc0d4937af31f0d0483cc5b97c03b60d4dd8d`，206主机/40SDL、10Python/5Node工具、24Node/9Python安装器及两套SDK构建/size通过；应用5099504B，槽余1191952B，SHA-256为`2a6067f2bf241f3aa235c9569634bc81fa7dcc3ee510ad22be61fa76b929a4eb`，升级/完整包与当前版本离线站点逐文件核验；原波形、强身份校验与6MiB应用池保持，实际菜单响应、触控及面板观感仍由用户刷机验证，开发过程不连接串口或自动刷写。

0.6.14修正书库横滑：UP报告按活动触点归一，快速滑动补最后坐标，横向主导手势允许适度斜移；四列后两本不被旧阅读快捷码拦截。设备输入线程与真实产品路由软件检查通过，固件源`4fdfcb6f647fe94a0dc315c80e6451bb5fd67819`，204主机全量/40SDL及最终20项相关复验、10Python/5Node工具、24Node/9Python安装器及两套SDK构建/size通过；应用5095600B，槽余1195856B，SHA-256为`ea6a06e32ade59fc78ef223044ad70f7fea133164bcbe90892cc3ce788c5b2b2`，升级/完整包与当前版本离线站点逐文件核验；实际触控成功率、面板响应和0.6.13唤醒修正仍需刷机验证。离线网页包仅含本版两种刷入模式，在线保留历史版本。

0.6.13修正阅读锁屏后电源短按可能无响应：入睡前读取PMU按键，扩展器中断加500ms定时复核；丢弃锁页扫描期间的旧输入，显示恢复失败保留原会话重试。七项睡眠竞态/故障软件检查及用户13本书×四字体的连续锁解锁矩阵通过，固件源`3c851394bd5e94fe6477d2f67f762ea20897e799`，204主机/40SDL、10Python/5Node工具、24Node/9Python安装器及两套SDK构建/size通过；应用5094576B，槽余1196880B，SHA-256为`f0aa4922dfe9bd66543f8c2ade9e74ff9e5026b36cf89cc6ceb04aa303b4adec`，升级/完整包与当前版本离线站点逐文件核验；设备短按唤醒、长期待机和定时复核的功耗仍待刷机验证。离线网页包仅含本版本升级/完整刷入，在线站点保留历史版本。

上一版0.6.10包含菜单跨度绘制、跨菜单界面字体复用、八像素并行内容风险统计及网页分区表兼容修正；保留原波形、刷新阈值与6MiB池。固件精确源码`5ba9eb4a482fec58e1886d80d3ad57943a8e9bfc`；195项主机、39项SDL、10项Python/5项Node工具、24项Node/9项Python安装器、13本用户书籍操作旅程及四款用户字体复用检查通过。两套ESP-IDF6.1构建/size和板级image-info通过；板级应用5090704B，6MiB槽余1200752B，SHA-256为`b89b4a483e8f1be4ca58e2bfbf070666eea1bbc9d03a73df8d70d011fff062cd`。升级/完整包共用同一应用，离线站点逐文件校验通过；未刷写设备，真实切页速度、面板欠载及USB仍待用户刷机验证。底栏按下即切页仅完成源码对照，未包含在本版。

0.6.11增加固定底栏首次单指按下切页及跨页接触所有权，保留原刷新和内容控件确认规则；精确固件源`fc455c871842f30608ad070c675f70cde2034881`；195项主机、40项SDL、10Python/5Node工具、24Node/9Python安装器、13本用户书操作旅程和四款字体复用检查通过。两套SDK构建/size及板级image-info通过，应用5091536B，6MiB槽余1199920B，SHA-256为`96c1292020bb932fce596c4ad4ec78748efc5ca42ba19b4ef3d364ef2d028eb4`。升级/完整包与离线站点逐字节核验，未刷写设备。用户已长期授权较大阶段通过验证后构建推送，设备由用户自行刷写。

0.6.12包含TXT一遍扫描/空行段距/最近阅读修复、三行滑动书架及传输启动资源与发现故障隔离。精确固件源`fee6f1a85e8978b487804bc65908689b4677e299`；198主机、40SDL、10Python/5Node工具、24Node/9Python安装器通过。用户13本阅读旅程/跨进程缓存/字体继承覆盖、三本TXT完整SHA与逐字符检查点、四字体管理复用、四组整库冷暖缓存/滑动与104组内存检查通过；默认6MiB全部开书，三组低预算明确OOM，所有退出池归零且原件SHA不变。两套SDK构建/size与image-info通过，板级应用5094032B，槽余1197424B，SHA-256为`124d481e0b933dd09e677b6004dc6273f559d910803cee77f6f37acacead7273`。升级/完整包和离线站点已逐字节核验；用户自行刷机验证触控、无线及真实切换速度，开发过程不接串口、不自动刷写。

## 另一台电脑网页升级

1. 退出阅读器USB传书、无线传书/对时，等待USB串口/JTAG数据接口恢复；用数据线接到另一台电脑，关闭占用串口的程序。
2. 在电脑Chrome/Edge打开上述HTTPS站点，选择带源码后缀的最新专用升级版本，核对型号RDP-G01-W、ESP32-S3、16MiB Flash。手机浏览器是否有串口能力不能假定，不把手机上传页当固件安装页。
3. 默认选择“保留布局升级”，连接真实端口；安装器只读核对已知旧设备分区表，计划应仅包含应用 `0x20000` 一项。分区不匹配或短读必须停止；刷过其他固件请按下节使用同源码完整包，不能改用旧首次安装包覆盖。
4. 开始写入，确认设备端MD5校验，再观察复位/USB重新枚举/正常开机。写入校验成功、自动复位触发和产品正常运行分别记录。
5. 开书/翻页/锁解锁/传书/保存重开验证，尤其记录插线与拔线的网页响应及上传失败原因；没有验收的数据不要称稳定修复。

也可用发布目录中 `PicoNano-web-installer-<提交>.zip`：解压后运行 `py serve.py`，保持终端，在Chrome/Edge打开 `http://localhost:8765/`。这只需Python提供本地站点，不需ESP-IDF/Docker/esptool；不能直接双击HTML使用Web Serial。

## 保留布局升级的写入契约

0.9开发树额外读取完整8KiB OTA选择区：全FF时只写`ota_0`；设备用过OTA时先写`ota_0`，再清空OTA选择区，使其从新写入的固件启动，不必完整刷入；短读停止。使用过设备OTA后，不可再使用旧离线包/CLI的固定`0x20000`升级方式。现有发布包保持原始字节，该保护尚未随新的安装站点发布。

当前专用升级以[已知旧设备表](https://github.com/windssea/pico_nano/blob/514bff88941136c14f3ce2ca1242ef48d3e11e1c/releases/pico_nano-0.0.57-6840523-upgrade/expected-partition-table.bin)为基准：从 `0x8000` 只读3072字节并严格比较，匹配后只写 `0x20000` 应用，不写bootloader、分区表、otadata、NVS、内部数据/壁纸或TF。清除/初始化选项禁用。适用前提是该已知布局、当前ota_0路径，不能推广到其他板型/布局/ota_1运行设备。

0.6.10网页安装器兼容修正：站点生成器还会从同版本、完整源码提交相同且应用长度/SHA-256一致的已校验完整包，加入其分区表作为第二项精确白名单。设备只要逐字节匹配其中一张完整3072B表，仍仅写应用；短读、未知或任意一字节变化仍停止。这解决本项目完整刷入0.6.8后，升级0.6.9被旧表白名单误拦截的问题。已发布固件及ZIP不修改，历史离线安装器和命令行升级脚本仍只接受其包内固定旧表；不能据网页修正宣称旧包也已兼容。

仓库新分区与旧设备NVS大小不同，不能用 `build-board/flash_args` 的完整写入计划替代升级，不执行erase-flash。串口写应用中断可能无法启动，需要按板级已验证方式重新进入下载模式并重写；它没有设备OTA切槽/回滚保证。已有16MiB备份单独保管，不放进公开升级包。

## 换过其他固件后的完整刷入

安装器提供独立“完整刷入”模式与同源码完整包生成入口。可用版本以网页清单为准；没有完整包时界面明确停止，不借用历史首装包。

完整包发布后，在“刷入方式”选择“完整刷入”，核对版本/源码后缀，再连接设备或沿用已通过芯片/容量检查的连接。分区不匹配不会自动切换模式，历史0.0.57首装包不进入当前完整刷入列表。

完整计划包括应用、OTA选择、引导程序及必要的分区表（最后写）；同布局时仅跳过相同引导程序与无需改写的表。默认勾选16MiB备份，短读导致备份失败即停止写入。NVS默认保留，新范围含原来的非NVS或未知数据时，必须明确勾选“清除设置区”才能开始，所选新NVS范围全部清除。内部数据与壁纸只有完整分区全FF才自动写空镜像；旧布局的同地址/尺寸/类型分区且有LittleFS头才作为可保留候选，头部识别不是健康证明。不兼容且非空时必须明确勾选初始化；该选项也会清除已识别的内部记录和壁纸。

核对逐项地址、长度和清除原因后，确认型号及完整刷入影响，再开始。所有镜像下载校验SHA-256、写入校验MD5，不整片擦除、不访问TF或PMU。写入中断仍有启动失败风险，不能将分区表最后写视为原子恢复；读写、复位与正常启动分别验收。完整包ZIP供站点携带完整镜像；现有`flash_upgrade.py`仍只适用于专用应用升级，不能用它执行完整刷入。

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

旧 esptool-js0.5.7 的 `after("hard_reset")` 只走RTS策略，缺少原生S3 USB退出强制下载模式的RTC/watchdog序列。自有 [installer/reset.js](https://github.com/windssea/pico_nano/blob/514bff88941136c14f3ce2ca1242ef48d3e11e1c/installer/reset.js) 先清RTC `FORCE_DOWNLOAD_BOOT`位；识别Espressif原生USB303A:1001后使用RTC watchdog复位，其他串口走RTS。不修改vendor、不写Flash/eFuse/PMU。

watchdog触发后USB可能在返回响应前断开，Windows端口号也可能变化；界面区分“写入校验成功”和“自动复位未完成”。这条原因来自代码路径，复位适配已有软件回归，真实网页复位/新电脑重新枚举/正常启动仍待用户复验。触发前失败明确提示，不能把超时直接当作已正常开机。

无端口先确认数据线、接口、电源与USB恢复；一直Connecting确认端口和板级下载模式。写入哈希已过但末尾串口报错时，先观察正常开机/重新枚举，必要时按板级方法退出下载模式。写入中断重新进下载模式保留日志再升级，不据复位错误执行擦除。

## 首次安装与旧包

旧完整首装包仍在历史发布目录，但不作为当前已安装设备的默认升级。它会写应用/otadata/bootloader和分区表，可能初始化新的NVS扇区/空内部镜像；用户明确选择清除或初始化会丢失对应记录。LittleFS头部识别不代表FS健康，头部全FF也不代表整分区为空。无独立型号/备份/布局确认不使用此路径。

旧完整方案的详细写入矩阵保留在[安装器归档](https://github.com/windssea/pico_nano/blob/514bff88941136c14f3ce2ca1242ef48d3e11e1c/docs/archive/2026-10-14/WEB_INSTALLER.md)，作为历史说明；当前新工作应先评估分区迁移及恢复，不直接覆盖旧设备。现有默认应用升级不执行上述首装路径。

## 开发者发布流程

### Gitee 国内分发

0.9源码增加可选的两个OTA附件：`PicoNano-<提交>-ota.bin`与固定名`PicoNano-ota-manifest.bin`。固件构建及打包获授权后，先使用已有的升级包和已资格USB迁移引导镜像生成签名附件，再将三个ZIP和这两个文件都加入同一GitHub Release。自动镜像会独立验签并核对应用/版本/源码与ZIP内应用，先完成Gitee应用上传和匿名校验，再上传签名清单；旧版本缺少OTA目录时仍只同步原来的三个ZIP和摘要。

```powershell
# 私钥路径仅指向本机受保护文件；公钥须与该板级构建配置相同。
# The private key stays in a protected local file; its public key must match the board configuration.
python tools/package_ota.py --release releases/pico_nano-0.9.0-<源码短提交>-upgrade --private-key <本机私钥.pem> --migration-bootloader <已资格引导镜像.bin>
```

该命令不覆盖已有`ota/`目录，也不将私钥或迁移引导原件复制进发布目录。`ota/release-public.pem`仅用于发布前独立验签，设备信任公钥来自固件配置；下载文件不能替换信任锚。0.9首次迁移与回滚尚待真机资格，不能对现有0.8设备宣称已支持。

国内入口为 [Gitee 发行版](https://gitee.com/windssea/pico_nano/releases)，该仓库用于安装包分发；Gitee 标签标记分发版本，固件精确源码提交以包内 `manifest.json` 的 `source_commit` 为准。推荐国内用户下载 `PicoNano-web-installer-<提交>.zip`，按前文运行本地安装站点，不需要克隆仓库；完整包、命令行升级包与离线网页包保持 GitHub 原始字节，另附 `SHA256SUMS`。

[gitee-release.yml](https://github.com/windssea/pico_nano/blob/514bff88941136c14f3ce2ca1242ef48d3e11e1c/.github/workflows/gitee-release.yml) 在 GitHub Release 的 `published` 事件后自动调用 [sync_gitee_release.py](https://github.com/windssea/pico_nano/blob/514bff88941136c14f3ce2ca1242ef48d3e11e1c/tools/sync_gitee_release.py)，也可从 Actions 的 `Mirror release packages to Gitee` 手动输入已发布标签补同步。它从主分支选出该版本唯一、同源的升级/完整包，核对包内文件和三个 ZIP 摘要，下载 GitHub 已发布附件逐字节复核，再上传到 Gitee；已有同名附件须匿名下载摘要相同，否则停止，不自动删除或覆盖。它不重建固件，不修改既有 ZIP，也不推送源码历史到 Gitee。

首次使用需要由账号所有者在 Gitee 创建仅限目标仓库、含 `projects` 权限的仓库级私人令牌，在 GitHub 仓库 `Settings → Secrets and variables → Actions` 保存为 `GITEE_TOKEN`；浏览器登录状态不能替代后台凭据。令牌只经环境变量读取，不放入代码、URL、命令行或校验报告。失效/权限不足时工作流失败，需要更新 secret 后补跑；附件配额由 Gitee 当前账号规则决定，不为释放配额自动删除旧版本。首次手动补同步可勾选 `initialize_guides`，会替换Gitee的中英文README为下载指南并读回确认；普通发布不修改README，已手工定制后不要再勾选。

附件上传使用curl流式multipart，需本机或runner具备curl；控制API仍使用Python标准库。连接等待20秒，4MB以内附件最多300秒、更大附件最多900秒，禁止跟随鉴权重定向和盲目重复POST；上传令牌仅经子进程stdin传入，临时文件只保存公开附件与响应。工作流最多运行三轮完整预检/同步，每轮先读取并核对已有附件，再上传缺失项，整体最长45分钟；超时结果不明确时不直接重放同一次POST。

```powershell
# 只检查本地包，无网络/上传；可在未配置令牌时复跑。
# Check local packages only; no network/upload or token required.
python tools/sync_gitee_release.py --tag v0.8.0
# 匿名核查已经公开的国内附件，不进行写入。
# Verify public domestic attachments anonymously without writing.
python tools/sync_gitee_release.py --tag v0.8.0 --verify-only --report build-dev/gitee-0.8.0/verified.json
# 仅在环境已注入 GITEE_TOKEN 且本批已获发布授权时上传。
# Upload only with an environment-provided GITEE_TOKEN and authorization for this batch.
python tools/sync_gitee_release.py --tag v0.8.0 --apply --report build-dev/gitee-0.8.0/synced.json
```

同步完成以全部附件的匿名下载 SHA-256 校验为准：旧版为三个ZIP与摘要共四个，含OTA的新版本另有原始应用和签名清单，共六个。日志或网页出现文件名不代表下载已经通过。自动流程最多等180秒供GitHub发布后完成附件上传，仍缺包则停止，可在附件齐全后补跑。当前已发布的Gitee分发供电脑USB串口安装；设备Wi-Fi OTA及其首次引导迁移列入0.9，详见开发指南。

### 固件构建与 GitHub 发布

按当前AGENTS.md，新批修改验证完成后先询问是否构建发布，不沿用前批授权。0.6.17的构建发布授权仅适用于当时批次；当前0.9已获得用户确认构建推送；真实OTA验收与正式发布分别记录。设备由用户自行刷写。源提交、固件构建、发布包和Pages部署均要可核对；不能把未构建的优化写进已发布manifest。

授权后先完成相关软件/可见窗口回归，提交需要发布的源码；复跑两套配置/size，确认board配置120MHz、USB/RF互斥及非诊断构建，slot余量目标至少10%。再打专用升级包：

```powershell
python tools/dev.py firmware-ci
python tools/dev.py firmware-board
python tools/package_upgrade.py --reference releases/pico_nano-0.0.57-6840523-upgrade/expected-partition-table.bin
python tools/dev.py factory-data
python tools/package_upgrade.py --full
python tools/dev.py installer
```

[package_upgrade.py](https://github.com/windssea/pico_nano/blob/514bff88941136c14f3ce2ca1242ef48d3e11e1c/tools/package_upgrade.py) 要求已登记源码干净，按 `pn_brand.h` 的 `PN_PRODUCT_VERSION` 生成 `releases/pico_nano-<版本>-<源码提交>-upgrade/` 的应用、已知分区样本、脚本、README、manifest/SHA及命令行ZIP。`tools/dev.py installer`测试并生成本地站点；[installer_site.py](https://github.com/windssea/pico_nano/blob/514bff88941136c14f3ce2ca1242ef48d3e11e1c/tools/installer_site.py) 支持 `--serve 8765`。现有离线网页ZIP是另行封装站点并附serve.py的发布产物，没有独立自动打包入口，后续需补齐可复跑工具；构建文件和参考表摘要必须匹配包清单。

同一板级构建再用`--full`生成`releases/pico_nano-<版本>-<源码提交>-full/`：包含该构建的bootloader、分区表、OTA初始数据、应用和`factory-data`生成的两个空文件系统镜像，不混用旧包引导/表。生成器核对flash_args固定地址、镜像尺寸与板级应用内版本，站点验证六个镜像清单摘要/身份；完整与专用升级包使用独立URL，网页按模式筛选，默认专用升级。当前软件回归为22项Node与5项完整包Python；完整迁移、备份读回及启动仍需设备验证。

提交并推送发布目录后，现有 [installer-pages.yml](https://github.com/windssea/pico_nano/blob/514bff88941136c14f3ce2ca1242ef48d3e11e1c/.github/workflows/installer-pages.yml) 通过用户已启用的GitHub Pages重建。核对工作流成功、在线默认源码后缀、应用/分区样本摘要和复位脚本，再给用户刷入。源码提交和发布提交区分，文件名版本相同不表示二进制相同。离线网页ZIP由生成的站点加原 `serve.py` 打包，并在 `PACKAGE_SHA256.txt` 记录两个ZIP摘要。

## USB直接复制（0.6.6开发预览）

设备“传书 → USB传输”手动交出TF，在电脑磁盘的`piconano/books`、`piconano/fonts`、`piconano/covers`、`piconano/wallpapers`分别复制图书、字体、封面和锁屏原图。复制完成先在电脑安全弹出，再点设备“结束USB传输”；恢复失败可重试，记录不会因此清除。MSC期间网页刷机所需的Serial/JTAG暂停，结束后恢复；软件验证与固件构建不代表本机USB已验收，请按开发指南记录枚举、读回SHA和串口恢复。

## 版本与交付证据

0.6.8阶段发布源码`c4a44e5`，优化实现提交`0787dc8`：190项主机、39项SDL、10项工具Python/5项网页及用户书库兼容/封面/缓存/内存矩阵通过；升版后补验11项相关主机、2项SDL，22项安装器Node/5项包Python、两套ESP-IDF6.1构建及size通过。板级应用5087568B，6MiB槽余19.14%，SHA-256 `964a941e1c143206932ba8e28e60c52b7eede48381bff41b6ed5efc5c347b4cb`；S3 image-info版本/checksum/validation通过。升级/完整包使用同一应用，离线网页ZIP与生成站点逐文件一致。原封面清晰度/PNCVv6/波形保留，连续滚动和按下反馈资格开关默认关闭。用户自行刷机，优先复验管理响应、首次/再次封面、遗物整理师开书、字体继承和保存重开；本批未连接或刷写设备，清单保持`verifiedOnHardware=false`，TF/面板/USB/功耗仍待实测。

完整刷入批次`4e02139`：基于最新已发布0.6.6阅读修复源码，未混入原工作区0.6.7开发改动。22项安装器Node、5项完整包Python及两套ESP-IDF6.1构建/size通过；板级应用5026336B，6MiB槽余20.11%，SHA-256 `9cc799bd1103d8b3608d94f21f1835bf6de6e4d91106d0659388f1a138a90d8a`。应用/完整包共用同一应用，完整包另含该板级构建的引导/表/OTA与两个空FS，站点及离线ZIP逐文件摘要一致；S3 image-info版本/checksum/validation通过。当前完整刷入仅有软件、构建和分发核验，备份可恢复性、实际迁移/复位/正常启动需用户刷入验收，清单继续`verifiedOnHardware=false`。

0.6.6修复源码`eff83f1`：板级应用5026336字节，SHA-256 `bdfeeb616884b9009f437ab46dfd5d10f0cfadeeb2c1af1f8873b17d24651a8f`。ZIP/JPEG、字体身份、图片缩进与按键回归通过；149CTest/32SDL/10Python/5网页/17安装器基础门通过，最终相关29项设备/核心、3项SDL及完整字体共享源回归补验通过，两套SDK构建/size与包摘要通过。完整系统字体位于TF固定路径，内置应急字库保留；用户本机安装和读回SHA一致，软件绘制通过不代表新固件真机阅读/功耗/光学验收。未在本轮串口烧写，继续`verifiedOnHardware=false`。

0.6.6源码`3c8eaf9`：板级应用5025184字节，6MiB槽约20.1%余量，SHA-256 `6cd8cbda938b04c848f5c2cb9f59d642b7d18e62c2bda40c8fb16e38123229b8`。149项CTest、10项Python、5项网页、32项SDL与17项安装器测试通过；最后回调链接与目录呈现修正补跑相关回归，两套固件构建/镜像/包摘要校验通过。USB和光学效果未做设备验收，清单继续`verifiedOnHardware=false`。


已发布 `0.6.5+766a137` 板级应用4973456字节，6MiB槽约21%余量，SHA-256 `099519ddf664887aefbec2e070de54a796687e4c0030ebb0d0016ad3a939f59a`；该值只适用于该发布，后续必须从新manifest读取。0.6.5 在面板扫描期间准备下一页、页脚变化合并为单次扫描并加输入健康日志。0.6.4（4972336字节）把字形缓存扩到约两页（1024槽/704KiB）并让封面预算计入可回收缓存。0.6.3（4972224字节）修复调频/浅睡在0.6.1–0.6.2未生效（漏含sdkconfig.h）、加睡眠引脚上下拉与预热加速。0.6.2（4968960字节）在 `0.6.1+7c51387`（4964336字节）基础上开启自动浅睡与触摸中断唤醒，插电脑USB时保持唤醒。0.6.1 在 `0.6+a0a9a97`（4957088字节，长章语义窗口、紧凑语义模型、字形预热、四路字形缓存、整书SHA DMA块与版本号）基础上增加动态调频、去除已显示帧副本、遮罩锁屏平面缓存、ZIP本地头随SHA顺序校验、阅读线深浅与左右半屏点按。插线稳定性、纸屏效果及时间/复位仍需设备验证。

本次文档归档不重新生成既有发布包或installer站点，旧包SHA/脚本/README保持原样。后续包的README使用本页，仓库相对链接自动转换为该包源码提交的GitHub链接，离线解压后仍可查阅引用，不继续指向已归档的独立异机指南。
