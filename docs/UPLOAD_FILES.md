# 上传文件安装与恢复

pn_upload_files为[上传内核](UPLOAD_TRANSACTION.md)提供真实文件port。PC测试走真实文件、文件fsync和目录fsync，包含逐同步点中断后新进程恢复。代码同时编译到ESP32-S3组件，但设备须提供实际命名空间同步和实时空间查询，尚无设备/网页传输入口；不能把PC文件操作当TF掉电验收或已能无线传书。

## 路径与所有权

owner提供已挂载可信绝对根（最长95字节，不接受根目录本身、点段或空段），单个文件名仍按内核UTF8/路径规则验证。kind固定映射books/fonts/covers/wallpapers，不接受客户端目录。会话数据在root/.readpico/uploads/随机ID.part，上传日志ID.a/b，安装意图ID.install.a/b；覆盖备份在目标同目录.pn-backup-ID。根/祖先/目录/叶节点在PC拒绝符号链接，普通文件操作必须由串行owner排除其他进程同时修改。

整个会话借用内核的独占WRITE租约。活动阅读、TF字体或USB会阻止开始；绑定时不能释放port。bind失败不会解除其他活动会话；关闭内核后unbind关缓存文件并释放绑定，随后可释放port，不删持久会话。没有挂载、格式化或隐式读写并发路径。

最多8个不同未完成会话，连无上传日志的孤立暂存/安装意图也计数。已COMMITTED/CANCELLED不占活动配额；损坏/未知日志阻止新会话，不自动擦除。新上传需有新文件全长＋其他未完成会话尚未发送长度＋16MiB余量的当前空闲空间，不按新旧长度差放行。不预分配整本，后续写失败不前进ACK。

## 正向同步

数据创建/截断/写入用精确长度和fsync，创建/日志写入/rename/unlink还同步对应父目录。同步错误不靠读回认成功；保存安装意图失败后阻止继续操作，必须关闭重开。PC默认fsync目录和statvfs；ESP的sync_directory/space_free回调为强制项，缺失返回UNSUPPORTED，不能假报同步成功。

参考BSP的read_pico_sd_sync会卸载卡，不能用于逐块上传。ESP-IDF的文件f_sync/命名空间实现须在SDK与真实卡上分别检查，[官方FatFs同步说明](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/storage/fatfs.html)也不能代替卡的掉电验证。设备已通过 [FAT适配](DEVICE_TRANSFER_STORAGE.md) 接入这些回调，仍需真实卡掉电验证。

若初始安装意图已保存，但进程在空part创建后、上传日志写入前中断，恢复可用同ID/媒体的PREPARED意图重建初始上传记录。part只能为空或尚未创建；已有非空但无有效上传日志的数据拒绝猜测或覆盖。

## 覆盖及恢复

创建覆盖请求必须明确给出原文件size＋完整SHA；不一致不写part，不把同名当同一原文件。授权意图在创建时同步保存，重开使用原意图，不从后来同名文件猜权限。新文件初次导入若已存在同名文件返回BUSY；重命名和同内容队列去重由后续产品层处理。

安装意图先记录VALIDATED和完整新SHA，目标旧主必须匹配原身份，再rename至同目录备份。备份身份确认后，part安装为新主；同步两个父目录、核对新主完整大小/SHA，再记录NEW_INSTALLED。恢复不只信phase，结合新主/part/备份的实际内容身份：已有正确新主继续提交；旧主已备份而part完整则继续安装；新主缺失/损坏且没有完整候选但旧备份有效时先恢复旧主并报告损坏。损坏新主暂存回part，不删坏数据或旧备份。不匹配的未知文件不覆盖。

新主已经确认安装后，旧备份/重复part清理失败返回成功＋cleanup_pending，安装意图停在NEW_INSTALLED；owner可通过查询提示清理警告并调用cleanup重试。此时不应告诉用户“未上传”。成功清理后记录COMMITTED，绝不清除新主。字体替换不自动改PNFP选择，使用中的源须先关闭，后续管理层必须处理引用身份变化；不能宣称字体替换/删除产品流程已完成。

## 实际资源校验

TXT完整严格编码探测（UTF8/UTF16/GBK），随后拒绝不支持的控制内容；TTF通过真实字体引擎逐字形加载所有轮廓，不等于所有字号的光栅验收或中文字覆盖保证。EPUB检查容器/OPF和各ZIP资源完整CRC，累计解压长度最大512MiB；不能由此证明每章XHTML/作者CSS均可阅读。PNG/JPEG完整解码至EOF，在2×2临时灰阶帧做验证，仍受原尺寸/像素、progressive内存预算限制，扩展名必须与内容类型一致。

当前文件port对PDF/FB2/CBZ返回UNSUPPORTED，原生格式路线仍按[格式计划](FORMATS.md)实施；不把这些格式的扩展名识别当原生支持。解析/摘要当前同步，任务取消与响应时间预算仍需接入。

## 持久意图

PNFI schema1封装在PNBL双槽，头128＋最多255文件名字节。magic0、version16@4、phase8@6（0PREPARED/1VALIDATED/2OLD_BACKUP/3NEW_INSTALLED/4COMMITTED）、kind8@7、flags32@8（bit0覆盖，bit1已知新SHA）、reserved32@12=0、media_id64@16、new_size64@24、old_size64@32、id16@40、new_SHA32@56、old_SHA32@88、name_length16@120、reserved6@122=0；128后原UTF8名字，无NUL。整数小端，版本/保留字段/媒体/ID/长度/身份语义全部核对，未知不覆盖。

## 验证

python tools/dev.py host新增upload_files：真实旧书覆盖及22个同步点进程退出/恢复，空part无初始日志恢复、新主损坏回退旧主、清理警告与重试、旧身份变化、叶/目录符号链接、8会话、未完成字节预留及空间不足拒绝；清理过程中发现新主损坏不能提交COMMITTED。upload_resources覆盖完整TXT多编码、TTF正常/坏轮廓、EPUB正文资源CRC错误、PNG/JPEG及截断/伪扩展名；安装SHA与Python独立比较。原内核每个日志字节中断测试保留。

这些是软件和POSIX进程中断证据，不是FAT写缓存断电、命名空间落盘或AP/STA吞吐的证据。样本和运行输出保留在被忽略的build目录。
