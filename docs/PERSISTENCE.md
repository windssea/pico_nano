# 内容身份与持久位置维护契约

版本0.0.4保留并扩展0.0.3实现的共享C的原文件SHA-256、A/B记录codec、带租约的stdio文件适配和TXT位置载荷。主机用真实文件验证；工程固件编译这些源文件，但启动测试页尚未调用它们，也尚未挂载内部data文件系统。本文描述接口边界，不代表已经支持打开TXT阅读。

## 内容身份

`pn_identity_file`在有效READ或WRITE租约内按4096-byte块读取，计算整个原文件的32-byte SHA-256。相同内容不同路径得到相同book_id；同路径同大小但不同内容得到不同摘要。路径实例、书目去重和导入索引尚未实现，不能用book_id替代path_id。

算法按 [NIST FIPS 180-4](https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf) 的SHA-256规范实现，主机以Python hashlib独立对照；它用于内容识别，不作为网络认证或签名实现，不宣称密码模块认证。未来传输安全使用审核后的TLS/认证库。

调用方传入byte_limit，超限返回LIMIT，读取失败返回IO，介质代次变化返回STALE_MEDIA，失败不更新输出摘要。即使上限为0也允许空文件；不会把超限的前缀当完整身份。没有整书分配，主要栈缓冲4096 bytes，另有摘要状态与压缩工作区；接入RTOS前必须配置并实测worker栈水位，不能在默认小栈上直接调用。

调用方必须禁止文件并发修改/替换。租约排除通过本服务取得的写入和USB访问，不能阻止服务外的宿主工具；本实现不检测同大小原地修改。身份计算目前同步执行、没有deadline/取消/进度回调，未来导入worker接入前仍需补充。卡被拔出的物理错误仍依赖底层I/O和实际介质监测。

## A/B记录线格式

所有整数使用小端，按字节显式编解码，不保存C结构体布局。最大载荷256 bytes、最大文件276 bytes，无动态分配。

| 字节位置 | 长度 | 含义 |
| --- | --- | --- |
| 0 | 4 | ASCII `PNJR` |
| 4 | 2 | schema=1 |
| 6 | 2 | payload长度0–256 |
| 8 | 8 | sequence，1起始、不回绕 |
| 16 | payload长度 | 不透明载荷 |
| 16+payload长度 | 4 | CRC-32/IEEE，覆盖此前全部字节，初值与最终异或均0xffffffff |

文件长度必须精确匹配，不接受尾部数据。load读取两个槽，选完整且CRC有效的最高sequence；另一槽缺失、截断或CRC损坏时可回退到有效槽。相同sequence且载荷不同返回CORRUPT；未知schema返回UNSUPPORTED；任何实际读取错误返回原错误，不猜测另一槽更旧。错误保持输出不变。

两槽都不存在返回EMPTY；只存在损坏记录或两槽皆损坏返回CORRUPT。save不会自动删除、重置或覆盖这些证据，需要未来恢复UI/显式修复流程。首次保存被打断而没有旧有效槽时，也可能进入CORRUPT，不能宣称能恢复尚未保存过的位置。

save先load，选择另一槽，以sequence+1写入，再同步并读回逐字节核验。旧有效槽不会在本次保存中截断；下一次成功保存可覆盖更旧槽。UINT64_MAX时返回LIMIT。write_sync失败仍可能已经写出完整新记录；调用方保持dirty并重新load确认，不能把失败理解为必然没有落盘。

## 文件适配与介质边界

`pn_journal_files_init`绑定调用方管理的两条路径及租约副本。路径长度小于384 bytes，两槽必须是不同底层文件；当前仅拒绝相同路径字符串，调用方还必须排除软链接、硬链接和路径别名。它不会创建目录、格式化或挂载。文件路径必须来自存储服务可信索引，不直接接收上传请求中的任意路径。

读取需要READ或WRITE，保存需要WRITE，USB租约不得进行应用文件访问。适配在I/O前后验证租约，处理fopen/fread/fwrite/fflush/fsync/fclose错误。读取只接受ENOENT作为缺失；EACCES等不能当新书首次保存。

write_sync执行stdio写入、fflush、fsync文件描述符和关闭；底层不支持同步时明确失败。它未提供父目录同步、FAT元数据事务、控制器缓存掉电保证或物理卡拔出监测。主机截断故障和进程重启测试只证明codec/选择逻辑与软件文件路径；真实LittleFS/FAT、实际拔卡和断电验证仍必须上板完成。

## TXT位置载荷

`pn_txt_progress_save/load`在journal里保存固定64-byte载荷：

| 字节位置 | 长度 | 含义 |
| --- | --- | --- |
| 0 | 4 | ASCII `PNTP` |
| 4 | 2 | TXT载荷schema=1 |
| 6 | 2 | encoding：1 UTF-8、2 UTF-16LE、3 UTF-16BE、4 GBK |
| 8 | 32 | 原文件SHA-256 |
| 40 | 8 | 原文件source_offset |
| 48 | 8 | 原文件source_size |
| 56 | 4 | 非零paragraph_version |
| 60 | 4 | 保留为0 |

offset必须不超过size。load必须提供已经识别的expected book_id，摘要不同返回STALE_JOB，不覆盖调用方位置。编码枚举不表示已经实现这些编码解析器；实际字符边界、源映射、策略迁移和位置锚点仍由后续解析器负责。未知schema或保留字段返回UNSUPPORTED，不降级读取旧格式。EPUB/PDF/FB2/CBZ位置载荷尚未实现。

## 复跑测试与接入工作

```powershell
python tools/dev.py host
python tools/dev.py firmware-ci
python tools/dev.py firmware-board
python tools/dev.py docs
```

host包括标准abc摘要、相同内容副本、同大小替换、资源上限和过期租约；独立hashlib对照16组边界/百万字节样本。journal覆盖276个最大记录写入截断点、192个单bit破坏、同步失败后实际提交、序号耗尽、真实文件读回与独立进程从每个TXT记录截断点恢复；CRC以Python zlib独立核对，均运行ASan/UBSan。

5次翻页/30秒同步策略和单TXT书签操作现见 [阅读状态契约](READING_STATE.md)。尚需内部data文件系统的非破坏挂载、存储owner/worker队列、持续介质事件、索引与path_id、锁屏/离书deadline屏障，以及真机掉电验证。工程启动不自动创建这些文件；不要在没有阅读会话的测试页写伪进度到用户TF。
