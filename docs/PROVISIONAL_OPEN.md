# EPUB 临时身份首屏设计（P-06，默认关闭）

状态：设计已定，代码未启用。开关计划为 `CONFIG_PN_EPUB_PROVISIONAL_OPEN`，默认 `n`；真机 A/B 通过前不得默认开启。目标源为 0.9.13 后的冷开优化分支。

## 目标与不变量

- 首屏前只校验 ZIP 中央目录、本地头、实际读取条目的 CRC；不再等待整书 SHA。
- 临时身份只作为**会话内缓存键**，不是安全认证，也不写入长期身份缓存。
- 正式 `book_id` 仍由完整文件 SHA 产生；完整 SHA 在首屏后后台完成。
- 临时会话不写正式进度、页码索引或语义缓存，避免身份未定时污染数据。
- 退出、换卡、USB、锁屏取消和任务失败必须释放临时键与租约。

## 临时键

仅对“无最近记录、无已缓存身份”的本地 EPUB 启用。

```
provisional = SHA256("PNPROV1" || path || size || mtime_ns || ctime_ns)
```

不读取正文、不读取中央目录即可计算，成本可忽略。它只用于：

- 本次 `pn_epub_open_job` 的 `book` 占位；
- 会话内页索引/语义模型的命名空间（不落盘）；
- 打开任务取消/失败时的清理标识。

明确不用于：跨设备身份、最近记录、长期进度、发布清单或安全判断。

## 状态机

1. **begin**
   - 校验文件 size/mtime；
   - 进入既有 ZIP 目录分步扫描；
   - 记录 `provisional`、正式 state 目录、期望 size。
2. **ZIP/CRC**
   - 复用 `pn_zip_scan_job_step` 与 `open_complete` 的目录/本地头校验；
   - 任一实际读取条目 CRC 失败仍报 `PN_CORRUPT`，不回退为“先显示”。
3. **首屏**
   - `pn_epub_app_open_job_begin_provisional` 走 fast 路径；
   - `open_complete` 跳过 `state_open` 的进度/样式/save 读取；
   - 允许读取全局字体/样式偏好，但不写临时身份文件；
   - 首屏 `active_step(OPEN)` 正常显示，加载页释放。
4. **后台完整 SHA**
   - `pn_epub_app_verify_step` 对 `book_source` 做完整 SHA；
   - 成功后取实际 digest（`book_file.identity`），执行 adopt；
   - 失败/换卡/取消：丢弃临时模型，返回可操作错误。
5. **adopt**
   - `a->book = actual`；
   - 初始化 save/progress/style 文件（以 actual 为键），**不加载旧进度**，从当前可见位置开始记录；
   - 打开页码索引/语义缓存的实际键；
   - 清理 provisional 标记；此后按正常持久化运行。
6. **关闭未 adopt**
   - 不写任何临时进度/索引/身份；
   - 下次仍走严格 SHA 或再次临时开书。

## 需要的代码拆分

`pn_epub_app.c` 当前 `state_open` 同时完成目录/介质/租约、进度、样式和 save。临时模式需要拆成：

| 函数 | 职责 |
| --- | --- |
| `state_mount(a, directory)` | 创建目录、挂 state media、取得 lease，设置 state_directory |
| `state_load(a, now)` | 加载进度/样式/save，设置 persistent |
| `state_save_start(a, now)` | 以当前 `a->book` 初始化 progress/style/save，不加载旧进度 |
| `state_open` | 兼容现有调用 = mount + load |

`open_complete`：

- 临时模式：`state_mount`，不 `state_load`；字体/样式仅用全局偏好；
- 正式模式：`state_open`，行为不变。

`pn_epub_app_verify_step`：

- `a->provisional && (s==PN_OK || s==PN_STALE_JOB)` → adopt（临时键不要求等于实际）；
- 仅 `PN_IO/PN_CORRUPT/PN_STALE_MEDIA/PN_CANCELLED` 不 adopt。

## 失败与回滚

| 场景 | 行为 |
| --- | --- |
| ZIP/条目 CRC 失败 | 首屏前返回 `PN_CORRUPT`，不 adopt |
| 后台 SHA 完成 | adopt 实际 identity，开始正常保存 |
| 后台 SHA 失败/换卡 | 关闭临时 app，保留原书/原进度，提示重试 |
| adopt 时 save 初始化失败 | 不写临时文件；返回错误，回到书库 |
| 用户在 adopt 前退出 | 不写临时进度；下次严格/临时重开 |
| adopt 后再次打开 | 使用实际 identity 的正常路径 |

## 测试

必须新增 host 测试：

1. 临时开书首屏成功，且 `state_mount` 前 `/data/progress` 没有该书 `.epub.a`；
2. 后台 verify 前没有 provisional 进度/索引文件；
3. verify 成功后 `book` 等于完整 SHA，并创建 actual progress/save；
4. verify 失败不创建 actual 文件、池/租约归零；
5. ZIP CRC 损坏仍首屏前失败；
6. 同一本书第二次打开走 actual 身份，不再临时；
7. `CONFIG_PN_EPUB_PROVISIONAL_OPEN=n` 时 app_main 不调用临时 API；
8. 现有 `epub_app`、`epub_fast_open`、`epub_long_chapter`、`identity_takeover`、全量主机回归不降级。

设备 `-diag`：

- 临时首屏时间、后台 SHA 完成时间、adopt 前后池峰值；
- 失败/退出后状态目录无临时文件；
- 10 本 EPUB × 4 字体，与严格路径首屏/翻页帧一致；
- 大书 6.8MB 首屏目标 ≤3s、1MB 级 ≤1.8s（待真机确认，不作承诺）。

## 未决问题

- adopt 后是否加载 actual 旧进度：本设计选择**不加载**，从当前页继续记录；若产品要求续读旧位置，需在 adopt 时提示/询问。
- 全局字体/样式偏好的读取需要 state_mount 但不写临时键；若实现复杂，可临时用启动默认字体，并在文档/UI 标注。
- 临时会话期间禁止页码索引落盘；用户退出前不会保存进度，这是默认关闭实验的明确代价。
- 真机 A/B 通过前不得默认开启；未通过则删除开关代码，不保留半成品默认路径。
