# 快速传书与导入方案

## 1. 三条通道

| 通道 | 用户路径 | 最适合 | 约束 |
| --- | --- | --- | --- |
| AP 手机网页 | 开热点→扫码加入→扫码打开网页→选文件 | 无路由器/第一次传书 | 通常需两个二维码步骤；手机可能提示无互联网 |
| STA 局域网网页 | 设备加入 WiFi→网页/二维码→拖放批量 | 日常、多本、字体 | 2.4 GHz；显示 IP，mDNS 只是便利入口 |
| USB MSC | USB 页面→电脑看到可移动介质→拷贝→安全退出 | 很大 PDF/漫画、多文件 | 硬件与 USB 栈先验证，设备暂停所有 TF 消费 |

默认 AP 一触开始；保存过的 STA 供一触连接，连接失败保留 AP 手动入口，不偷偷切模式。传书页面离开执行停止屏障，回进入前的位置。后台常驻无线/传书列 P2；初版不边读 PDF 边写 TF。

备选串口二进制传输需电脑工具，保留工程调试用途，不作为普通用户默认通道。USB Serial/JTAG 不等于现成 USB 磁盘；TinyUSB OTG 和当前调试口/PHY 切换、GPIO19/20 走线、枚举和自动刷机恢复均要真机验证。

Espressif v6.1 MSC 示例明确区分应用与主机访问存储，禁止同时访问同一个分区。本设计采用文件系统独占，不只是加一个 fopen 互斥。[官方 v6.1 MSC 示例](https://github.com/espressif/esp-idf/blob/v6.1/examples/peripherals/usb/device/tusb_msc/README.md)

## 2. 为什么推荐网页分块续传

直接单个 PUT 流实现容易，但网络断开或手机熄屏后需重传整书；HTTP 多连接并发容易挤占显示与内存；设备端转换所有格式不可控。因此推荐：单个活动文件、队列顺序发送、固定 64 KiB 块、确认落盘再前进、持久会话、导入异步。上传文件清单与完成后索引不需要阻塞数据通道。

手机浏览器熄屏可能暂停网络；页面显示“回到页面后继续”，不承诺后台上传持续。页面刷新/重开后重新选择原文件，验证文件 size＋SHA-256/块摘要，再找设备会话续传；网页不能假定永远能保存原文件句柄。

## 3. 配对与生命周期

AP SSID `ReadPico-XXXX`，随机 WPA2 密码，用户可查看。设备传书会话生成 128-bit 随机 token，显示网页二维码 URL fragment（例如 `/#pair=...`），脚本读取后清除地址 fragment，再在请求头 `Authorization: Bearer ...` 使用。token 不放查询串，不记日志，不进入永久设置；离开传书立即失效。

手输 IP 用户使用一次 6 位配对码，5 分钟有效，5 次失败锁 60 秒；配对得到完整 token。修改、删除、覆盖、配网、升级均需配对授权。只读容量、基础状态可开放，书籍名称和目录默认也需授权。无通配 CORS；校验 Origin/Host；不在网页里用 innerHTML 插入文件名。WiFi HTTP 不提供机密性，界面说明适合可信局域网；配对授权解决操作访问，不能替代 TLS。

网络状态：STOPPED → STARTING → READY → STOPPING → STOPPED；STA 加 CONNECTING/FAILED。上传状态与网络状态分开，不因断 WiFi 把已落盘会话当失败文件删除。

## 4. API 契约草案

API 统一 `/api/v1`，错误 JSON 包括 `code,message,retryable,request_id`；数值大小用整数 bytes，文件名 UTF-8，上传会话 id 设备随机生成，不由文件名拼路径。

| 方法与路径 | 输入/结果 | 规则 |
| --- | --- | --- |
| GET `/status` | 通道、空间、限额、任务状态 | 不返回密码/token |
| POST `/pair` | code → token | 限速，禁止日志记录凭据 |
| POST `/uploads` | kind/book-font-cover-wallpaper，name,size,sha256,overwrite_id | 返回 upload_id,chunk_size=65536,next_offset |
| GET `/uploads/{id}` | 已持久 offset、状态、可重试原因 | 只对当前授权客户端开放 |
| PUT `/uploads/{id}/chunks` | offset,length,chunk_sha256＋raw bytes | offset 必须等于 next_offset；同块重发幂等 |
| POST `/uploads/{id}/complete` | 声明结束 | 核验总长度/完整摘要→校验格式→提交→入库队列 |
| DELETE `/uploads/{id}` | 取消 | 删除本会话临时数据，不删旧书 |
| GET `/books?cursor=...` | 元数据与缩略图链接 | 页最大 64 条，查询/排序有界 |
| DELETE `/books/{id}` | 指定 content_id＋path_id | 确认页面动作，只删对应实例 |
| PUT `/books/{id}/metadata` | 自定义标题/作者/封面引用 | 侧车更新，不修改 EPUB/PDF 原文件 |
| GET `/fonts`、DELETE `/fonts/{id}` | 字体目录/校验结果、删除指定资源 | 使用中返回409，设备先选择替代；系统UI不可删除 |
| GET `/wallpapers`、DELETE `/wallpapers/{id}` | 原图目录、激活缓存状态、删除原图 | 已应用内部缓存保留状态明确返回 |
| POST `/network` | SSID/password | 传书停止后再切网；凭据单 blob |
| POST `/update` | 升级会话创建 | 与上传、USB、阅读互斥；分块写非活动槽 |

协议返回状态：201 会话创建，200 幂等成功，202 校验/入库中，409 位置/版本冲突，413 限额，422 摘要或格式错误，507 空间不足，503 介质不可用。错误中不得返回内部任意路径。

上传初次创建可只声明未知全文 SHA，先按块摘要推进；最终完整摘要由客户端与设备双方核验。默认网页用自托管、经测试的增量 SHA-256 worker（非安全上下文的 HTTP 不依赖 WebCrypto 可用性）；每次读最多 256 KiB。已转换的电脑工具可提前给全文 SHA；设备无须把全文件读到内存。

## 5. 块与提交事务

会话 manifest 内容：版本、upload_id、目标 kind、用户名字、期望总长度、完整/未知摘要、媒体 id/epoch、下一已确认 offset、上一块摘要、创建时间/持久序号、提交 phase、原文件身份、CRC。A/B 两份，选择最高有效序号。每块同时核验 content length 与 offset，不允许稀疏写入任意位置。

块处理：获得写 lease → 校验限额/可用空间 → 接收至 ≤64 KiB 缓冲并算摘要 → 顺序写 `.part` → 通过实现支持的同步机制落盘 → 写新的 A/B manifest并同步 → 返回 next_offset。同步原语在 v6.1 FatFs/wear-leveling 实测；不支持 fsync 时使用确认过的 close/reopen 路径。HTTP 200 表示同步约定下持久块，不仅 fwrite 成功。

重发上一块：offset 小于 next_offset，长度和摘要与日志一致则返回当前 offset；不一致 409。超过 next_offset 禁止写。掉电恢复对 `.part` 长度与 manifest核对，截去未确认尾块，或读尾块确认后推进；不得相信单个损坏 manifest。会话跨开机保留，需要新配对认证；新的媒体 epoch 不复用旧句柄。

完整提交阶段：VERIFYING → VALIDATED → OLD_BACKUP → NEW_INSTALLED → INDEX_PENDING → COMMITTED。旧文件用同目录 backup，提交日志落在 `.readpico/uploads`；原文件与临时文件均先同步。FAT rename 不是整条事务的掉电原子保证，启动恢复逐阶段判定：缺新主文件恢复旧备份，有新主文件且完整 SHA正确则完成入库，两个都无效显示修复错误。备份删除失败不回报“未上传”，而回报成功＋清理警告。

原书内容身份与进度不立即销毁；新书按新 SHA 建身份，覆盖路径映射，新书不继承旧书进度。相同 SHA 重复导入只增加路径或去重，不重置阅读。用户“清进度”与覆盖是不同命令。

为了保留旧文件和整本暂存，创建会话预检需保证 `新文件总字节＋manifest/索引安全余量` 额外空间，不按“新文件减旧文件大小”放行。最多 1 活动上传、最多 8 持久未完成会话，临时数据总量不超过 TF 可用配额且保留 16 MiB 安全空间；用户可查看并清理，不后台偷删活跃会话。

## 6. 校验与入库队列

用户看三个独立阶段“发送 / 校验 / 整理书库”。字体进行 SFNT/字形边界检查；图书先结构签名与快速元数据，全文 EPUB/PDF 兼容问题留详情。导入摘要可复用上传流状态，但断电后需要重建完整 SHA，后台顺序读，不阻塞 HTTP 后续块。

封面慢任务有独立队列，先入书名卡。可以开刚完成的书；打开优先级高于剩余书封面。批量队列一书失败不取消其余，网页逐书显示可重试/不支持/空间不足。

传书时保持 UI 字体，关闭被替换 TF 字体源。成功字体标记“可选择”，停止传书后在设备字体列表预览并选择，不突然替换正在显示字体。覆盖前确认新 TTF 内容摘要，旧字体失败可恢复。

壁纸作为独立资源kind上传JPEG/PNG到 `/sdcard/wallpapers/`，上限8MiB且受源像素/解码预算限制；设备预览、裁切与应用后才生成内部锁屏缓存。字体/壁纸使用同一失败恢复和后续持久续传协议，目录、大小、文件类型校验按kind分别执行。完整管理流程见 [字体上传与锁屏壁纸](FONTS_AND_WALLPAPERS.md)。

## 7. 吞吐与性能

目标不是 WiFi PHY 标称带宽，而是块确认后的有效 payload。初值：64 KiB 块、单连接、接收 8–16 KiB 流缓冲复用；设备屏更新 ≤1 Hz，仅小区域；导入封面在上传间隙工作；二维码固定区域不刷新。

基准：5 次各 32 MiB 不可压缩文件，AP 距离 1 m、无拥挤干扰，STA RSSI≥−60 dBm；TF 预先写测中位数≥2 MiB/s，足够空间；上传计时从首块开始到最后持久 ACK，完整校验入库另测。AP 中位目标≥0.5 MiB/s、STA≥0.8 MiB/s，p95 块 ACK≤250 ms。若媒体同步每块太慢，可提高 128 KiB 块并同步预算与协议版本，不能以未落盘 ACK 伪提速。

mDNS 名称为 `readpico-XXXX.local`，IP/二维码总是可用；不承诺路由器隔离网络能直连。不使用自动 captive portal 截获作为唯一入口，手机 HTTPS/私有 DNS 兼容问题提供手动地址提示。

## 8. USB MSC 流程

进入 USB：暂停读取→保存→关闭正文/PDF/图片/TF 字体→等待所有 lease 归零→同步并卸载 TF→交给 MSC block backend→显示“电脑正在管理”。固件不读取书名/封面/索引，不在 block 写入时同步访问 FAT。

退出：优先主机安全移除→设备退出/线拔出→停止 MSC→重新初始化介质→挂载→媒体 epoch更新→扫描变更/哈希→回书架。主机未弹出时提示仍可能有写缓存；设备按钮不能保证电脑缓存已写完，需用户明确确认强制退出风险。

异常断线不自动格式化，重挂失败指向电脑修复。USB 模式默认导出 TF，不能给主机暴露 nvs/ota/私有 data。TinyUSB CDC 调试与调试恢复需要稳定枚举验证；MSC 不可用时 WiFi 主通道仍满足快速传书。

## 9. 电脑辅助工具

计划 `tools/importer/`：Windows-first CLI＋本地拖放网页界面，复用设备 v1 API。`calibre ebook-convert` 作为用户可选安装的外部转换工具，明确检查版本/许可和是否安装；不自动捆绑大工具或破解 DRM。MOBI/AZW3→EPUB；PDF 原生优先，可选 OCR/转 CBZ 由外部工具完成，设备不承担 OCR。

所有转换输出新文件，保留原文，预览书名、封面、章节与源格式，再发送。纯手机网页可以传原生格式，但完整 MOBI/AZW3 转换初版需要电脑；这项限制写进帮助，避免虚假的“手机全格式秒传”。
