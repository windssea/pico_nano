# PicoNano 构建、运行与验证

## 2026-10-28 预绘前缀回归

`page_bitmap`检查绘制前及成功小步后取消保留游标、每个已提交节点只绘制一次；回调自身取消/失败仍废弃候选，半页copy仍为EMPTY且不修改当前帧。`prerender`保留TXT/EPUB模式0–5，新增EPUB模式6/7/8：强制部分预绘、完整字形后输入取消、部分接管后显示失败重试，均对照关闭预绘的完整帧和位置；原低预算、重排/反向、背景/阅读线、失卡覆盖保持。EPUBsteps统计变为逐节点提交，不用于与旧版本直接比较调度耗时。

本批相关host4/4、完整性专项5/5、SDL专项5/5、SDK6.1诊断/CI构建通过，未重跑系统整轮。可见模拟器已用最终源码重建重启并保留。真机仅最终一次对已有60fe511d基线：最新1a08fd2b，两书本轮NEXT均为完整预绘命中；部分前缀分支的软件覆盖不冒充实卡隔离测速，见[报告](READING_DEVICE_TEST_REPORT.md)。

更新：2026-10-28。这里只记录现有可复跑入口；当前实现/未完工作分别见[架构](ARCHITECTURE.md)和[开发指南](DEVELOPMENT_PLAN.md)。固件构建、串口写入与真机验收是不同阶段，默认工具命令不烧录。

## 系统审查回归口径

`python tools/dev.py host`覆盖主机C/ASan/UBSan、工具Python及网页资源Node检查；`sdl`覆盖真实SDL事件循环，自动回归dummy与开发可见窗口分开；`firmware-diag`/`firmware-ci`分别使用隔离SDKCONFIG，`installer`只测试/生成本地安装站点，`docs`核对链接、原组件摘要和分区。独立构建目录可用于候选，不能在另一轮CTest运行期间重链接同一批二进制。最终结论按完整一次运行与专项补测分别报告，不相加成虚构的一轮全通过。

`blob`新增单次读改写的逐字节断写、读回损坏/I/O、未知版本、分配失败、空载荷/缺失和序号溢出；`recent`以真实文件端口计数，变化更新3读/1写、未变化2读/0写，并保留20条、完整路径、通知和换代检查；`recent_idle`覆盖真实阅读退出后的延后保存。`reading_store`另注入stat/readdir/closedir失败及容量查询时失卡，须在不完整计费时拒绝准入、不清理原文件。这些测试随host运行，不改变设备诊断入口。

本轮确定性20条内存端口探针仅对照正式1.0.8相同存储源码与最终候选，记录I/O、分配和池峰；没有用主机时间代替实卡毫秒数，也没有额外真机开书A/B。诊断日志有扰动、父子阶段可嵌套，普通diag不进入真实锁屏浅睡，均不能用来承诺正式版睡眠/功耗。实际结论见[报告](READING_DEVICE_TEST_REPORT.md)，下一步顺序见[系统审查](DEVELOPMENT_PLAN.md#2026-10-28系统级代码审查与优化顺序)。

`reading_store`用实际文件检查根缺失、CRC损坏、I/O拒绝及成功删除事件，验证先回收帧、当前书结构证明受保护、外部文件不动和16MiB/16KiB上限。`exit_checkpoint`覆盖短读退出后生成结构、临时写租约拒绝后的重试、未配置缓存的空待办、容量不足停止、部分结构事务的切书/普通释放与当前页保存。诊断`Cache file phase=... status=... name=...`只报告实际操作；`.bak`缺失而主根有效是正常首次提交状态，不能判成损坏。没有历史事件的根缺失不能证明删除或从未生成。

`txt_resume_pending`用UTF8/GBK真实文件和已提交续读包检查：完整打开确实命中、跨代trusted首屏不耗尽原书校验、原解析画面一致、后台尾部等长篡改仍拒绝。GBK保留既有流式探测保守拒绝后的完整重开，验证恢复帧一致。`exit_checkpoint`另覆盖前台切书不新生成包、部分事务借帧/租约释放以及必要保存失败重试；默认退出/锁屏检查点用例保持。诊断`resume.miss.*`、`bootstrap.miss.*`和`source.blocks.miss.*`记录实际分支，不新增诊断文件读取；根不可用包含缺失或根校验失败。

分块来源回归：`source_blocks`检查完整表/根、逐字节篡改、短尾块、所有提交中断点、低内存/低空间、fsync失败、取消及失卡。`source_blocks_app_host/immutable`通过真实开书入口检查重启、缺件、损坏、截断、备份、残留tmp、启动包缺失及同大小同mtime原书替换；Python独立核对每块SHA与可信根，比较OPEN/NEXT/PREV全帧，另覆盖多章、超大单章和图片。块表命中后`identity_steps=0`，没有块表仍完整扫描；这里的immutable是设备所有权语义替身，不能当实卡测速。

启动结构缓存回归：`bootstrap_app`使用原创小书、48章/超大单章各57.6万字及图片页，跨进程比较正常解析全帧，覆盖实际缺包/根、损坏/截断、版本、备份及同大小同mtime源替换；Python独立核对完整包SHA。`bootstrap_entry_host/immutable`观察真实ZIP任务次数，命中须为0，跨代或普通主机可在块表认证后按需完整校验原书块；同代完整备忘仍独立；块表缺失则完整复核原书；缺包/坏包须重新建立ZIP任务。`bootstrap_file`逐个中断提交边界，检查全部spine资源字节与CRC、共享配额仅删除自有闲置包、同步失败、取消、拒分配及失卡后不刷新/删除新代暂存；原池归零。它们不是TF或面板实测。

诊断镜像可在首页发`cache 0/1/2`切换正常/缺失/损坏读端注入，以`Bootstrap test fault=`owner回执为准；`bootstrap.hit/load/stored/fault`与`resume.hit`分开计数。此入口不写坏或删除真实缓存，正式镜像编译掉故障端口。主机实际文件故障、真机读端注入、真正重启后的完整来源复核须分开报告；冷开、同代命中、首次NEXT及加载/正文job计时不能混合。

固件采用混合优化：SDKCONFIG默认`CONFIG_COMPILER_OPTIMIZATION_SIZE=y`（`-Os`），根CMake在阅读、字体/排版、XML与图片解码、存储、主控制层及显示/TF热路径上追加`-O2`。以生成的`compile_commands.json`中最后一个优化参数为准，不通过删字体、图片、功能或放宽6MiB槽的10% OTA余量门槛缩包。正式板级与CI均保留WARN/ERROR日志；`sdkconfig.diag`明确恢复INFO及原有测试/计时能力。已有本地SDKCONFIG不会自动被defaults覆盖，切换策略时应同步对应优化和日志choice，随后重建并核对生成配置；板级120MHz与CI80MHz仍隔离。

跨机器重新编译可能改变引导程序字节。继续兼容既有签名OTA链时，完整包使用`python tools/package_upgrade.py --full --qualified-bootloader releases/pico_nano-1.0.5-3c63c46-full`显式复用已资格引导；工具检查旧正式清单的产品/板型/SDK、芯片头、长度及SHA，不覆盖本地编译产物，并在新清单记录引导来源。发布前还须独立验签旧OTA清单，并核对其引导摘要、新完整包引导及新OTA清单三者一致。不能仅凭文件名宣称引导已通过设备资格验证；若要变更引导，必须另做迁移资格测试。

用户明确要求同版本重构建时，将该版本旧包目录保存在`releases/archive/`，当前目录只保留新源码对应的一组包。公开服务保留旧提交后缀的不可变附件；仅稳定名称`PicoNano-ota-manifest.bin`与`SHA256SUMS`需更新。Gitee手动同步可指定`previous_source`（完整旧源码SHA，对应工具`--replace-stable-from`）：先完整校验同版本归档包及OTA公钥连续性，上传新不可变包，旧稳定文件先另名归档并匿名读回，再替换原名；未知冲突、重复名称或归档校验失败仍停止。默认同步与只读核验不允许替换，也不删除其他版本。已安装同版本旧构建的设备不会因版本相等获得OTA升级，需使用USB更新。

续读包回归为`resume_app`（独立进程重启、A/B换书、缺包/根、损坏/截断、显示失败重试、时间/字号/字重/背景变化及图片完整帧）和`resume_file`（三类无损像素、全部提交边界中断、可信SHA、空间不足、同步失败、取消/失卡与自有配额清理）。`resume_window`驱动真实SDL事件循环，核对开书/NEXT/PREV完整画面、跨进程命中和删包回退；自动回归使用dummy，开发仍须重建并保留可见窗口。三者分别随`python tools/dev.py host`/`sdl`运行。

`resume_switch`与`resume_switch_large`在同一进程、同一个6MiB池和介质中执行A→B→A→B，实际park/drop单个热会话；分别使用原创小书与48章/超大单章各57.6万字的书，不能称作真实《绍宋》《明颂》。覆盖完整保存、当前包写出部分行后取消、下一包取消、下一页未预热时先存当前包，核对重开命中、退出/恢复完整像素和位置、首次NEXT及释放归零。`PN_SWITCH_EVIDENCE`可保存原始计时/帧摘要；时间排除截图写盘，但仍是主机软件观察，不包含TF/面板。真实睡眠诊断另输出`resume.stored/hit/load`事件，不逐行打印写包，正式配置不增加这些日志。

续读快速入口回归增加NEXT后的立即PREV，核对原当前包回翻全帧及真实索引绑定次数；完整命中时不以延后到PREV绑定索引冒充优化。`epub_reader_session`验证回翻仅接受精确已确认历史及相邻下一页锚点，失败回执保持位置/历史。`resume_app`的EPUB完整序列现在包括OPEN、NEXT、PREV三次包命中，TXT保持原RAM回翻；坏包、缺文件与设置变化仍逐完整帧对照。

快速入口回归另以链接器观察真实`pn_page_index_open`次数，续读首屏命中须为0；`resume_app`通过`PN_RESUME_ENTRY=1`覆盖显式未显示候选、缺包/坏包、设置变化和失败重试。`reopen_entry_host/immutable`用原创实际书源核对完整SHA证明复用、错误expected拒绝及换代后重新完整复核，PC仍不复用证明。诊断`epub.open.source-memo`只表示本次命中原完整证明；`resume.sources/decode`分出来源复核与包解码时间，父子阶段有嵌套，不能相加当总耗时。

最终性能对照使用独立冻结的当前正式版本与最终候选，保持同配置、同输入和状态；报告只汇总可操作开书、首次NEXT、完整帧和池峰。现有`test_local_resume.py`、`test_resume_window.py`为可选历史观察器，不在每次开发默认执行，也不把不同旧提交逐一拿来A/B。两种观察器分别包含/排除截图写盘，必须标明计时边界，软件结果不代表TF或面板。

`catalog_index`与`catalog_mount`增加stdio定位计数及逐次故障注入：首屏不探读页外记录、同页/搜索顺序读，仍用独立目录扫描核对首屏、倒序、字母与搜索；三次定位任一步失败不得发布半页，之后可重建且原READ租约有效。定位次数是软件端口计数，不代表TF事务或耗时。

## EPUB连续NEXT工作量回归

锁屏变慢诊断：`device_sleep_long_timer`以假时钟推进4小时的500ms定时复核，时间本身不使介质连续性失效；原非电源IRQ仍拒绝热恢复并保留首次原因。`device_lock_validate`的16轮覆盖2秒/5分钟/4小时同一阅读对象、完整帧/位置及显示失败重试，另外保留疑似换卡时的冷重开。它替换硬件端口，不是实际等待、SD或功耗测试。产品WARN日志`Reader wake cold fallback`区分无挂起页与连续性/挂载条件，`reason`是失去证明的原因而非确认拔卡；INFO热恢复日志带回执耗时。用户故障仍需真机日志对应，不能把来源保护直接移除。

用户已确认“解锁后出现封面加载，再恢复正文”，说明确实进入重新开书分支。可用`python tools/dev.py firmware-wake-diag`构建`build-wake-diag/pico_nano.bin`（`1.0.6-diag`），独立SDKCONFIG叠加`sdkconfig.wake-diag`：保留真实锁屏浅睡及5分钟自动锁屏，INFO日志开启，逐字阶段计时/串口控制台关闭。普通`firmware-diag`仍用于串口自动化，会用轮询代替锁屏浅睡，不能混作这项故障的硬件复测。

唤醒诊断的`PN_WAKE_TRACE`只在专用配置启用：product owner保存固定RAM快照，恢复入口最早2秒后每3秒重播一次`Reader wake trace`，最多5条，繁忙时后延。原生USB真实浅睡后可能仍无法恢复串口，本轮已出现证据中断，不能以“没有日志”判定CPU是否运行。诊断另在内部`/data/progress/wake-diag.a/b`保存少数阶段，沿原A/B CRC/同步/完整读回，两槽最多552B、保留64KiB余量；不创建目录、格式化或访问TF/NVS，写入失败不改变阅读回退。重启后以`Reader wake saved`输出上一条有效记录，最多5次；最新槽损坏可回退旧槽，旧阶段只说明最后可靠记录，不能当作异常堆栈。串口和Flash同步均扰动时间，不能把诊断耗时当正式性能或功耗。

记录`locked_ms`、`retained`、`continuity`、`reason`、`sd/present/mounted`、`warm/warm_ms`以及`start_ms/ready_ms`；`warm=-1`表示未执行热恢复，`0`表示成功，其余为原状态码。`retained=0`排查锁屏前菜单/关闭路径；`continuity=0`及reason排查保守介质失效；`sd`非零或mounted=0排查底层挂载。持久载荷`wake-v1`包含阶段、入口/正文回执/等待时间，flags位0..4依次为slept/retained/continuity/present/mounted。先开始串口采集，再按物理键锁屏/等待/解锁，保留正文后15秒；失联后恢复设备并重新采集启动记录。该入口只编译，不自动刷写或发布；部署先核对实际分区及回退镜像，不使用固定偏移盲写。

`opening_legacy`与`opening_frozen`使用原创EPUB及完整校验的封面，按正常/失卡/重挂载同序列逐帧比较；旧查询模式出现正式/默认/正式，新展示快照三帧逐字节一致，正常首帧与旧模式一致。另验证缺文件、换目标、可选申请失败及取消；冻结的是展示，不代替书籍校验。设备与SDL均调用同一实现；主机条件不等于真实卡、USB或面板验收。

字体去重使用主机/固件同一`embed_font_view.py`生成视图，构建目录的`pn_font_view.json`记录原始SHA、私有字节、片段数及数据节省。`font_storage_generator`验证原TTF完整重建、确定性和坏输入拒绝；`font_blob`验证跨库读取、EOF及坏段边界；`font_storage_validate <原UI.ttf>`验证全字节、10万次随机读取、多字号绘制哈希及池峰。旧提交加入同一观察器并以`PN_FONT_STORAGE_BASELINE`编译后先冻结旧可执行文件，当前版本默认走共享视图；该旧观察模式只用于旧源码，不能用新源码假冒旧基线。端到端旧新对照可复用`test_resume_window.py`第三参数传入冻结的旧SDL，原TTF与全部字体许可证清单不变。

1.0.6后的小范围缓存交接回归：`cover_preview_host`/`cover_preview_immutable`用实际PNG/PNCV核对像素及元数据、来源替换、CACHE_WRITE、换卡、USB、取消与缺文件；常规PC仍stat，设备不可变源契约下常驻命中零stat、零新分配。`page_bitmap`覆盖原生整帧、带填充行和取消不写当前帧；`pn_reading_copy_bench`只计连续缓存拷贝，不含解析/TF/面板。旧新对比应先用同一观察器冻结旧可执行文件，再交替固定CPU多轮测量；微阶段比例不能称为整次开书/NEXT提升。

`epub_long_chapter`核对120次NEXT、40次PREV的全部161帧与同步流式基线一致，并限制单页小步数，防止已验证缓存前文逐字重扫。`resume_turns`覆盖0/1/16/64步扫描重叠、一次导航最多一次续读包探测，以及原创大章第80页热开后连续NEXT；后台索引分别停在0/1/32/500步后由NEXT接管，直接连翻8帧、回翻再连翻9帧逐字节比较，关闭池归零。`product_open_loop`另从真实异步邮箱交替投递15次点击/右键NEXT。

真机复测沿用失败书籍和原续读位置，不清缓存；保留每5秒`Reader NEXT waiting`的`steps`与`phase`，并记录首次/第二次NEXT显示回执、PREV及重开归位画面。主机有界工作量通过不能代替原失败真机验收。

## 冷恢复与退出收口回归

`opening_feedback`编译真实设备main和原文件/显示桩，使用8MiB资源EPUB与完整外置系统字体：第一加载帧前不得读取外置字体，来源阶段不得绑定字体/发布正文；覆盖取消后继续、热唤醒直回正文、冷唤醒仅一张加载页、异步挂载取消/重试不重启探测和挂载失败解除输入门。原`opening_frozen`增加失效介质下仅复制RAM展示、不取得租约及同次显示固定检查。`device_sleep_long_timer`仍覆盖四小时定时复核不使连续性自动过期；未知共享中断、PMU失败等仍保守回退。

诊断观察新增`epub.open.feedback`（早期反馈准备/扫描，可嵌套原书准备）、`Reader wake feedback`（唤醒到反馈回执）及睡眠退出事件汇总。与请求到正文、首次NEXT分别报告；不把Flash加载页提前当成完整冷开耗时下降。最终对照只使用正式1.0.7与最终候选，资料在忽略的`build-dev/open-feedback-final/`；真实TF/睡眠/面板测试继续暂停，由用户安排。

`exit_checkpoint`使用真实TXT/EPUB应用检查无正文空闲等待时的冻结收口、立即释放及挂起收口，只有当前页0槽，重开完整像素一致、退出归零；覆盖CACHE_WRITE争用和恢复正文前取消借帧。`identity_memo_host/immutable`检查后台完整扫描才发布证明、部分扫描不授予证明、最近两书调度去重、WRITE/换代失效及相同元数据不重复落盘。`identity_takeover`编译真实设备main，覆盖已知最近书仍启动完整复核、同任务接管和介质换代重试。`device_identity`检查32KiB/4KiB路径完整SHA一致、4ms软预算、外部栈、批次中途取消后游标准确及错误释放；PSA/DMA是显式替身，不能当硬件SHA测速。

SDL `cold_open_window`检查独立进程正常封面开书、续读完整帧、校验中NEXT不推进、缺包/坏包回退和原书改变拒绝；不再有非交互快照。`race-open-next:<ms>`只为输入竞争注入，不等待被正确丢弃的NEXT回执。用`PN_COLD_OPEN_EVIDENCE=<新目录>`运行`python simulator/test_cold_open_window.py <pn_sim>`保留夹具/日志/PGM；200ms模拟呈现延时不是面板计时。外置字体验证由原`resume_window`覆盖。

性能只保留最终代码对当前正式1.0.7源码f9e8c388e06ad25a90fb46b33d8259b8af670d75的对照；不把中间开发提交当基线或重复跑大矩阵。最终原始资料存忽略的`build-dev/ready-open-final/`，汇总见现有真机报告中的软件部分。开发仍须重建并保留可见窗口；硬件测试暂停，不自动刷写或连接串口。

## 推箱子的软件回归

`python tools/dev.py host`包含 `sokoban`（90关完整解法、128步撤销、溢出/无效触摸、保存/重开、实际同步后返错的提交不确定性、坏槽回退与未知版本保护）、`sokoban_render`（每次走路/推箱/撤销/重做的局部与全绘逐像素核对）、`sokoban_bank`（90关重新求最优推数）和 `blob_bounded`（1305个逐字节断写位置、完整CRC、超长槽/I/O/分配失败）。`device_management`覆盖无TF路由、64次走路不触发管理清屏、锁屏屏障与中键退出的一次原表GC16回执。面板接口在此为桩，不能作为光学验收。

`python tools/dev.py sdl`包含 `sokoban_window`：2MiB池、触摸走路/推箱/撤销、跨进程恢复、锁屏、取消确认、手动清屏与空闲不重绘。先运行 `python tools/dev.py sdl-run`，再用可见模拟器从娱乐（460,190）点推箱子（500,400），首次在选择页点开始（340,952）。方向键移动，Z/Backspace撤销，R重玩确认，P选关确认，C清屏，L锁屏，Esc或游戏中的Enter可靠保存并退出；选择页Enter开始。设备娱乐页右键进入推箱子，游戏左键/中键保存退出、右键撤销，移动主要用触摸/滑动。存档位于 `--state-dir/sokoban.a|b`，不修改数独文件。直接演示脚本 `library,tap:460:190,tap:500:400,tap:340:952`；恢复已有局时省略开始点击。

原始测量在忽略的 `build-dev/sokoban/`。板级编译仅核对链接/体积，不刷机或自动发布；固件增量应与冻结的同配置1.0.7正式构建比较。题库离线重建入口见[题库说明](../assets/sokoban/README.md)。

## 娱乐与数独的软件回归

开书长标题由`loading`覆盖：逐行居中、36/32px与三行上限、可选作者重排、完整封面像素、内置缺图退路、超长中文/无空格英文/中英混排、元数据UTF8截半及845B文件名。`PN_LOADING_PREVIEW=<已存在目录>`额外导出loading-4..7的长标题PGM；仅显示副本裁切，原名/元数据保持。

2026-10-25游戏页紧凑布局：棋盘(36,144)，68px单格；常用工具两排（撤销/擦除/笔记、检查/提示/新局），坐标中心分别(134/342/550,802/874)；数字键3×3，中心(134/342/550,960/1052/1144)，196×84px。顶部(588,72)重玩，(80,72)返回。检查/提示不进入菜单，原题与填写同时显示，用字重/大小和填写下划线区分，无切换标签。新局和重玩保留清空确认。

`device_management`覆盖64次填写跨越旧区域/整屏维护阈值而不触发GC16，保存失败不退出，成功退出一次完整原表GC16；这是驱动桩回执，不是面板残影测试。`sudoku_render`仍逐像素比较局部与整绘，另检查填写标记；`sudoku_window`按新坐标覆盖恢复、取消与直接检查/提示。开书静态页由`loading`验证封面原生像素、缺图/长标题和phase兼容调用的静态一致，`cover`/`cover_work`覆盖新preview接口的常驻/PNCV、分配失败和缺失回退。

题库入门分级v2复跑：`python -m unittest discover -s assets/sudoku -p test_sudoku_bank.py`及`python assets/sudoku/verify_delivery.py`（Python需Pillow）。检查全部1200题产品分档、独立精确覆盖唯一解、每步证明与二进制往返；简单档还核对44–50已知数、纯裸单、开局至少8个入口及每行/列/宫至少3个已知数。`python assets/sudoku/sudoku_bank.py --per-tier 300 --seed 20261024 --workers 4`重新筛选已有题库/检查点，再补齐，不自动接受旧简单题。

`sudoku_window`另构造旧schema1棋局和题库归属不匹配的schema2棋局，验证跨进程恢复的填写、笔记、撤销、题面和用时不丢；明确新开简单题后才清旧已玩位图，按新档位选题。升级后继续旧简单局不会自动变容易，需要选择新棋局。

`resume_turns`覆盖续读后继续越过两张持久帧、分步NEXT与扫描重叠，`product_open_loop`从实际设备邮箱投递点击和右键NEXT，`epub_long_chapter`将分步路径与同步流式基线逐帧比较。真机卡页需采集固件构建号及`Reader action=`、`Reader result=`、`Reader NEXT waiting`日志；主机通过不能作为该故障已修复的结论。

`sudoku`验证无周期动画、存档与故障恢复；`sudoku_render`逐像素比较变化格更新与完整重画，覆盖选格、填数、笔记、撤销、连续事件累计和显示失败失效。`sudoku_window`静置32秒跨过原动画与自动保存期限，确认只有一张成功游戏帧。`device_management`检查重复输入不呈现、成功自动保存不重画和失败锁屏屏障。`ui.sudoku.render`记录绘制阶段，不包含面板扫描，不能称为真机输入响应耗时。

`python tools/dev.py host`包含`sudoku`（全部1200题唯一解、题库循环、存档恢复、撤销满载、同步失败/分配失败及坏档保护）与`device_management`（产品路由无TF、输入、锁屏保存屏障、恢复）。`python tools/dev.py sdl`包含`entertainment_window`、`sudoku_window`，覆盖分类、空书库、2MiB预算、跨进程笔记/撤销恢复、锁屏计时、确认取消、拖出取消和15轮进出归零。自动回归用dummy，开发仍须重建并保留可见窗口。

可见模拟器从书库点“娱乐”（460,190）再点数独（100,400）；初次选择难度后点“开始游戏”（340,1040），之后自动恢复存档。游戏操作均有直接触摸入口。PC方向键选格，1–9填数，N笔记，Z撤销，Backspace擦除，Enter保存后退出到娱乐页，Esc返回，L锁屏/唤醒；设备左键返回、中键保存后直接退出、右键下格，开始/换局使用触摸按钮。提交失败不能用中键绕过保存屏障。

首次演示脚本`library,tap:460:190,tap:100:400,tap:340:1040,wait:1000`；恢复演示省略开始按钮点击。`window-close`正常保存后关闭，`back`/`quit`均为Esc只返回上层。主机存档位于`--state-dir/sudoku.a/b`，设备位于内部`/data/progress`，不放TF缓存目录。固件静态占用用`python tools/dev.py firmware-board`的size及实际bin长度评估；不以PC池、PNG大小或CI配置替代真机水位。原始本机测量保留忽略的`build-dev/sudoku/`。

## 整书索引的软件回归

连续编译回归：`index_replay`用独立出版物的原4事件分页逐页比较连续任务的文字/图片节点、全部位置、度量和坐标，并在接续期间注入取消与失卡；`index_large_resume`生成原创单章100万字，检查中断续建与一次完成的全部页边界/首屏、池归零及工作步数，防旧窗口占满额度后的逐页小块解析退化。正常主机入口包含两项。

大书对照沿用下述`test_local_book_index.py`，默认480章×6000字；`--chapters 1 --chars 2880000 --resume-after 3000`测试同等总字数的超大单章。`PN_INDEX_PROFILE=1`将阶段累计/最大小步和次数写入日志及报告`phases`，与关闭诊断的多轮耗时分开报告；阶段存在嵌套，不求和。基线必须是改动前冻结的独立程序，同一轮完整逻辑页表与首屏逐字节比较，不能只比较页数。

`page_index_log`检查新链首次创建及v2续写的逐阶段中断、同步失败、4KiB读后取消/17B短读、未确认尾部、跨块历史/同键覆盖、篡改旧块并重算CRC、低空间、缺文件/缺可信根重建；新增缓存按变化槽追加，合并另覆盖逐阶段中断、两次同步失败、短读/取消、篡改源及重算暂存CRC拒绝、备份恢复、分配失败、1600次覆盖/重复压实和未完成链接近1024块上限；128KiB测试池不变。可选块目录分配失败时继续查询，并比较目录与回扫的seek次数。这些是文件端口故障检查，不是FAT物理掉电结果。

观察器支持PNPL v3：Python独立验证块CRC、链SHA与内部根，再导出规范化v2逻辑表，逐字节对照全部边界及完成游标；存储文件SHA另列，不能把不同存储格式称作原始文件相同。`--resume-after 3000`追加未显示候选中断/重开检查；`--migrate-baseline`在旧程序已建立的同一份缓存上调用新程序真实空闲维护入口，另外检查压实后的完整表与画面一致，不混入冷建统计；将`--chapters 480`改为`960`可观察书长翻倍的IO增长。完整字形覆盖对照可用仓库`assets/fonts/HarmonyOS_Sans_SC_Regular.ttf`；默认压力文本的4096个字均在该字体中。

`index_replay`将原4事件小步作为逐页oracle，核对混排/图片/实体/长段落的节点、源位置、事件序号、度量与段落坐标，并检查取消、坏源尾部和失卡。索引可单独调用32事件小步，普通翻页预算不变；TF专项另覆盖17字节摘要短读、读后取消重试，以及载入后篡改且重算CRC仍不得经合并变成可信表。

性能观察器`pn_index_bench`随host构建生成。在同一Linux/Docker环境可运行`python3 tools/test_local_book_index.py --binary build-host/pn_index_bench --font assets/fonts/read-pico-ui.ttf --output build-dev/index-bench`：默认生成原创480章、288万字压力书；可传`--book`换成实际EPUB、`--font`换实际字体、`--baseline`指定独立冻结的旧观察器。工具复制原件到临时目录，独立冷建，要求整表及首屏SHA、总页数一致、原件SHA保持、池归零。记录核心耗时、逻辑读写/seek、路径检查及小步数，均非设备TF/面板/产品让出耗时；正式性能对照应先以单独Release目录仅构建`pn_index_bench`，关闭sanitizer，不能将ASan与Release时间混比。

诊断固件的`Whole-book index batches/steps/work_us/max_batch_us`汇总开书等待模式的批次工作，和既有`elapsed_ms`配合区分批次内外成本；批次外还含UI/推屏/输入职责，不能全称睡眠。后台索引另使用`idle.index`标签。此处只提供观测点，不代表本轮已构建刷入或测得真机结果。

`book_index`覆盖TXT超过512项的连续编译、256页提交后重开续建、EPUB跨资源、完成表复用、预览取消及确认排版的新旧变体，核对首屏完整像素与释放归零。`font_cache`检查大字宽工作集的度量一致、重复轮廓加载下降及申请失败保留小表；`index_cursor`检查小/满窗口游标查询成本不随边界数线性增长、满窗口256页连续候选、持久游标保留、未提交窗口淘汰后的游标回退和拒绝陈旧完成提交；`device_index_background`/`device_index_wait`使用真实产品控制层检查先正文/先索引顺序。`settings_ui`检查开关持久与忙时拒写，SDL的`book_index_window`检查可取消等待、暖开复用及后台顺序。运行入口仍为`python tools/dev.py host`与`sdl`。`page_index_tf`另以内部512KiB/TF64MiB复现大小文件保留阈值混用，并检查内部仅64KiB时拒写、重开保留已提交表；不删除用户数据。软件回归不代表TF实际掉电、面板响应或真实提速验收。

## 退出阅读的软件回归

`recent`检查已验证快照通知、未变化/写失败/读失败/失效介质及独立读回一致；`recent_idle`在真实TXT/EPUB中检查保存失败/取消不通知、重试成功与释放归零。`device_reading_stats`用真实产品关闭路径核对快照交接后首页不重读日志、媒体代次改变拒用；`device_management`与`shelf_view`分别核对首页缓存键和完整像素的分钟内一致、周余数进位及分钟边界失效。GC16退出扫描、统计同步和保存屏障保留；软件检查不代表退出耗时或面板实测，新批固件构建刷入另按用户确认执行。

## 外部背景专项回归

主机paper用独立生成的原生4位灰度PNG核对全部像素、左右边缘、缩略分页/缓存、缺失源、错误摘要、坏缓存、分配失败，以及TXT/EPUB首次NEXT/PREV、失败回执、挂起恢复和跨进程式重新打开；style覆盖旧schema与新256B schema8往返。device_refresh检查静态灰底不累计、第四次大面积灰阶迁移、未对齐/行位图一致与失败不提交。网页资源测试独立解码全部PNG行预测，检查封面/锁屏/背景的尺寸、灰阶、透明度与失败不上传原图。

可见模拟器新增tools/run_sim.ps1 -BackgroundDir <工作区目录>，缓存写Docker用户卷的独立backgrounds目录；主机也可用PN_BACKGROUND_DIR/PN_BACKGROUND_CACHE指定私有样本和缓存。原书、字体、图片保持只读。窗口/主机计时不能当面板残影或TF性能实测；新批正式构建发布仍需确认。

## Windows 环境

旧熊猫姿态仅保留诊断专用演示：先回首页，经既有`device_test.py serve`会话执行`python tools/device_test.py cmd "loading 0"`，另可发送`loading 1`、`loading 2`、`loading 3`，中间键返回。只投递给产品owner，不在控制台线程推屏，不开书或改进度；正式镜像不提供该设备入口。真实开书已改为静态封面卡，不触发这些姿态或动画扫描；不要用旧演示代表当前开书行为。

当前仓库 `D:\dev\windssea\pico_nano`，需要真实 Python3、Docker Desktop Linux 引擎；主机 Node 用于工具/安装器测试。Windows 的 python/python3 可能是应用商店别名，先用 `py -0p` 查解释器，本机已验证入口：

```powershell
cd D:\dev\windssea\pico_nano
py -V:Astral/CPython3.12.13 tools/dev.py docs
```

下文通用 `python` 在本机可以替换为 `py -V:Astral/CPython3.12.13`，其他电脑选择自己的实际解释器。开发脚本统一UTF-8输出，不修改全局Python配置。

Docker先确认Linux服务可连接；本机可用context为desktop-linux，仅按实际情况选择当前shell，不持久改变用户配置：

```powershell
docker context ls
docker --context desktop-linux version
$env:DOCKER_CONTEXT='desktop-linux'
```

[tools/dev.py](../tools/dev.py) 固定 IDF 镜像为 `espressif/idf:v6.1@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`；SDL镜像由 [tools/docker](../tools/docker) 构建为 `pico-nano-sdl:dev`。参考仓库只读，不把依赖/波形源整理进自有代码。

离线中式黄历的固定MIT源码及派生历表见`LICENSES/calendar-manifest.json`；`node tools/generate_chinese_calendar.cjs --check`复跑生成物和逐日年度摘要，不联网。主机`chinese_calendar`用同源C查询对照1970..2100全部47,847日，包含闰月、干支、节日和完整宜忌；这不代替设备时间来源、面板显示和真实睡眠验收。

## 日常命令和产物

2026-10-21开发配置使用SDK USB/WiFi共存（`CONFIG_ESP_PHY_ENABLE_USB=y`），不再手动切换USB数据pad；OTA的TLS使用`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`将工作内存放入PSRAM，保留完整证书校验。已有`build-*/sdkconfig`不会自动继承修改后的defaults，复跑相应构建前只同步这两项及Kconfig互斥的内存分配选项，保留板级/CI时序隔离；源码编译门会拒绝旧的USB关闭或TLS内部内存配置。诊断构建不替代正式板级/CI构建；本批用户已另行授权1.0.2正式构建发布及远程OTA测试。

阅读统计检查为主机`reading_stats`、`device_reading_stats`和`bookmark_lock`：前者覆盖门控/跨日/校时/换书归档/重启/坏档保留，第二项用真实产品main和TXT/EPUB验证首屏不加载统计、成功翻页、失败扫描、重绘与存档恢复；硬件接口为替身。SDL新增`reading_stats_window`，通过真实菜单/锁屏/加载延迟旅程与跨进程文件快照核对正文停留、翻页和持久恢复。窗口使用主机时间，执行时间及端口延迟都不是TF、面板或睡眠功耗实测；正式板级/CI和实机诊断按本批授权分开进行。

tools/test_local_system_startup.py的--staged-prepare使用产品同源的源/排版两个阶段；--sources-only在源准备后立即点击，检查0次空闲布局、1次前台布局，完整书/字体身份已就绪。--interrupt-layout --baseline-warm --baseline <独立旧观察器>在实际首屏布局完成时注入新输入取消，再点击；要求旧对象丢弃/新对象保留，点击阶段旧书SHA一次/新零次、完整三帧一致。--expect-layout用于完整候选交接，--paint-unshown继续演练原8节点正文预绘。普通PC仍重查外部修改；固定文件port仅模拟设备READ排他约束。local-samples的原准备矩阵改为分阶段，另加local-idle-source-handoff全量源后点击；中断版本对照需指定独立冻结观察器，不能把同一程序切换策略称为旧固件。hot_host/immutable另在首字度量期间及布局完成后取消，检查对象/菜单缓存保留、失败不提交进度、原帧重试及TTL/USB归还；这些是软件行为，不是TF或面板计时。

`tools/test_local_identity_scratch.py --binary build-host/test_device_identity --output build-dev/local-identity-scratch`复制并完整校验用户`testBook`与`testFont`的全部文件，每份分别在64KiB与1B池预算下比较自管DMA工作区和显式关闭能力后的原借用策略，输出明确标为同程序策略对照。可用`--baseline <独立旧产品观察器>`改为独立版本对照；不将切换同一程序的策略冒称旧固件。该工具已加入`local-samples`。观察真实设备适配器的逻辑fread次数、字节及池分配，PSA/DMA接口为替身，PSA替身暂存整个输入的主机堆在池账本之外；不是TF事务、设备总内存或耗时证据。MOBI/PDF仅参加摘要，不表示支持阅读；原件SHA必须保持。`device_identity`另检查192KiB池中150KiB界面缓存不因多余摘要缓冲被回收，关闭能力的原策略作为对照，并保留内部余量、PSRAM栈与失败退路。

`tools/test_local_menu_probe.py --binary build-host/device_menu_probe`在全新一次性Docker中复制全部用户书籍，逐款把四款用户字体副本装到固定系统字库路径，驱动真实`app_main`首页/设置/传书/书库路由。每款记录冷进入和四轮往返的stdio读量、seek/stat、字体创建与字宽/光栅轮廓加载、池峰值及17张完整4bpp帧；可用`--baseline <旧输出目录>`逐字节对照。最近阅读预览由三本真实文件的元数据种入，不代表阅读位置/内容身份验收；硬件接口为替身，单调时钟记录的是主机经过时间，不能当TF/面板耗时。已加入`local-samples`；`font_cache`另检查五个不同字号的常用字符反复交替后不再加载轮廓，以及原回收/低预算/故障退路。

同一入口加`--cancel-system`在首次完整字库/菜单缓存就绪后，向真实产品`system_font_ready`注入已取消的准备范围，断言退出后仍保留源/UI/池占用和READ租约；随后的同路径重试不得打开字体、读字节或新建引擎，继续核对原17张菜单帧。`local-samples`分别运行普通及取消两组，后者输出`build-dev/local-menu-cancel`。`font_identity`另查公开打开接口在入口/源验证中取消及替换请求，`device_management`在四个检查点取消，保留原失卡、坏字体及显式交接检查。这些是软件回归，不是人为取消状态下的实机速度；旧基线必须由含同一观察探针的原产品源码独立冻结，不能用新行为改名作旧版。

`--system-failure missing|corrupt`另用每款用户字体的缺失或改坏首4B的私有副本，核对反复菜单不再读字体、末轮不创建引擎，原17帧与独立旧探针对照。第18帧是恢复正常字体后的书库，可传`--recovered-baseline <普通菜单输出>`与其`16-library.raw`直接比较；逐字体交替检查显式资源释放和媒体代次变化后的重试，再检查租约配额/写者占用、一次IO错误和OOM不阻止后续加载。恢复副本只在一次性容器端口夹具中持WRITE租约写入，不是上传事务验收；重挂同一份私有书库时明确保留菜单预览种子，不把它当真实阅读记录。`local-samples`增加独立`local-menu-missing`/`local-menu-corrupt`两组；每次须全新容器，不能在同一容器连续复用固定`/sdcard`和`/data`目录。

`epub_cover_page`检查独页封面竖图/横图/极长图/方图的原图比例、双向居中与实际像素边界，另检正文夹图/配文/多图/徽标及坏图占位。`tools/test_local_cover_fit.py --capture build-host/epub_app_capture`用用户全部EPUB和实际霞鹜文楷逐本捕获前三页；可传`--baseline <修改前捕获器>`逐完整帧、页脚和源位置对照。该入口已加入`local-samples`，原件只读，窗口与主机结果不代替面板验收。

0.6.15的封面重绑检查为`cover_resident`，以1/2/13个公开边界样本对照原缓存路径、随机子集/环形换序、继续阅读重复、来源变化、取消及USB；不替代实际书籍。`tools/test_local_cover_resident.py --binary build-host/cover_resident_validate`扫描全部用户EPUB/TXT副本，每13本一组，比较41次完整帧并统计真实PNCV文件读取。`tools/test_local_cover_priority.py --binary build-host/cover_priority_validate --rebind-before-write`在尚未落盘时换序、返回首页再回书库，对照最终PNCV完整字节与唯一写入次数。两者已纳入`local-samples`，原件SHA须保持；端口读取计数和主机画面不是实卡或面板计时。

0.6.14的`device_async`覆盖真实输入线程对UP占槽的两次空触点和终点投递；`tools/test_local_device_shelf.py --binary build-host/device_shelf_validate`用用户整库副本驱动真实产品翻页路由，覆盖三列/四列/列表、只有DOWN/UP的快速滑动、略斜横滑、普通空报告、垂直/多点取消及首末页。固定`/sdcard`路径仅在全新一次性Docker中创建，脚本不接设备或映射TF；已纳入默认Docker `local-samples`，原生运行须改用该容器入口。SDK替身与窗口不能代替实际触控验收。

0.6.13追加七项`device_sleep_*`检查及`tools/test_local_lock.py --binary build-host/device_lock_validate`（已接入`local-samples`），用用户全部EPUB/TXT与四款字体的副本驱动真实产品锁屏/唤醒路由，每组检查三次锁解锁、完整像素/位置、旧电源事件清理、恢复扫描失败后原位重试及关闭池归零。睡眠适配器的GPIO/PMU/SDK接口为替身；可见模拟器只用于观察锁页/正文，不模拟实际低功耗或硬件唤醒，不能把两者代替真机验收。

0.6.12追加`text_inspect`、`shelf_gesture`及`shelf_gesture_window`专项；`tools/test_local_text_inspect.py --binary build-host/test_text_inspect`只读核查用户三本TXT的一遍读取、独立逐字符检查点与Python完整SHA，`tools/test_local_shelf.py --sim build-sim-sdl-idf/pn_sim`核查用户全书库三/四列、左右滑动、封面冷暖完整字节与菜单往返；两者已纳入`local-samples`。这些文件读取/CPU计数不代替真实TF、触控和面板计时。

`text`增加缓存内/跨块的UTF8最短形式、代理区和Unicode上下界，使用1～17字节及256字节短读检查字符/范围与损坏时输出保持。`text_inspect`另记录仅摘要和已知身份后仅探测的CPU时间，仍用独立逐字符检查点对照；比较构建须注明优化/消毒器，不能把主机时间当设备时间。`tools/test_local_text_decode.py --sim build-sim-sdl-idf/pn_sim [--baseline <旧窗口程序>]`在三本用户TXT×四字体副本上比较打开、两次向后和一次向前的48张正文帧/源位置、反向页像素及关闭池归零；已加入`local-samples`。窗口退出会先保存回书库，脚本明确排除最后的书库帧。

语义持久缓存的同源检查为`semantic_snapshot`（事件/结构校验）、`semantic_file`（真实文件/配额/租约）、`semantic_disk`（真实关闭重开、短章/长段、整书SHA、完整帧/位置、损坏/空间不足/2MiB及同路径换内容），窗口旅程为`semantic_cache_window`（两进程、SHA保留、命中不再正文建模、损坏重建和工具菜单抑制）。缓存目录是内部状态root下`semantic`，不在TF上传目录；这些是软件检查，不是面板首显/稳定计时。

`semantic_steps`观察真实文件端口每次最多一次4KiB写入，并把flush/fsync/close/rename分成不同调用；覆盖暂停续写、部分写后放弃、写入/同步/替换失败和低空间/介质失效时旧包保持。`semantic_disk`另检查实际阅读器的新输入、重绘、热挂起及关闭优先释放暂存，安静后可重试；BUSY不能当作已存。底层系统调用的真实耗时不由这些软件检查证明。

`semantic_snapshot`新增真实章节分槽跨会话加载、读/CRC/解码各边界取消、32个加载/解码分配点单次故障、旧单槽兼容和坏尾退路；逐有效字段核对语义，不把C结构padding当作事件内容。`semantic_file`同时覆盖同书多章共享原配额及每步4KiB读取，`source_cache`逐字节核对512/1024/2048/4096供数窗、跨界、大读取、回收和错误。

`semantic_snapshot`另固定分步前版本1载荷摘要，逐准备阶段检查取消后重试、旧包未被提前交付、pin/回收及前台热复放、存储忙/端口身份变化、低内存、跨4096字节字符串和介质变化；完整事件复放与原源对照。`resource`检查基路径/片段/合并路径取消仍是`PN_CANCELLED`且输出不变，坏UTF-8仍拒绝。细阶段计时为`epub.snapshot.count/alloc/zero/header/records/crc/header-crc/offer`，设备需诊断配置，主机需Timing；这些不代替真实面板和最长系统调用测量。

`xhtml_pass`将可重置冷解析与原双遍通用sink逐事件、样式、位置和全章统计对照，观察XML次数和章节供数字节；覆盖超过128KiB的流式章、后置样式隐藏无效资源、多个前置head、小页完成仍拒绝坏尾部/CRC、隐藏/重复锚点及输出保持、稀疏检查点旧值、内存回退、取消和失卡。XML供数是软件计数，不是TF物理事务；仍须可见窗口与真实面板验收。

`xhtml_compat`用内存片段检查词法边界、分块、声明/CDATA/实体、截断根、输入错误/取消、逐分配失败及嵌套XML诊断。综合入口追加`tools/test_local_compat.py`，只从用户原书提取实际失败章，独立构造修正副本并对照1/17/4096字节短读、整本事件摘要、实际字体全帧/后续分页位置、受限池、v1/v2缓存重开及坏CRC/缺失结束根不出半帧；结果在`build-dev/local-compat/`。正常版本1包继续可用，成功容错的章单独写版本2，不能把其接管暂存当落盘。首次异常源多一遍检查，主机结果不代表设备响应或全格式兼容。

`cover_steps`用真实文件观察封面缓存每次最多4KiB写入，逐正常阶段中断后验证原像素重试不再解码；另覆盖取消暂停、低余量、来源变化、部分写后失卡、写入/同步/替换失败、模拟FAT不可覆盖的备份读取/回滚和零租约。`cover_window`检查真实窗口循环的冷提取/重开缓存像素一致、细步骤、键盘输入期间取消/重试及退出。CRC分块与系统调用计数不是时间保证；真实FAT掉电、最大同步/关闭耗时和面板响应另验。

POSIX的`cover_window`持久化场景等待三条实际`cover.cache-clean`回执，再由SDL信号处理转成正常QUIT；测试显式启用其信号处理，要求退出成功与池归零。30秒仅为卡死看门狗，不把1500ms脚本等待当作全部fsync完成上限；原同步/清理次数、三包落盘、缓存重开整帧及中断暂存检查保持。非POSIX沿原脚本退出，不以主机文件系统时序推导设备指标。

| 命令 | 产物/行为 | 证明什么 |
| --- | --- | --- |
| `python tools/dev.py docs` | 检查项目Markdown链接/代码块、分区与vendor/字体摘要 | 文档引用/复用来源有效 |
| `python tools/dev.py host` | build-host；同源C、ASan/UBSan、真实文件/HTTP、SDK-stub及Python/Node工具测试 | 主机软件行为，非真实无线/面板 |
| `python tools/dev.py sim` | build-sim-idf、build-sim/artifacts | headless PGM与调度场景 |
| `python tools/dev.py sdl` | build-sim-sdl-idf；窗口程序及dummy交互回归 | SDL自动旅程，不自动打开可见窗口 |
| `python tools/dev.py sdl-run` | build-sim-sdl-run，Release；启动可见窗口 | 人工观察同源界面，非设备性能 |
| `python tools/dev.py firmware-ci` | build-ci-s3，独立sdkconfig.ci | CI/保守时序交叉编译与size |
| `python tools/dev.py firmware-board` | build-board，独立sdkconfig.defaults | 已核对Zbit120MHz板级编译与size |
| `python tools/dev.py firmware-diag` | build-diag，sdkconfig.defaults叠加sdkconfig.diag，版本带`-diag` | 串口测试控制台诊断镜像；禁止打包发布 |
| `python tools/dev.py factory-data` | build-dev/factory-data.bin、factory-wallpaper.bin | 生成首装空镜像；不写设备，不用于默认升级 |
| `python tools/dev.py installer` | 计划测试+build-dev/installer-site | 生成本地站点；不发布、不连接设备 |
| `python tools/dev.py local-samples` | build-dev/local-samples/report.json | 本机私人书/字体独立验证，不作为公共CI依赖 |

命令日志在 `build-dev/logs/`。相同构建目录串行，不并发启动两个Ninja；执行未结束时读现有会话/日志，不因观察超时反复重启任务。`--native`可使用本地安装工具，仍要记录实际工具链及依赖版本。

设备路由测试使用固定`/sdcard`、`/data`作为硬件替身的存储根。Docker默认在一次性容器里提供；GitHub临时Linux runner在CTest前以`sudo install -d -o "$(id -u)" -g "$(id -g)" /sdcard /data`建立当前runner可写目录，测试本身以普通用户运行，不跳过断言。原生运行须使用专用、无实际设备挂载的隔离环境，不能把产品TF或已有数据目录交给这些夹具。

SDKCONFIG按环境隔离：CI与板级都有各自build目录/sdkconfig，不能拷贝CI配置覆盖板级或把本机生成sdkconfig提交成基线。120MHz只适用于核对器件。当前无线适配要求 `CONFIG_ESP_PHY_ENABLE_USB=y`；检查defaults及实际build配置，不据缓存旧构建宣布新源码已编译。

## 开发时保持可见模拟器

其他电脑首次拉取源码，不需要私人测试书/完整字库也可运行原创演示库：先`python tools/dev.py sdl-run`构建，再`powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_sim.ps1 -Demo`。依赖Docker Desktop Linux与WSLg；Windows可按上文用`py -V:Astral/CPython3.12.13`代替python。演示输入在忽略的build-dev/demo-library，状态独立保存到Docker卷的/data/demo-samples；不扫描私人目录。已有窗口会保留，重建后先`docker stop pico-sim`再启动。默认不加-Demo仍使用本机tests/testBook与tests/testFont，只操作独立副本。

缓存基础专项为`cache_file`、`page_index`、`wallpaper_cache`、`paper`、`reader_app`和`epub_app`。覆盖缺目录/坏主包有效备份、每个提交步骤中断、失卡后不清理重挂载路径、原生像素，以及索引满盘时真实进度成功保存、正常挂起/恢复；取消不是缓存MISS。主机为真实临时文件/SDK替身，不代表FAT掉电或面板验收。

TF增量索引随上述可见启动器默认启用，用户/演示缓存分别保存在Docker卷`/data/user-samples/reading`与`/data/demo-samples/reading`。可传`-NoReadingCache`切回内部窗口作策略对照，旧缓存/用户位置保留；手动运行程序时用`PN_READING_CACHE=<可重建目录>`启用，空值使用内部退路。不要将模拟器目录映射为实际设备TF卷或原件目录。

主机`page_index_tf`检查1800个边界跨512项RAM窗口、版本/布局隔离、整表SHA、重算CRC伪造、摘要读后取消续建、可信根先提交而TF未发布时中断、孤立大暂存恢复、命名空间GC/根配额与失卡归还。`reader`另检查解码检查点不能充当上一页页首。SDL`tf_index_window`用原创TXT、文字EPUB和图片EPUB，比较冷/暖、PREV/NEXT、坏/缺/不可写缓存共48对完整原生帧；这是同程序后端策略对照，非旧版或真机耗时。GitHub源码CI新增独立SDL任务，完整窗口回归可在其他电脑复跑。

`tools/test_local_tf_index.py --capture build-host/epub_app_capture --baseline <独立旧版capture> --output build-dev/local-tf-index`逐本检查私人EPUB前三页；可用`--books`和`--font`指定本地样本，原件只读。候选通过`PN_EPUB_INDEX_IDLE`显式推进真实延期写入，再跨进程重开，比较旧版/候选冷页/候选暖页的完整像素、位置、原池上限与关闭归零。报告中的host_total_ns包含开书/翻页/落盘/关闭及工具开销，不能称物理开书耗时；没有baseline时仅作同程序冷/暖对照，--limit明确为子集。

```powershell
python tools/dev.py sdl-run
powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_sim.ps1
```

第一条构建并启动；第二条可直接调用窗口启动脚本。源码重建后，先确认并停止旧 `pico-sim` 再重新启动加载新二进制；脚本会保留已运行窗口，不自动将它替换成新源码。关闭窗口也会结束对应容器。依赖WSLg；`docker logs pico-sim`查看输出，每次真实呈现PGM在 `build-dev/sim-frames/`，状态默认保存在 `pico-sim-data` Docker卷。

默认窗口显示阅读首页，B书库、H首页、C继续；书库方向键聚焦、Enter打开，PageUp/Down翻库页。管理页数字1/2/3是左返回/中首页/右确认；阅读S排版、P预览、Enter应用/Esc取消，L锁页预览与返回。脚本输入可用 `-Script 'settings,back,fonts,back,wallpaper'`；无quit时回放后仍保持可操作。

局部按下反馈开发演练可传`tools/run_sim.ps1 -Feedback`（`PN_SIM_FEEDBACK=1`）。脚本`press:X:Y,wait:MS,move:X:Y,release:X:Y,focus-lost`覆盖保持/滑出/失焦，反馈PGM以`feedback-`前缀保存，不改变普通帧序号；`PN_SIM_CLOCK`只为自动对照固定状态带。设备资格开关`CONFIG_PN_UI_FEEDBACK`默认关闭；可见PC反馈不证明FOLLOW DU光学效果或真实扫描时间，资格镜像仍按本批构建授权与面板验证流程处理。

反馈定稿演练可用`press:100:600,move:100:850,release:100:850,wait:750`，日志`settle=1`及`feedback-*-settle.pgm`是空闲维护回执；`PN_SIM_SETTLE_FAIL_ONCE=1`仅用于软件失败/重试对照。320ms安静等待与1秒重试不代表面板耗时，设备仍须验证新输入到达定稿前/中的延迟与灰区恢复。

字体连续列表软件演练传`tools/run_sim.ps1 -Follow -FontDir <工作区内至少7款字体的目录>`（`PN_SIM_FOLLOW=1`），例如`-Script 'library,fonts,press:400:820,move:400:520,wait:180,move:400:350,release:400:350,wait:1500'`。管理首页键脚本为`key-home`，`home`仅代表字母H，不能用来验证管理三键路由。`PN_SIM_FOLLOW_TRACE=1`记录阶段/offset/回执/代次；`PN_SIM_FOLLOW_CAPTURE=1`配合FRAME_DIR保存实际窗口像素，`PN_SIM_FOLLOW_FAIL_ONCE=1`仅模拟首次逐相失败。有效字体、跨页惯性、框外保持、故障恢复、失焦/导航和低预算由`font_scroll_window`检查；不能据窗口速度宣称墨水屏流畅，实际SDK构建及光学资格仍待完成。

设备列表已绑定独立`CONFIG_PN_UI_FOLLOW`资格入口，默认关闭，可与`CONFIG_PN_UI_FEEDBACK`独立组合；两套sdkconfig基线未启用。`device_list`及`device_list_no_feedback`编译真实main资格路径和原波形，以SDK替身覆盖VCOM门、归一、原相位、后缓冲、灰阶定稿、失败/欠载和实际UI退出；`device_list_boot_*`另检启动失败不注册/不推屏。`device_toc`和`device_toc_no_feedback`另用原创54项EPUB验证真实目录绑定、半程退出、排空确认及失败GC恢复；`toc_motion`检查跨页/部分行命中、分组/失败跳转、尾部当前项和2MiB退路。它们不是实际IDF交叉编译或光学验收。新批构建发布按当批用户授权执行，资格镜像和默认发布镜像明确区分。

目录连续滚动在书库打开EPUB后用同一`-Follow`入口，脚本例如`library,enter,toc,press:400:900,move:400:500,wait:180,move:400:250,release:400:250,wait:1800`。`toc_scroll_window`使用原创48章/54项目录，检查真实事件、跨页、框外保持、灰阶定稿、原位置重开、按下反馈、失焦/失败恢复和低预算。独立`--book`窗口目前保留旧分页；主产品与`--library`才绑定该资格路径。

书架焦点演练使用`library,wait:800,toc-down,toc-down,toc-up`，其中`toc-down/up`只是SDL方向键脚本别名，不要求已打开目录。`shelf_focus`逐字节对照三/四列、四种风格、列表/首页、真实灰阶缩略图及缺图卡；`shelf_focus_window`验证真实键盘事件、焦点往返像素、外置补字、选书阅读及返回。`device_management`另检实际设备底图/三键、取消/失败/代次和状态带恢复；SDK仍为替身。这些测试不证明面板光学流畅度。

本机综合测试与可见模拟器默认扫描用户提供的 `tests/testBook/` 和 `tests/testFont/`，新增文件在下次运行时自动纳入；不生成或混入示例书。`tools/run_sim.ps1`先用`tools/prepare_local_samples.py`复制到被忽略的`build-dev/user-samples/<随机ID>/`，记录原件SHA，阅读器删除/覆盖只作用于副本。原件目录已被Git忽略，私人书籍和完整字库不得加入公共fixture、固件或发布包。修改源码后重建并重启窗口，新增资源也须重启以加载新副本。

可见启动器将这批新副本另挂到固定`/samples`：程序使用`/samples/books`、`/samples/fonts`，宿主副本仍每次独立。用户样本状态/缓存位于`/data/user-samples/{state,covers,wallpaper}`，与旧生成样本分开，旧记录保留。这样重启不会只因随机副本目录变化而丢封面缓存/字体路径；未更改书籍和mtime时复用已有缓存，新增/变化文件仍须正常校验或重新提取。首次从旧`/work/build-dev/...`路径切换需要生成一次新键。

开发时可传`tools/run_sim.ps1 -CoverCacheNamespace qa-20261015`使用独立`/data/user-samples/covers-qa-20261015`目录，观察封面冷提取；同命名空间后续运行可检查重开缓存。名称只接受小写字母/数字和连字符，最长48字符；不删除默认缓存或改变阅读状态目录。仍须先停止旧窗口再启动；主机文件系统和窗口耗时不能代替实卡/面板测量。

`python tools/dev.py local-samples`按实际EPUB/TXT依次跑解压/出版物与封面缓存、字体设置、mimetype兼容/损坏拒绝、内存、跨进程阅读缓存、不支持格式和逐章正文解析；某个样本矩阵失败仍完成其余矩阵，最终非零退出，不能把开书成功当全书兼容。报告分别在`build-dev/local-samples/`、`local-font-settings/`、`local-mimetype/`、`local-memory/`、`local-cache/`、`local-unsupported/`、`local-chapters/`。

该入口也运行`tools/test_local_publication.py`，用同目录真实EPUB比较同步/分步的全部元数据与spine关联，逐步取消/恢复/关闭、介质换代，并在元信息最小的一本逐个注入分配失败；副本另检container/OPF坏CRC、截断和内部DTD拒绝。结果在`build-dev/local-publication/`；`xml`单元检查交错池、4KiB推进、源尾错误和分配清理，不替代真实输入到面板回执计时。

`tools/test_local_zip_job.py --binary build-host/zip_job_validate`也已接入综合入口，使用真实EPUB独立副本比较原同步打开与可恢复打开的全部目录/资源字节，检查每步读取配额、1/17字节短读、预取消及读取后取消继续、失效源/IO、目录快照不准入；最小样本覆盖每步放弃和逐分配失败，退出池须归零。报告在`build-dev/local-zip-job/`，FD源接口读量不是TF物理事务或整机内存。`zip_job_oracle`复跑原Python zipfile解包oracle；`zip_job`另用原始ZIP边界片段覆盖空归档/最大尾注、超过128KiB目录及大量扩展字段、ZIP64拒绝、同哈希不同名及重复名，不能作为真实书籍体验替代。

`tools/test_local_cover_priority.py`用真实EPUB/TXT每六本分组，比对同步与产品分步路径的完整缩略图/元数据/PNCV字节，检查所有可见结论完成前没有缓存write/fsync、继续阅读重复槽不重复解析、写入暂存放弃后不重解码。报告`build-dev/local-cover-priority/`；此处“可见完成”是可绘制状态，实际显示回执仍由设备/SDL控制层确认。

该工具可传`--baseline <旧cover_priority_validate>`比较同源副本的新旧缓存完整字节，记录文件端口调用与源缓存峰值；`--deny-source-cache`压低父池可选分配余量，并用独立可回收前台对象检查零驱逐、直接读取退路及像素/元数据一致。可选`--compression-probe`保留历史zlib/RLE尺寸探针，默认不执行，也不改变产品格式。端口计数/主机编码大小不能证明实卡提速。`crc32`单元以独立逐位oracle/标准向量核查任意初值与分块，`semantic_snapshot`另在每个载荷CRC块后注入取消，检查无事件/统计发布、无源XML回退及随后精确命中。

`tools/test_local_semantic_probe.py`从真实EPUB生成实际语义缓存，核查跨章新文件缺失为0载荷读取、旧单槽不匹配仅152B包头读取，且不申请完整包、命中完整包字节不变、正文事件一致、坏头/坏载荷回退、低内存/取消/失效介质清理；额外覆盖用户刀剑样本的容错章。可选`--baseline <旧epub_app_capture>`比较同书/字体/状态的首帧、跨章帧和缓存包。报告`build-dev/local-semantic-probe/`，文件端口计数不等于TF总线实际读量或响应时间。

字体设置逐本核查管理全局默认、四款实际字体的单书覆盖、全局变化保留覆盖、恢复继承、取消预览及显式系统字体；副本数字前缀固定行次序，避免把Python码点顺序误当产品拼音顺序。字体探针以整组正文码点并集检查五种字号和灰阶/二值绘制，单字体缺字数量与引擎状态分开，探针通过不等于全字覆盖；当前用户样本未含完整鸿蒙系统字库，不能据此关闭其删除保护/全字补字的实际资源验收。

内存矩阵在1/2/4/6MiB池检查普通字体，在6MiB追加全部用户字体，逐阶段保留pool begin/end/peak/attempts、九类内存及六类缓存账本；每类peak不能相加当同时峰值。除prepare结果也记录实际开书/翻页/返回状态，拒绝损坏/IO误报，单场景超时保留日志并继续其余矩阵。关闭used/live必须归零；低预算明确OOM属于退路结果，不能计为成功阅读，报告另列reading_opened、controlled_oom以及默认6MiB是否全部打开。主机的SDK heap/stack不可用；设备诊断`CONFIG_PN_PROFILE`另记录内部/PSRAM空闲、最低水位、最大连续块及任务栈，仍须采集真机日志。

跨进程缓存核对整帧、完整书源SHA、语义包和分页索引、坏包重建；百分比跳转后先翻页往返清除会话临时返回入口，避免把页脚状态差异误判为正文缓存差异。TXT不使用EPUB语义包，单独记录`.pni`，不能将`model-load`缺席当TXT无缓存。显式指定其他私人清单时可传给`tools/test_local_samples.py --manifest`，例如：

```json
{"files":[{"kind":"book","path":"tests/本机样本.epub"},{"kind":"font","path":"tests/本机字体.TTF"}]}
```

原件只读，仅复制到隔离目录。`tools/run_sim.ps1`可指定 `-Books/-Font/-FontDir/-WallpaperDir` 和 `-Timing`，容器输入须在工作区内；未指定壁纸目录时用空目录。模拟器时区Asia/Shanghai，电量为演示值，锁页预览不模拟PMU功耗。预算上限6MiB，低预算可降，不可扩大掩盖设备故障。已有公共小型边界fixture仅用于单元回归，不能替代用户资源的综合测试和可见验收。

`root_navigation_queue_window`使用既有公开边界文本，分别对四根页和反馈开/关注入扫描中第二次底栏接触，核对最终整帧与串行点击一致，并验证旧内容区触点仍拒绝。`PN_ROOT_RACE_ARTIFACTS`可保留全部日志与画面；未设置时，首次最终帧差异会先复制产物到模拟器构建目录内的独立`artifacts/root-queue-failure-*`目录并打印路径，容器退出后仍保留。第二点击及密集导航像素测试均固定容量显示输入，完整帧/次数/pending断言保留；`race-root:延迟毫秒:起点X:起点Y:终点X:终点Y`在延迟后释放起点并向终点投递独立点击，仅最终松手确认脚本。`PN_SIM_CAPTURE_CANCELLED=1`仅供竞争诊断捕获已成功显示、随后因新输入取消准备的帧；默认仍跳过这类开发截图，不改变成功显示回执。模拟等待与公开边界样本不代替真实用户整库/字体及面板验收。

`root_navigation_coalesce_window`另查四根页、反馈开/关的密集独立点击：`race-roots:延迟毫秒:首目标:后续目标1:后续目标2:末目标`使用0～3底栏编号，首目标扫描中投递另三次完整接触，核对最终完整帧与串行末目标相同，积压三目标只准备最后一个。`race-focus:延迟毫秒:起点X:起点Y:移动X:移动Y`注入失焦后保持左键的移动，最后才松手，不能把同一持有变成新点击。`PN_ROOT_COALESCE_ARTIFACTS`保留原始日志/帧；扫描中注入须在日志确认pending，不能只凭脚本字符串断言竞态发生。空闲检查须独立配置`--cover-cache`启用索引，冷缓存、释放后等待才要求建索引，不能对未初始化模块要求成功日志。共享`async`及实际`device_management`另查批次上限、后续取消、优先电源项、同一持有、多点/滑入和最终空样本的释放。

`tools/test_local_root_navigation.py --sim build-sim-sdl-idf/pn_sim`已接`local-samples`，重新复制全部用户书籍/四款字体，逐字体比较密集点击和失焦持有的最终整帧、关闭池归零及释放后空闲索引。可选`--baseline`只接受含同样竞争脚本入口的独立冻结程序；原件SHA保持，记录旧/新导航尝试次数，不能把次数直接换算为面板扫描减少或真实速度。开发工具中的700ms等待只制造积压窗口，不是产品延迟或面板实测。

## 真机串口自动测试（诊断镜像）

`python tools/dev.py firmware-diag`构建`build-diag`，在板级默认上开启`CONFIG_PN_TEST_CONSOLE`与`CONFIG_PN_PROFILE`，应用版本为`<版本>-diag`，设置页可见；产品/发布构建关闭性能计时且不编译该控制台，钩子为空。指令语法与合成时间线在`components/pn_ui/pn_test_console.c`，设备与模拟器共用；设备在输入任务的触摸采样层用合成采样替代一次读数，因此三键、阅读手势和底栏导航仍走产品原代码，但不覆盖触控芯片I2C/固件本身。截屏在显示任务两job之间读取epdiy已显示参考（后缓冲），回传board布局4bpp并逐行CRC，`known=0`表示参考未知。每个显示job完成后上报`PNT 0 display kind start end ms status`，时间为设备esp_timer毫秒。

| 指令（每行`<seq> <verb> ...`） | 作用 |
| --- | --- |
| `ping` / `version` / `status` | 连通、版本与ELF摘要、view/输入原子计数（不遍历堆） |
| `heap` | 堆最大块/最低水位与任务栈；遍历堆会屏蔽中断，只在两显示job之间执行，扫描中执行曾在真机引起欠载 |
| `tap X Y [ms]`、`long X Y [ms]`、`swipe X0 Y0 X1 Y1 [ms]` | 合成触点；Y可到1399以覆盖屏下键区 |
| `key left\|middle\|right [long]` | 屏下三键（中心80/240/400，Y=1340） |
| `power` | 合成电源短按；锁屏后唤醒仍须实体键 |
| `shot` | 回传当前已显示画面 |

刷写只写应用分区：`build-dev/device-venv/Scripts/python.exe -m esptool --chip esp32s3 -p COM3 write-flash 0x20000 build-diag/pico_nano.bin`（venv由`py -m venv build-dev/device-venv`并安装esptool创建）。打开串口会触发一次USB复位，所以`tools/device_test.py serve --port COM3`常驻持有端口并记录`build-dev/device-test/<时间>/serial.log`与`results.jsonl`；其他终端用`tools/device_test.py cmd "tap 85 1150" --brief`、`shot out.png`、`tail`。手势返回首个/最后显示job相对合成首采样的时间及期间产品计时日志。`sdkconfig.diag`另开`CONFIG_PN_DIAG_NO_AUTO_LOCK`（不自动锁屏，电源键锁屏保留）与`CONFIG_PN_DIAG_NO_LOCK_SLEEP`（锁屏只轮询不浅睡，`power`指令也可唤醒，硬件睡眠/唤醒须在非诊断镜像另测）。控制台启动后常持禁浅睡锁（产品默认空闲自动浅睡会让USB串口停止响应），并把产品日志改为整行非阻塞写入驱动、电脑不读时直接丢弃（驱动VFS逐字符50ms超时会拖住产品任务），所以诊断镜像不能用于功耗测量。进入传书无线或USB传书会断开串口；串口日志本身改变时序，结果是诊断镜像上的近似值，不等于产品镜像、光学稳定或功耗验收。模拟器脚本可用`pnt:<指令>`复用同一语法。

## EPUB 与性能诊断

`tools/test_local_foreground_open.py --binary build-host/device_menu_probe --scenarios --output build-dev/local-foreground-open`已纳入`local-samples`，在全新一次性Docker中复制全部用户书籍/字体，逐款系统字体驱动真实`start_reader`与`tick_opening`的普通EPUB冷开。要求加载后尚无可阅读app、逐轮完成且唯一正文回执，记录完整OPEN/NEXT/PREVIOUS三帧及池；八种场景检查目录阶段返回、摘要后返回、协作取消、锁屏准备、失卡、USB租约归还、首帧显示失败和错误内容身份，失败不得写最近阅读。默认只验证当前行为并保留画面，不能称新旧像素一致；传`--baseline <独立旧产品观察器>`才逐字节比较完整原始帧。测试端口显式报告已挂载的TF，只访问副本，不模拟实际卡吞吐/面板/低功耗；普通文件port保留外部改写检查。`--limit`只作明确子集，实际整库和低预算/设备峰值应分别报告。

`tools/test_local_zip_job.py --binary build-host/zip_job_validate --scan-prepare --output build-dev/local-zip-scan-job`在用户全部EPUB副本上，先以4096B/8读配额准备中央目录，再顺序喂入原书并完整finish；与原同步归档逐目录、逐资源字节对照，覆盖目录1/17字节短读、取消/重试、可选快照拒绝、失效源/IO及最小样本每步放弃/分配失败。配额只指目录job，后续顺序供数/收尾单独执行，不能当整个开书耗时保证。默认完整ZIP job模式保持，两模式均加入`local-samples`；`zip_scan_job_oracle`和`zip_job`另核对独立zipfile解包、坏头/重叠/同哈希/重复名状态。`test_local_open_steps.py --baseline-partial --baseline <独立旧探针>`可比较两版均先完成64KiB摘要再点击，明确区别于旧冷开基线。

`tools/test_local_open_steps.py --binary build-host/font_system_startup_probe --output build-dev/local-open-steps`检查用户全部EPUB×四款字体×正文/补字：真实首页任务先完成64KiB摘要，注入一次取消，再点击接续原任务，核对剩余摘要字节、完整身份、OPEN/NEXT/PREVIOUS三张原始帧及关闭池归零。可传`--baseline <独立冻结旧探针>`作跨版本冷开对照；默认明确标为同程序冷开/部分准备策略对照。全部原件SHA保持；书SHA启动次数为零不等于点击没有剩余摘要工作，报告单列`click.work_bytes`。`tools/test_local_identity_jobs.py --binary build-host/test_device_identity --output build-dev/local-identity-jobs`另对全部用户书籍和字体比较原同步与分步设备适配器的摘要、逻辑fread数及每步DMA归还；可传独立`--baseline`，PSA/DMA是替身且PSA主机堆不计入池，不能用池峰称设备总RAM。这两个命令已加入`local-samples`。真实`device_menu_probe --prepare-books`区分唯一正文回执和部分EPUB接管前的一次加载反馈，旧报告`one_present`仍兼容。

本机综合入口也运行`tools/test_local_middle_key.py --sim build-sim-sdl-idf/pn_sim`与`tools/test_local_font_samples.py --binary build-host/font_samples_validate`，使用用户原书/字库的独立副本，检查阅读中间键、排版保存和关闭池归零，以及字体列表首帧不打开外置小样、每轮最多一个与合并呈现。字体工具可加`--baseline <旧探针>`比较完整最终像素；未给旧工具时只报告本版行为及重绘一致，不当作跨版本像素对照。结果位于`build-dev/local-middle-key/`和`local-font-samples/`。公共`catalog_mount`只将挂载根`mkdir`替换为IDF6.1 FatFS的`EINVAL`，其余千本索引、真实文件、重开/取消/分配失败与原测试相同，不能代替实卡掉电或响应计时。

`font_startup`编译真实TXT/EPUB入口，检查外置补字引用沿同一个完整校验句柄进入排版键，该补字文件仅一次完整摘要；另覆盖缺失、同大小替换、主字体损坏、取消、失卡和显示失败的释放。`tools/test_local_fallback_startup.py --binary build-host/font_startup_probe`已接入`local-samples`，使用全部用户EPUB/TXT和TTF的独立副本，记录真实文件端口字节、完整摘要次数、画面摘要与池峰；可选`--baseline`对照修改前冻结探针。它不测物理TF或面板时间。阅读线的整帧SDL对照固定`PN_SIM_CLOCK=12:34`，保留全部像素判据，避免跨分钟HUD变化误报取消失败。

`font_startup_probe`另记录冷开至首屏的原书stdio打开/读取/字节/定位。`tools/test_local_fallback_startup.py --binary <新探针> --baseline <旧探针> --compare-io`比较两个均只做一次完整摘要的冻结探针，检查字体读取保持、原书读取/定位不增及全部原始帧逐字节一致；不带该选项时保留原双扫描补字优化的基线判据。原件仍只读、使用独立书/字体和状态副本，软件文件计数不代替TF/面板计时。

`identity_reuse_host/immutable`核查同路径、媒体、代次、READ资格、取消、旧句柄关闭及PC同大小外部改写；`font_identity_immutable`另确认已加载系统源不能覆盖所选引用的不匹配摘要/长度。`tools/test_local_system_startup.py --binary build-host/font_system_startup_probe --expect-reuse --output build-dev/local-system-startup`逐本逐字体检查系统源已加载后，选择该同路径为正文或补字的打开/NEXT/PREVIOUS。`font_system_startup_probe`仅将文件port切至`PN_TEST_IMMUTABLE_FILE`来复现设备源不可被外部写入的约束，其余阅读代码与主机同源；这不是SDK/TF/面板实测。传`--baseline <独立冻结旧探针>`逐完整帧、原书读取及后续翻页IO对照，要求恰好少读完整字体一次、峰值不增。普通`font_startup_probe`不加`--expect-reuse`另跑同一矩阵，要求保留完整字体核验；两组已接入`local-samples`。`--limit`只供开发子集，报告明确标记，不能当整库通过。

同一工具加`--warm-prepare --expect-reuse`检查未显示准备：记录空闲完整SHA/读取与0次状态写，点击阶段书/字体SHA及打开均0，再核对原始OPEN/NEXT/PREVIOUS全帧。没有`--baseline`时明确以同一个当前程序的普通冷开作策略对照；可另传独立冻结旧冷探针，不能把同程序冷/暖结果称作跨版本实测。`hot_host/immutable`增加未显示所有权、错误身份/路径/格式/策略、首次显示失败无记录、准备/接管取消、24个早期分配故障、菜单cache owner保护、低预算、已有root、TTL不反复重建、USB和媒体失效，显式启动字体的完整身份也在空闲补齐。测试已有公开边界样本只用于契约，用户整库矩阵仍使用原件副本。

继续加`--baseline-warm --expect-layout --baseline <独立旧探针>`可比较两版同样提前开书时的首屏交接：新版本空闲阶段真实FIRST/JUMP排版恰一次，点击不再次prepare；独立旧程序仍在点击排版。探针记录idle/click布局次数，`PN_FONT_STARTUP_PROFILE=1`输出阶段时间与池；这是主机测量，不能作TF或面板时间。`hot_host/immutable`检查重复准备不重排、布局完成后输入取消、非首屏保存后整帧恢复、显式回书首丢弃候选，以及接管后失卡不得呈现；读取核心prepared回执不代表显示成功。

再加`--paint-unshown`推进首屏正文分步绘制，独立旧基线保持仅提前排版。探针完整统计空闲绘制的原书/字体读取与FT_Load_Glyph（含RENDER标记），点击命中计数及三张全帧；不把绘制过程排除在idle计数之外。图片页保持前台渲染，预算不足允许停止可选准备并原路绘制。`hot_host/immutable`检查每步至多8节点、部分像素/取消回退、12个正文阶段分配故障后的完整帧回退、混合字号与两种阅读线、非首屏恢复/回书首及池策略/pin归还。

`test_local_menu_probe.py --prepare-books`在原17帧之后追加真实`app_main`的TXT/EPUB准备、1500ms门槛、菜单无额外字体IO、锁屏释放及一次正常OPEN显示，四实际字体均退出池/租约归零；不把内存中构造的已识别Continue选择当作已有阅读记录。此模式每款字体清空一次性容器的私有`/data/progress`，避免上一款实际OPEN写出的记录改变下一款冷菜单；原件和默认记录不动。`prepare_window`另检查真实SDL进程重开的首页空闲准备、冷开最终全帧及设置释放，固定窗口等待后必须看到真实ready/正文绘制完成/take日志，不以经过时间代替准备成功。这些入口已纳入`local-samples`/窗口回归，不替代面板计时。

像素测试可用`PN_SIM_STORAGE`固定传书页/关于页的容量显示，与`PN_SIM_CLOCK`同为显式显示输入；磁盘写入空间准入仍用真实statvfs，不受该值影响。密集导航专项固定此输入并保留全帧对照，以免测试自身或并行产物改变可用量；普通可见启动不设置它。前次未保留的差异帧仍不能据此追溯断言其原因。

新版探针另输出首屏后NEXT/PREVIOUS的`turn_io`，与冷开计数分开；`--compare-io`在前后探针均提供该行时也检查翻页原书IO不增及字体读取保持。旧探针缺少此行时不推断其翻页IO。

实体中间键在阅读中短按回主页、长按600ms打开阅读工具栏；排版/字体页短按保存成功才回原书。SDL用数字2或`key-home`模拟短按，用F10或`key-middle-long`模拟长按；此脚本别名不代替真实按住/释放资格测试。

```powershell
python tools/dev.py epub-sample --archive "build-dev/sim-library/piconano/books/05-章节目录与书签.epub" --all-resources
python tools/dev.py profile --archive "build-dev/sim-library/piconano/books/05-章节目录与书签.epub" --rounds 30
```

profile每次生成 `build-dev/profile/<随机ID>/` 隔离状态与summary.json；保留源书SHA、轮数、预算、后端、冷/热条件、失败计数和各阶段p50/p95/max。普通软件显示回调不测面板，OS缓存通常未受控。相同书/字体/布局/工具链对照，不能修改原书后宣称同样本提速。

真实设备控制器的主机诊断可在独立一次性容器运行`tools/test_local_foreground_open.py --binary build-host/device_menu_probe --output build-dev/foreground-profile --prepare-first`，并给容器传入`PN_OPEN_PROFILE=1`。夹具复制用户样本，比较加载期间提前准备后的完整首屏/翻页；`--baseline`可指定另一独立二进制做逐字节对照。`--scenarios`另覆盖取消、锁屏、换卡、USB、显示失败、错身份和多书切换。容器需全新`/sdcard`、`/data`，不要对用户挂载目录运行夹具。原`profile`入口仍调用同步开书，不能代替这个真实产品job入口。

设备诊断关注`epub.open.request/loading/first-frame`及`full-wall/trusted-wall/wait/max-step`，具体分步为`directory/identity/zip-finish/publication/metadata/state/fonts/sources`。2026-10-19本地诊断将`content.sha-read/update`和`storage.file-read/seek`改为阶段边界`diag_io`累计：calls为调用数、total_us为非重叠同类调用之和、max_us为最长单次、non_ok保留异常；空任务不输出，阶段不再输出重复堆/栈快照，按需在显示job间用heap命令采样。SHA读取回调可能含ZIP feed，不能把整个回调都当读卡时间；`idle.*`观察后台预算。墙钟、最长单步与嵌套子阶段不相加，IO汇总窗口可能包含随后的空闲校验；`diag_io`不能直接当逐次延迟分布。开书IO快照是本job累计值，汇总工具的`max_io`取最大值而非相加；截断/拼接记录排除，指定输出时旁存`.invalid.json`并报告损失数量，不能当完整日志。发布配置不输出新增高频诊断。首屏完整校验与图像质量保持，软件显示回执不是面板光学稳定；输入/显示使用esp_timer，不能将ESP日志前缀的绝对毫秒与其混算。

主机 `-Timing`/profile记录分类账本及阶段局部同时峰值；分类峰值不相加，嵌套耗时不重复相加。设备可单独用 `CONFIG_PN_PROFILE=y` 做诊断，但不自动刷机，默认发布配置关闭。诊断日志有测量成本；记录是否开启，池外堆和任务栈必须设备独立测量。

普通固件保留开书/SHA/页准备、显示reason、dirty/convert/power/scan/remember、queue/wait/receipt和关键保存分段；用来定位慢开书/翻页，不能只看总Present。热页软件和真实光学/电源性能分别比较，参考成绩及局限见[架构末节](ARCHITECTURE.md#性能利用与阅读优化)。

## 验证与开发收口

0.9 OTA源码的主机专项为 `ctest --test-dir build-host -R '^update' --output-on-failure`（`update`同源事务、`update_boot`真实启动端口的SDK替身、`update_ui`同源设置页）；默认`python tools/dev.py host`包含它们。`test_package_ota.py`用真实密码库覆盖逐字节篡改及错误密钥，`test_sync_gitee_release.py`另验证签名OTA附件与三个ZIP内的应用一致。固定IDF镜像已带cryptography48.0.1，原生Python需先 `python -m pip install -r tools/requirements-ota.txt`；未安装时不把签名测试跳过当验证成功。设备PSA/TLS及真正双槽/断电不能由主机替身替代。

国内分发工具独立复跑为 `python -m unittest discover -s tests/tools -p test_sync_gitee_release.py -v` 和 `python tools/sync_gitee_release.py --tag v0.8.0`；后者默认仅验证现有包，不连接设备、不上传。真实同步、匿名下载核验和 GitHub Actions 凭据配置见[安装与发布](INSTALLATION.md#gitee-国内分发)。这类分发/文档变更无需重新构建固件或重启产品模拟器。

`gfx_spans`以修改前逐点距离/混合为独立像素oracle，覆盖6000组裁剪/奇偶行距/灰阶/分数描边，另含实际屏幕大小卡片、退化半径与极端坐标；比较包括相邻半字节、行尾和边界保护。`settings_ui`检查兼容网络名快照更新不再提交相同画面。它们验证绘制等价与重复工作消除，不代表ESP32、TF或面板响应时间。

`settings_font`检查同池引擎移交、连续12次设置热打开零字体创建、冷/热完整像素一致、分配/取消/错池/呈现失败、压力回收、被违规填满的归还槽和最终零资源；`device_management`实际设备控制层另检查设置借用期间源交接和取消仍保留原页。`tools/test_local_settings_font.py --binary build-host/test_settings_font`已接入`local-samples`，逐款扫描用户TTF，独立复制后作为TF界面源检查，原件SHA保持，结果默认`build-dev/local-settings-font/`。这验证借用/缓存和像素，不把任意测试字体当真实鸿蒙系统资源验收。

`device_refresh`直接编译实际设备统计源码，检查全部65536组前后字节、八位计数的256种掩码、8192组混合像素字、阈值前后各一点、1/2/3字节未对齐及页脚/空/小脏区，并与冻结逐像素规则比较完整方案。无转白资格时传NULL目标抓意外读取。管理、列表/目录及无反馈资格入口仍用同一设备函数回归；这些是软件判断/内存访问验证，不代替面板扫描、残影或TF计时。

2026-10-14完整刷入批次仅改安装器/打包与文档，固件基于已发布0.6.6阅读修复源码。独立工作区从该基线完成两套IDF6.1构建/size、factory-data与22项Node/5项包Python；板级应用5026336B、20.11%槽余。既有149主机/32SDL为前批固件证据，本批没有重跑或重建产品可见界面。包、站点和离线ZIP逐文件SHA一致不代表设备迁移成功，实际备份恢复、刷入、复位与启动另验。

文档重组只跑文档/引用/归档校验及受影响工具测试，不为可逆文档修改重建固件。源码修改选择有意义的相关主机回归；阶段完成再全量host、SDL自动旅程和可见窗口。需要发行时按用户授权跑两套SDK构建/size，随后按[安装与发布](INSTALLATION.md)核对包与站点。没有刷入的构建不能称真机修复。

基线时间要写清：已发布 `0.6.5+766a137` 经146CTest/10Python/5Node、31SDL、17项安装器计划测试及两套固件构建。本页计数是历史证据，下一批以实际输出为准，不把“计数一致”当验收条件。

逐项真机验证方案、样本与长期矩阵见[开发指南](DEVELOPMENT_PLAN.md)。错误/失败/取消、资源关闭used/live=0、像素/位置一致性都需保留，不能把失败样本从统计中删掉。

USB界面可见演练程序为SDL构建的`pn_usb_preview`：空格切换准备/等待/连接/停止/失败/完成六态。它复用产品绘制，不枚举USB或读写TF。`device_usb`验证100次独占交接，`device_usb_board`用API替身编译真实板级出口，验证活动回调、延期写、PHY失败及重试；均不能替代真实电脑与设备测试。
0.6.6本批软件门：149项CTest、10项Python、5项网页与32项SDL通过；目录说明可见检查后修复为四行独立绘制，并补跑USB/版本/管理窗口三项。板级接口替身和真实USB、共享渲染和面板光学分别记录。


`root_navigation_window`以现有公开文本边界样本检查无松手已显示新页、跨页持有/滑出、内容滑入和孤立松手拒绝、四页往返完整像素、子页/失焦、两种反馈配置，以及扫描中释放后下一点击可用。`tap`检查共享按下所有权、多点/无效/两个空样本重置，`device_management`实际main检查底栏资格和过期空样本只释放不选择。脚本`close-window`发送真实窗口关闭（`quit`仍是返回键）；`race-nav:延迟毫秒:X:Y`在呈现中注入松手，仅用于软件竞争验证。它们不代替用户书库旅程、可见窗口或面板测速。


0.6.16的`cover_resident`边界与同名用户整库工具追加冷bind无封面文件读取/来源stat、逐轮最多一张PNCV、取消与USB读写隔离、最终像素及元数据对照；常驻封面命中仍复核来源。大JPEG窗口测试等待包含800ms解码安静期，不能把等待窗口长度当主机提取耗时或TF面板性能。`device_async`追加64次无恢复间隔的代次确认，睡眠适配覆盖11种通知/电平/共享中断/错误场景，用户书籍锁屏每书每字体16轮并检查扫描期间新旧电源事件；设备书库旅程追加每布局64次主页锁解锁。硬件均为端口替身，不能据此宣称真实短按成功率或待机功耗已验收。

2026-10-20首屏输入交接检查：`async`验证唯一暂存、确认前不交付/不取消、失败/换view/紧急导航丢弃、晚释放竞态；`device_async`以真实输入线程及SDK替身覆盖旧触点隔离、新点按去重、跨首屏确认的手势和左/右键语义。`recent_idle`用同源TXT/EPUB确认坏首屏不记历史、首屏/NEXT无近期记录写入、空闲延时/合并、IO失败限频/取消重试及退出保存屏障；`reader_recent`继续核对挂起/关闭前的真实记录。设备诊断的`Reader input ready t=`是首屏与模型均提交后的时刻，预备阶段的status body=1只代表可采集新手势，不能单独当首屏已确认；配合实际display回执与Reader action计数验证一次输入一次翻页。诊断合成触点仍不替代实体触控、光学或真实睡眠验收。

2026-10-20资源精简回归：`style`使用真实A/B日志逐个读取旧schema7 ID1..8，核对只有移除款的背景解析为无、其余非默认字段一致且日志序号/载荷不变，越界及坏版本继续拒绝。`builtin_art/reading_art`检查仅两个封面、ID0/4/7顺序、原生图/状态栏行带及旧ID白底，保留原图SHA。`style_ui`覆盖持久ID命令与画廊空位拒绝；SDL `reading_background_window`追加两种格式全部旧ID跨进程恢复、非默认排版及原日志字节不变。主机图片与冻结版本对照不是板级固件体积、面板或冷开耗时实测，正式构建发布须本批新确认。


## 页表单遍完整校验回归

`page_index_log`保留旧表/追加链、逐提交边界、短读、取消、坏CRC、重算CRC伪造、坏前缀、可信根及128KiB低内存检查。新增真实fread计数约束：重开186144B夹具时最终207996B，包含完整链与末尾窗口，不允许恢复“记录扫描+SHA整表再读”；冻结e127d62为392556B。比较的是端口字节量，不是TF耗时。`source_blocks_app_host/immutable`、`resume_app`、`bootstrap_entry_host/immutable`、`xhtml_pass`与SDL开书/续读/TF索引专项覆盖依赖安全重放和完整帧退路。

本批仅跑相关host10项、新I/O1项、SDL5项及两套构建；不冒充完整系统回归。真机仅最终有效单轮对现有基线，选书必须核对`Reader selected ... name=`；脚本串行执行，不能仅凭首次显示回执或截图位置认定目标已打开。缓存命中差异、预绘状态与扫描/补画时间须一并报告。
