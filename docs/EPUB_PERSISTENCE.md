# EPUB语义位置持久化与保存屏障

0.0.26加入pn_epub_progress与pn_epub_save，原生会话的确认显示回调可以保存规范章节路径与locator-v1原文位置。实际进程重启/重新打开/重排恢复已通过核心集成测试；设备/SDL EPUB交互界面后续已装配，不能把这项验证当面板上的续读验收。

## 共享类型与源身份

解析器、导航与存储共享pn_core/pn_semantic.h，位置/版本/元素枚举值未改，locator仍v1。存储仅依赖pn_core，不形成storage→format→text循环。pn_epub_progress包含外层确认的完整内容SHA和章节location；UTF8规范路径相对出版物，不能用绝对路径、空段、点段、反斜杠、冒号、query/fragment或控制字符。允许合法Unicode和原文件名，最大1023byte；位置保留64bit element/offset与32bit run。持久位置必须chapter_start=false、element非零，ELEMENT的run/offset为0。chapter_start是请求资源首的运行时标志，不持久化。

外层先计算/验证原EPUB SHA，再load和恢复；同字节换文件名仍是同一本书，不根据路径猜身份。不同内容SHA返回STALE_JOB，不覆盖、擦除旧记录。字段验证不能证明目标仍存在；随后pn_epub_reader JUMP仍必须验证原出版物资源和语义事件，失败不默默回到书首或修改记录。

## 载荷与A/B

使用既有PNBL A/B同步读回及CRC，PNEP内载荷schema1最多1091byte，不能塞进256byte PNJR。建议外层使用逐内容SHA的epub位置文件；当前核心测试用独立state目录，产品目录装配仍在后续阶段。

| 偏移 | 长度 | 内容 |
| --- | --- | --- |
| 0 | 4 | PNEP magic |
| 4 | 2 | schema1，小端 |
| 6 | 2 | reserved=0 |
| 8 | 32 | 原EPUB SHA256 |
| 40 | 2 | locator version1 |
| 42 | 1 | ELEMENT0 / TEXT1 |
| 43 | 1 | reserved=0 |
| 44 | 8 | element |
| 52 | 8 | raw Unicode scalar offset |
| 60 | 4 | run |
| 64 | 2 | UTF8 path字节数1..1023 |
| 66 | 2 | reserved=0 |
| 68 | 可变 | 规范UTF8章节路径，不含NUL |

多byte整数小端，不直接保存C struct、bool或padding。未知schema/locator/kind/reserved返回UNSUPPORTED，不猜默认；无效路径/长度/字段为CORRUPT；错误不改输出。EMPTY仅双槽都不存在。外层CRC损坏可按PNBL规则使用另一个有效槽；内载荷未知/损坏不自行回退为某个猜测位置。没有新格式化、NVS擦除或字体/书源修改。

## 确认、阈值和重试

pn_epub_save_init的baseline只能是已读到的持久记录。pn_epub_save_confirm可直接用作pn_epub_reader的确认回调：显示、正文/图片源校验全部成功后才接收位置，随后检查5次真实前后页或30秒阈值。相同位置的重画/强刷不新增dirty或翻页计数；重排、跳转更新实际原文位置但不伪装为真实翻页。

失败保留最新显示位置、dirty、计数和起点；自动重试至少隔1秒，等待返回BUSY。显式flush可立即重试，离书、锁屏、切电必须先flush成功；失败留会话及用户反馈，不能关闭源后丢dirty。显式flush失败后自动tick也会按1秒重试，不因尚未到普通30秒阈值而忽略已请求保存。

同步写入返回错误可能已经提交；flush立即重新load，只有完整CRC/schema/SHA及全部位置字段与当前确认位置一致才认定提交成功。其他情况仍dirty并重试。不把保存失败当显示失败：新位置已实际显示，导航visible不能回滚到旧页。

## 验证与剩余装配

python tools/dev.py host包含：

- epub_progress：真实A/B文件、中文路径、1023byte路径、64bit值、重开、CRC槽回退、版本/保留字段/UTF8/规范路径/SHA拒绝、所有观察到的load分配点故障及介质失效，零残留。
- epub_save：5次真实翻页、30秒阈值、重复位置、错误保留dirty、1秒重试、立即关闭保存、实际写入后返回错误的读回确认、时间倒退。
- epub_reader_session：真实ZIP/XHTML与模拟显示/度量，跨独立进程保存/恢复父元素在子节点之后的文本位置，完整原文件SHA写入记录，同字节别名、宽度重排均恢复C；改变内容SHA拒绝旧记录且文件不变。未显示页不进入保存，关闭分配故障保持dirty/会话，修复后可完成屏障。

测试中的显示owner是模拟，不是实际GUI或面板。字体/图片真实绘制与显示owner装配、恢复错误中文引导、逐书100条EPUB书签、最近阅读、TOC/七项排版、封面缓存及完整真机功耗/资源仍需要接入。保存codec/策略本身没有创建硬件睡眠或产品交互入口，第一版目标保持完整。


实际应用与独立PC窗口见 [原生EPUB应用](EPUB_APP.md)；此前核心模拟显示边界不变，设备/书架和完整UI继续装配。
