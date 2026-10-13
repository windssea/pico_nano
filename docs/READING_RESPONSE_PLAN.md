# 阅读响应与清残影开发计划

2026-10-13，用户要求阅读官方与第三方资料、统一清残影入口、校正全刷判断、优化翻页/内存/反馈，按阶段实施并在结束后提交推送。固定硬件仍为ESP32-S3/8MiB PSRAM/16MiB Flash/684×1216、16灰阶；不启动子agent，保持可见模拟器，不修改vendor波形或PMU标定。

## 已确认的问题

- 排版的`gl_before_clear`与系统刷新级别同时生效，分别在阅读应用和设备显示层计数；中间动画帧、管理更新和重绘可消耗设备计数。
- 原翻页效果插入两次GL16，再显示最终页；额外申请两帧约832KiB，与墨水屏每次约0.6秒显示耗时叠加。
- 已有语义缓存减少章节重排，完整开书仍受完整SHA与ZIP装配影响。真实普通页约483ms正文绘制、131ms旋转转换、464ms扫描，需分别改善。
- 周期GC16与未知参考/手动深清原先都先clear再白场绘制；区分用途才能减少重复清屏，不能把普通差分更新称为深清。

## 实施顺序与出口

| 阶段 | 工作 | 验证出口 |
| --- | --- | --- |
| R1 资料与基线 | 逐项记录可见源码、提交、硬件差别及可借鉴机制；未公开源码与网页动画不作真机证据 | 六个参考项目都有来源与限制，记录现有双配置与计数触发 |
| R2 统一刷新 | 仅保留系统“刷新与屏幕”；历史排版记录兼容读取但不控制刷新；成功实际翻页计数，管理/预览/失败/取消/预排不混入 | TXT/EPUB、全局6/14/24周期、重绘/取消/失败、手动深清/未知参考/欠载回归 |
| R3 翻页与内存 | 一次最终灰阶呈现取代三次面板过渡；减少临时帧；改进字形混合和旋转转换的访问局部性，像素不变 | 精确像素对照、边界/裁切/失败、主机与设备栈门、原6MiB池、低预算回收 |
| R4 操作反馈 | 明确输入已接受/正在处理/保存失败，保留异步输入合并与保存屏障；减少反馈帧额外分配和无效刷新 | 连点/取消/保存失败可继续操作，反馈不误提交位置，不新增动画多刷 |
| R5 验证与提交 | 更新文档、完整同源/SDL/双SDK；可见窗口观察；设备连通时只更新应用区对照；最后commit/push | 分清软件、设备启动与光学验收，记录未通过门，提交与远端同步 |

## 来源审阅记录（固定快照）

| 项目 | 提交与初步证据 |
| --- | --- |
| [官方](https://github.com/MindReset/read_pico_firmware) | `6a247cc94420b85c650b32a6b2c063a360070eee`；main/display.c周期升级全像素GC16，不另插第二次刷新；手动清屏单独执行clear/from-white。 |
| [whiteos](https://github.com/446599/whiteos) | `d967659a1689d73470b6e0b11e84c23ae9926a78`；公开库主要网页/发布包/许可，README明确翻页动画为网页示意，不能证明面板速度或残影。 |
| [read-pico-reader](https://github.com/wegooo-cell/read-pico-reader) | `2566dcf768ae96c2377688a4a39dbd7aefa58308`；main/display.c区分整页与局部反馈，不将局部按压计入整页GC周期。其快速页面路径涉及黑白化和分区参考管理，不能仅靠改mode启用。 |
| [PicoReader-ifEdition](https://github.com/yiifanrecyclable/PicoReader-ifEdition) | `15476d57c219b5c74d2c694cf39e2824c927e269`；main/display.c全页切换与局部反馈分开，间隔性全像素GL16、不插入第二次刷新；深清与恢复另行处理。 |
| [RickyOS](https://github.com/chinoryunqin/RickyOS) | `ecfc977e6dd2fa93ef3a1f237ea0c0e48ad292ae`；有界字体缓存、4KiB光栅工作区；ActivityManager的1500ms静止后补灰阶入口实际关闭（graySettleAllowed=false），注释提到面板失配；不能作为已启用方案照搬。 |
| [Metalio-E-INK4](https://github.com/CloudZao/Metalio-E-INK4) | `b94da8d44db77d3e7a674056fb69e4f8232b7e26`；800×480/SSD1677与本板不同，仅借鉴有界字形/页预热、后台解析与输入反馈，不复用LUT或电气时序。 |

只读快照与过程记录位于忽略目录build-dev/references；不执行参考工程、不修改既有只读参考目录，不据源码声明光学效果已经验收。最终差异与阶段结果在本页及MEMORY_AND_RESPONSE、CHANGELOG更新。

## 决策与阶段结果

R1源码审阅完成，来源均固定提交；R2统一入口与逻辑周期、R3单次显示/像素优化、R4区域快照与错误反馈已实现。R5完整验证与提交结果在下文补充，不用主机截图宣称残影验收。

| 机制与参考证据 | 本项目决定 |
| --- | --- |
| [官方display.c](https://github.com/MindReset/read_pico_firmware/blob/6a247cc94420b85c650b32a6b2c063a360070eee/main/display.c)：周期升级GC16，全像素刷新与手动clear分离 | 采用既有epdiy全像素GL16/GC16；周期GC不额外白场，手动/恢复另行深清 |
| [read-pico-reader display.c](https://github.com/wegooo-cell/read-pico-reader/blob/2566dcf768ae96c2377688a4a39dbd7aefa58308/main/display.c)：局部按压不计整页周期，快路径维护分区/灰阶参考 | 按逻辑成功翻页计数，显示用途独立于输入view；管理小变化局部GL16，正文驱动所有像素 |
| [ifEdition display.c](https://github.com/yiifanrecyclable/PicoReader-ifEdition/blob/15476d57c219b5c74d2c694cf39e2824c927e269/main/display.c)：快页间隔性全像素GL16 settle、不插第二次重绘 | 本阶段正文始终全像素GL16；差分与较稀疏settle的收益列真机实验，避免先引入参考状态转换 |
| [RickyOS ActivityManager](https://github.com/chinoryunqin/RickyOS/blob/ecfc977e6dd2fa93ef3a1f237ea0c0e48ad292ae/src/activities/ActivityManager.cpp)：1500ms灰阶恢复被关闭；输入会取消空闲准备 | 保留灰阶和取消/回执屏障，先减多刷及有界内存；黑白先显再补灰阶须另做光学状态测试 |
| [Metalio book_screen](https://github.com/CloudZao/Metalio-E-INK4/blob/b94da8d44db77d3e7a674056fb69e4f8232b7e26/main/display/screen/book_screen/book_screen.cc)：有界字形预热，换参先预览，再后台建索引，busy合并输入 | 已有输入/worker/显示分工和邻页预排继续复用；章节语义复放避免重复XML；采用单个待执行导航和区域反馈，后台全书索引另排期 |
| [whiteos README](https://github.com/446599/whiteos/blob/d967659a1689d73470b6e0b11e84c23ae9926a78/README.md)：网页演示不证明实体波形/速度/残影，公开库无阅读实现源码 | 只参考展示与交互，不据网页动画重建物理动画或声明性能 |

实现：排版仅六项；PNTS旧清屏字段和PNIP旧动画位保留兼容。TXT/EPUB全局周期按成功前后翻页计数，预览/重画/失败不消耗；管理24次成功更新独立计数，物理清理成功回执重置阅读周期。系统与工具栏深清动作复用同一意图。日志给出reading/management/requested/maintenance/manual/unknown-reference/underrun原因。

翻页从三次呈现减为一次，临时两帧831744字节归零；不增加池上限。工具栏快照415872→112176字节，完整原正文仍保持不可回收。字体按一次裁切后直接混合半字节，旧舍入/二值阈值不变；684×1216旋转使用512B栈小块连续读写，避免PSRAM跨整帧跳读。无波形、PMU、字体原件或vendor修改。

阶段软件出口：18项刷新/像素/字体/真实阅读会话回归通过；14项工具栏/异步/设备路由/旋转回归通过。工具栏测试比较完整原像素、失败及分配失败恢复、显示一次和<120KiB快照计费；像素测试覆盖所有ink、灰阶/二值阈值、负裁切、奇数宽与padding；TXT/EPUB逐一验证6/14/24和重绘不计页。原完整测试的默认13页断言随旧逐书设置移除，改为全局14页行为验证。

## 仍需真机的下一阶段

1. 同本书冷开/保存位置重开各10轮，记录全书SHA、ZIP、publication、model、layout、正文、转换/scan/显示等待、保存及栈/最大连续块；比较普通、第6/14/24次与手动深清，不合并软件和面板耗时。
2. 原生16灰阶密集文字/图片/菜单交替至少100页，拍固定曝光纸屏照片评估白底/细线残影和闪屏。只有完成此门，才试差分正文+间隔settle、图像转文本额外清理；不以像素变化率猜物理残影，更不自动加第二次扫描。
3. 字形命中率、32KiB累计栈和6MiB池内/池外压力校准；完整内容身份SHA保留。未来在强内容身份与字体/样式版本键下做持久语义/分段窗口、TXT扫描合并和书库增量；当前单章超384KiB仍流式，不能承诺所有长章零重复解析。
4. 黑白先显、1500ms后补灰阶须验证输入打断、参考切换、图像/文本与8次保洁的实际成本；本阶段不启用，默认一次16灰阶显示。

## R5完成的软件结果与交付边界

完整135 CTest、7 Python、3 Node通过；收尾修改后13项设备/阅读/工具栏/取消回归通过；最终完整30 SDL通过。两套ESP-IDF v6.1构建与size通过：board 0x4a35d0、CI 0x4a2a10，6MiB应用槽约23%余量。vendor/字体/许可证摘要和文档检查通过，固定硬件上限不变。旋转函数Xtensa实际单帧576字节（entry 0x240）；累计显示任务栈余量仍需刷入后测，不从反汇编推断整机峰值。

最新Release可见窗口已重建重启；同源TXT/EPUB前后翻页、工具栏开关/手动刷新、系统刷新页与排版更多页均观察，更多页仅剩字间距。窗口保留可操作，真实原生PGM无像素修改转PNG与串口枚举留忽略目录build-dev/reading-response。Windows当前无串口枚举，因此最新固件仅构建准备，未写入设备；之前a9658f6d4版本的开书/全刷不满意反馈仍有效，不能把模拟结果说成真机改善。

同一原创481章、911963-byte EPUB、6MiB池、ASan/UBSan、同一当前驱动，在归档789f75e产品与当前产品各30轮；每轮独立保存状态，并完整SHA/恢复冷重开，所有操作成功、关闭used/live=0。主机Windows绑定文件系统和OS缓存未受控，以下是软件观测，不是TF或面板数据；jump p95没有改善，不用单个中位数替代尾延迟。

| 阶段 | 基线p50/p95（µs） | 当前p50/p95（µs） |
| --- | --- | --- |
| ZIP装配 | 202633 / 245282 | 46335 / 54582 |
| open（60样本，含两类新会话） | 299051 / 360926 | 143992 / 160537 |
| 保存位置冷重开整体 | 336883 / 407343 | 182021 / 191029 |
| 下一页准备 | 3256 / 3598 | 1542 / 1814 |
| jump重排 | 6276 / 8427 | 2736 / 8965 |
| 混合绘制 | 9660 / 18903 | 6264 / 15645 |
| 预排 | 3264 / 4378 | 1605 / 1871 |

单章语义模型增加约325KiB样本会话峰值，属于384KiB/共享1MiB内的可回收代价；禁用旧动画时才省去两帧831744字节，并不宣称所有场景常驻内存都下降。完整身份扫描仍保留。原始比较、源/镜像SHA、版本、未刷状态和失败测试的修正记录随本机manifest保留；长期物理验收按上述下一阶段执行。

## 2026-10-13 四项实机反馈后的优先级

现有设备分段显示完整SHA约5.016秒、正文光栅约383ms、扫描约471ms；下一轮按真实DMA路径和字形热度优化，不继续以PC读取调用数推断设备收益。普通阅读静态页脚保护和开书busy输入门已接入代码，见PAGE_TURN/ASYNC_READING；它们借鉴的是区域与所有权机制，不复制参考波形。

进一步只读复核上述固定源码：read-pico-reader的静态导航/灰阶条带与内容更新分离，可借鉴到页脚；Metalio book_screen保留按页位图预热及独立开书worker/反馈，下一步应测下一页字形预热（当前只有节点预排）；RickyOS ActivityManager的输入取消空闲渲染与重启准备可改善快速翻页后预热命中。其graySettleAllowed仍为false，不据注释启用黑白先显/灰阶回补。

同书热重开另需有界保留已验证句柄/章节/字体状态，低内存可回收；传输/USB/换卡先清理租约，禁止只按size/mtime复用SHA。首页不需要立即完成的封面/索引继续空闲执行，连续排版参数可以等抬手/短暂静止后一次预览，减少无用重排。所有方向分别设置软件与真实面板出口，尚未实现的方向不计完成。


## 2026-10-14 read-pico-reader重点复核与阅读线落地

用户指定重新重点分析最新main。固定只读源码为 `24363623eba4f523d12302f4fec872cb96eb809a`，对外名称已改为 kiikoread，板型同为RDP-G01-W/ESP32-S3、SDK6.1；替代旧2566dcf分析基线。源码位于忽略目录 `build-dev/references/read-pico-reader-2436362`，未运行其构建脚本或固件，未将README性能描述当成真机结果。用户另明确要求本轮同时实现阅读线，其余机制只分析。

| 关注项及源码 | 已读到的行为 | PicoNano接入判断 |
| --- | --- | --- |
| [残影刷新](https://github.com/wegooo-cell/read-pico-reader/blob/24363623eba4f523d12302f4fec872cb96eb809a/main/display.c#L359) | 整页计数和局部反馈分开；快页面差分GL16+周期全像素settle，阅读5/10/15/30页或关闭强刷。文字边缘另有自行调序波形。 | 保留PicoNano成功逻辑翻页6/14/24周期；差分正文/周期settle须另做面板对照，拒绝直接移植调序波形。 |
| [翻页](https://github.com/wegooo-cell/read-pico-reader/blob/24363623eba4f523d12302f4fec872cb96eb809a/main/apps/app_book.c#L2780) | 下一页位图在独立task中预绘制，显示与预绘制重叠，内存不足或插图时放弃；显示结束会等待预绘制完成。 | 当前仅预排节点，建议先字形预热，再做池内可回收位图预绘制；前台不得被后台join拖住，取消、字号及会话代次须覆盖。 |
| [无线传书](https://github.com/wegooo-cell/read-pico-reader/blob/24363623eba4f523d12302f4fec872cb96eb809a/components/read_pico_transfer/read_pico_transfer.c#L272) | HTTP整文件PUT，16KiB接收与8KiB stdio写缓冲；临时文件+覆盖备份、失败清理；STA关闭modem sleep，HTTP内部栈在WiFi启动前预留，输出断连reason与连续堆。 | 借鉴内部堆准入、reason诊断和缓冲测量；保留PicoNano分块SHA、持久ACK、幂等续传及统一安装事务，不能换成失败删part的整文件入口。 |
| [USB传书](https://github.com/wegooo-cell/read-pico-reader/blob/24363623eba4f523d12302f4fec872cb96eb809a/main/apps/usb_storage.c#L69) | 本机卸载FAT后TinyUSB MSC独占TF；SDMMC1bit20MHz；禁止auto_mount/format；断开、回调排空、停止后重挂，并归还Serial/JTAG PHY。 | 先落实PN_MEDIA_USB交接栅栏、所有TF消费者退出和字体切内置；重挂换epoch并重验资源，逐步测读/写/弹出/拔线/串口恢复。非本轮实现。 |
| [WiFi配置](https://github.com/wegooo-cell/read-pico-reader/blob/24363623eba4f523d12302f4fec872cb96eb809a/components/read_pico_transfer/transfer_policy.h#L65) | 设备扫描按SSID去重并保留最强RSSI，分页列表与ASCII键盘配网，NVS单blob保存；STA全信道按信号排序，30秒5次有界重连。 | PicoNano仍用内部data A/B凭据；可扩设备扫描/信号/连接原因。网络owner须能给对时和升级借用，避免未来启动多个WiFi实例。 |
| [在线升级](https://github.com/wegooo-cell/read-pico-reader/blob/24363623eba4f523d12302f4fec872cb96eb809a/main/ota_online.c#L97) | HTTPS CA校验、板型/项目/布局/版本/大小/SHA，Range重连，写非活动槽，擦写/显示共锁；主线程切槽，启动检查后确认rollback。 | 参考布局为4MiB双槽，PicoNano是6MiB双槽且当前应用约4.9MB，布局完全不能移植；目前仅Web Serial升级。新增设备OTA需自身manifest、实际旧表/bootloader回滚门和取消/断电验证。 |
| [阅读线](https://github.com/wegooo-cell/read-pico-reader/blob/24363623eba4f523d12302f4fec872cb96eb809a/main/book/book_layout.c#L310) | 关闭/虚线/点线，±8px位置，横排跟行、竖排跟列；逐步绘制，不加第二张页帧。 | 本轮自有同源实现：默认关闭，虚线/点线1px灰色，位置±8px步2，TXT/EPUB真实行定位，跳过插图行，逐书schema3、旧版默认关闭；PicoNano未支持竖排。 |
| [大型EPUB](https://github.com/wegooo-cell/read-pico-reader/blob/24363623eba4f523d12302f4fec872cb96eb809a/main/book/book_epub.c#L68) | 8192 manifest/spine/nav上限，标题/锚点块池与动态表，ZIP哈希定位，持久OPF/目录索引；整章HTML提取上限4MiB，仍整章解析/分页。缓存按路径、大小、mtime。 | 借鉴目录池化与强键持久索引，保留完整内容SHA/解析版本，不用mtime免检；PicoNano已有章字节/语义/邻页缓存，长章仍需分段模型，不能只放宽资源上限。 |
| [日期时间](https://github.com/wegooo-cell/read-pico-reader/blob/24363623eba4f523d12302f4fec872cb96eb809a/components/read_pico_transfer/read_pico_transfer.c#L600) | SNTP复用在线STA或临时连网；独立设置操作写PMU TIME_SYNC并TIME_GET核验，开机恢复settimeofday、睡前同步；分钟变化只刷新可见状态区。另有/clock客户端时间接口，当前upload页面未调用。 | PicoNano目前只在开机读已对时PMU并推算HH:MM，未接网络或手机对时。建议UTC秒存PMU、展示UTC+8，连网异步校时、手机时间作无互联网备选，失败保留旧时钟，不碰VCOM/时序寄存器。 |

建议顺序：先验证现有传输恢复并补断连reason/堆余量，随后独立网络owner与异步对时；翻页字形预热、差分/settle对照与强内容键EPUB持久模型并行排期（不代表启动子agent）；USB独占恢复和设备OTA分别过硬件门。阅读线软件落地详情见[逐书排版](TYPESETTING_SETTINGS.md)。保留PicoNano品牌、固定目录与共享安装事务，不直接采用参考的自由文件目录、NVS凭据、4MiB分区或自调波形。

复核细节：参考的OTA确认调用在硬件/显示/触摸准备后、进入首个应用界面之前，不能据此宣称已经验证首个主页或打开书。PicoNano若新增回滚门，应在首个必要界面成功呈现和关键存储自检后确认新槽。参考的配置页SNTP调用是同步等待，临时连网最多等待15秒、SNTP等待12秒；应借鉴PMU写入与读回流程并改为自有异步任务。参考STA可在离开传输页后保持联网，PicoNano当前退出会销毁无线并恢复USB；对时先选择临时连网或受控复用，避免意外改变离线功耗/USB恢复。

后续阶段已在开发代码接入STA信号/断连原因/恢复计数，见[局域网传书](LAN_TRANSFER.md)；日期设置与独立单owner短轮询SNTP流程见[日期与网络对时](DATE_TIME.md)。仅手动临时连网，不复用传书无线，不开机联网。释放无线后补偿清理耗时，写PMU并读回确认；这批尚未构建或发布，真实网络/PMU走时待刷入验证。手机时间备选、设备扫描键盘、USB MSC、设备OTA与进一步EPUB模型优化仍在后续阶段。
