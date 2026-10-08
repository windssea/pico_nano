# 小纸 Pico 阅读固件开发计划

更新基线0.0.49。当前T01–T14完成情况、用户需求覆盖和待办统一见 [PROJECT_STATUS](PROJECT_STATUS.md)。本文保留原始详细验收要求；阶段代码或主机绿色结果不能自动勾完含硬件/产品闭环的任务。

> 执行者：按任务逐项实现与验收，使用 executing-plans 工作流；未经用户另行授权不启动子 agent。本文是用户要求的长期开发路线，过程日志、临时检查点与实验记录放忽略的 docs/local，不进入产品提交。

**Goal：**在 Read Pico 现有板上交付可靠、清晰、易用的离线阅读固件，原生 TXT/EPUB/PDF，完整版本扩展 FB2/CBZ，具有封面、排版设置、快速传书和可恢复升级。

**Architecture：**复用板级驱动，新建原生 UI、异步阅读会话、统一显示 owner 和介质租约。重排文本与固定页面分开，全文内容身份与可恢复文件事务统一管理。

**Tech Stack：**ESP-IDF v6.1、C11、FreeRTOS、epdiy 裁剪 LCD fork、厂商波形、受控 TTF 与图像解码、独立 PDF 候选、HTML/JS 手机传书页面、Python 主机工具、CMake 主机测试。

## 1. 全局约束

- 目标目录 `D:/windssea/dev/pico_nano`；参考 `D:/windssea/dev/read_pico_firmware` 只读，来源基线见 README。
- 板型 RDP-G01-W / ESP32-S3 / 16 MiB flash / 8 MiB PSRAM / 684×1216 逻辑竖屏。
- ESP-IDF 必须 v6.1；不使用浮动 master/latest。
- VCOM 在 PMU，运行时只读；出厂校准独立；不写 SY7636A VCOM/VLDO/放电/延时/VCOMCTL。
- 不手改参考生成/第三方表；保留组件 SPDX、字体与解码器许可；PDF许可门先行。
- 完整UI中文“小纸 Pico”，英文/日文“Read Pico”；pico_nano只作内部代号。
- 数据可靠优先；挂载失败不自动格式化；USB与固件不能同时访问TF。
- 性能数字按 VALIDATION 指定条件测；没有真机证据不得称“已验证”。

## 2. 阶段依赖与估算

按一位熟悉 ESP-IDF 的开发者全职估算，不是日历承诺；准备硬件/引擎移植/许可采购等待另计。每阶段通过门槛才进入依赖阶段。

| 阶段 | 任务 | 工作量估计 | 独立产物 |
| --- | --- | --- | --- |
| M0 可行性 | T01/T02/T03 | 2–3人周 | 驱动与资源基线、PDF技术样机、USB与WiFi测量 |
| M1 基础可靠性 | T04/T05 | 3–4人周 | 租约、身份、持久位置、显示路由与桌面回放 |
| M2 文本内核 | T06/T07 | 3–4人周 | 字体、TXT/EPUB、增量分页与热翻页 |
| M3 产品0.1 | T08/T09 | 4–7人周 | 封面书架、字体上传管理、锁屏壁纸与AP/STA网页传书 |
| M4 Beta | T10/T11 | 3–5人周 | 原生PDF产品交互、持久续传与导入 |
| M5 完整格式与维护 | T12/T13 | 2–4人周 | FB2/CBZ、USB、升级回滚 |
| M6 发布 | T14 | 2–3人周 | 性能/稳定性验收、部署包、公开支持边界 |

总计约19–30人周，包含字体上传管理、锁屏壁纸以及共享代码桌面模拟器、自动截图和故障回放；此前18–28人周估计仅明确基础主机测试；PDF如果需更换引擎或许可证路径，额外2–6人周以上且可行性重新评估。1.0可不含未通过硬件门的USB，但不能删掉用户明确的PDF原生需求。完整版本不以开发量估算代替验收。

关键路径：T01→T02（PDF门）与T04→T05→T06→T07→T08→T09→T11；T10在T02/T05通过后落实；T12依赖阅读/图片；T13依赖存储/USB门；T14统一验收。任务可独立评审，不代表允许自动并行 agent。

## 3. 验证命令约定

长期任务中的拟建入口不代表全部已经存在。当前已建立同源工程、阅读/上传等软件测试，可执行命令以 [当前构建与运行](BUILDING.md) 为准；其余测试和产品功能按对应任务继续实现，不把命令清单当成验收结果。

环境与完整命令统一维护在 [开发与PC模拟测试](DEVELOPMENT_AND_TESTING.md)。固件CI与真机分别使用build-ci-s3/build-board及独立SDKCONFIG，避免已生成sdkconfig覆盖默认配置。主机用真实组件源与port替身；桌面模拟器使用同一UI/字体/排版源，不另写模拟产品。CTest的`-L`筛选标签、`-R`筛选名称。编译、主机、芯片仿真与真机结果分别报告。

## 4. 具体任务

### T01 驱动迁移、合法来源与测量基线

当前状态：部分：来源/工程已建立，真机基线待测。细分证据见PROJECT_STATUS。

**拟建文件：**`CMakeLists.txt`、`main/app_main.c`、`sdkconfig.ci`、`sdkconfig.defaults`、`partitions.csv`、`components/pn_port/`、`tests/host/CMakeLists.txt`、`tools/check_docs.py`、`LICENSES/`、`docs/DEPENDENCIES.md`。逐组件移植参考BSP/芯片驱动/epdiy/波形，不复制demo应用。生成或vendored表只保留来源不手改。

**输入/输出：**输入来源清单；输出 `pn_board_init()`、`pn_clock_us()`、受测 `pn_alloc()` 和板级display适配。每个分配报告pool/bytes/high-water，主机替身与IDF实现同契约。

- [ ] 建立simulator/CMakeLists.txt、窗口/灰阶图、headless截图、受控时钟/预算、文件与内部存储替身；可选QEMU smoke独立于产品BSP配置。
- [ ] 建立 CI 编译与 host CTest 入口；测试失效分配器返回NO_MEMORY且已分配资源释放。
- [ ] 移植硬件，记录引脚、VCOM读取、触摸/SD/PMU、display缓冲实际内存；编译确认IDF版本。
- [ ] 真机测GC16/GL16/DU、空闲关轨、pclk欠载恢复、首帧、触摸坐标与三键。
- [ ] 对120MHz基线做压力、冷热启动；另建保守时序实验配置比较，不修改参考仓配置。
- [ ] 验证文档与代码来源许可、flash布局、恢复刷机；产物单独评审后提交。
- [ ] 按ARCHITECTURE拆分1.875MiB assets与1MiB wallpaper，验证两份415872-byte位图、元数据与文件系统开销可容纳，不侵占位置日志/小书库。

**验收：**可启动到最小测试页，资源峰值与显示扫描baseline明确；无额外SY写入；CI和host入口可复跑。默认分区app映像与烧写地址由IDF生成。

### T02 原生PDF可行性与许可门（最高优先级）

当前状态：未完成：原生PDF技术与许可门尚未通过。细分证据见PROJECT_STATUS。

**拟建文件：**`components/pn_pdf/include/pn_pdf.h`、`pn_pdf_adapter.c`、`pn_pdf_budget.c`、`tests/host/test_pdf_budget.c`、`tests/fixtures/pdf/manifest.json`、`tools/benchmark_pdf.py`、`docs/PDF_SUPPORT.md`；实验日志 `docs/local/pdf/`。

**接口：**`pn_pdf_open(stream,budget,token)`、`pn_pdf_page_info(page)`、`pn_pdf_render(page,viewport,strip_sink,token)`、`pn_pdf_close()`。返回status、heap peak、实际过滤器错误；budget不得绕回系统无上限malloc。

- [ ] 建立40本分层PDF样本与期望页缩略图；fixture生成简单PDF并验证扫描JPEG/CJK样本授权。
- [ ] 编写堆超限、取消和损坏文件回收测试；先证失败路径不会泄漏，再适配候选引擎。
- [ ] 裁剪PDF-only引擎、交叉编译，固定commit/构建选项与许可证；测app大小，不能默认AGPL与现项目发行许可已兼容。
- [ ] 真机测CJK/扫描/混合/xref/透明/大图；记录每页输出与内存，不只开一个hello-world PDF。
- [ ] 按FORMATS的90%基准语料、3MiB堆、时间、6MiB槽余量和许可门做结论；失败给出替代引擎/硬件或范围调整证据。

**验收：**正式门通过才能把PDF产品实现列可交付；失败可继续文本内测，但Beta/1.0 PDF不算完成。此任务产物是可复跑PDF技术样机，不要求先实现整套书架。

### T03 WiFi、USB与卡写入通道基线

当前状态：部分：热点/扫码代码已装配，无线/USB实测待做。细分证据见PROJECT_STATUS。

**拟建文件：**`components/pn_transfer/pn_network.c`、`pn_usb_probe.c`、`tools/benchmark_transfer.py`、`tests/host/test_transport_state.c`、`docs/USB_SUPPORT.md`。

**接口：**`pn_network_start(AP/STA)`、`pn_network_stop(deadline)`；USB probe不读写正式用户卡，使用专门测试介质。

- [ ] 状态机测试启动失败/重复停止/切网事件，不允许停止后事件重新连接。
- [ ] AP/STA有效payload基线，测TF同步写成本、信号与手机熄屏行为。
- [ ] 核查本板GPIO19/20、USB PHY、OTG枚举、TinyUSB CDC/MSC与USB Serial/JTAG恢复刷机。
- [ ] 做独占介质读写样机，禁止应用与主机并行访问，真机拷贝后摘要一致。
- [ ] 决定USB是否进入1.0；无硬件证据保持通道未启用，公开WiFi能力。

**验收：**得到各通道实测数据与USB恢复路径，不承诺标称链路速度。

### T04 存储租约、内容身份与持久位置

当前状态：主体代码已实现，真实TF掉电/拔卡验收未完成。细分证据见PROJECT_STATUS。

历史0.0.4起实现租约、原文件SHA、A/B记录、TXT位置载荷、5次翻页/30秒同步保存策略和单个TXT书签持久操作，主机/交叉编译验证见BUILDING和PERSISTENCE；本任务的真实拔卡/掉电、内部挂载、RTOS保存屏障/产品接入尚未验收。

**拟建文件：**`components/pn_core/include/pn_types.h`、`components/pn_storage/include/pn_storage.h`、`pn_media.c`、`pn_lease.c`、`pn_journal.c`、`pn_identity.c`、`tests/host/test_media.c`、`test_identity.c`、`test_journal.c`。

**接口：**使用ARCHITECTURE中的book_id/location/token；输出storage acquire/release、content hash、A/B record load/save，CRC与sequence持久格式有版本。

- [ ] 测试同路径同大小不同内容→不同id；相同内容不同位置→同id不同path_id。
- [ ] 测试每个记录写入边界断电，坏CRC回退旧代，无任何有效代返回未恢复。
- [ ] 实现media epoch、READ/WRITE/USB_EXCLUSIVE lease，拔卡后旧lease不可提交。
- [ ] 实现非破坏挂载、首次空分区初始化与修复入口；完整进度、缓存与原书分目录。
- [ ] 测保存5页/30秒窗口、离书/锁屏屏障、NVS错误与内部journal异常；故障不可清其他书数据。

**验收：**`ctest -R "media|identity|journal"`全通过；真实拔卡/掉电恢复保留至少最后有效位置；挂载失败不自动格式化。

### T05 导航、输入与唯一显示调度器

当前状态：部分：输入与显示确认已接，全量设备交互/调度门待做。细分证据见PROJECT_STATUS。

**拟建文件：**`components/pn_shell/pn_router.c`、`pn_navigation.c`、`components/pn_ui/pn_gesture.c`、`components/pn_display/pn_scheduler.c`、`pn_damage.c`、`pn_recovery.c`、`tests/host/test_router.c`、`test_gesture.c`、`test_display.c`。

**接口：**输入`pn_intent_t`（turn/tools/back/lock），`pn_job_token_t`；输出display_submit引用缓冲与完成事件；导航保留origin，返回恢复来源。

- [ ] 接入桌面鼠标/三键、旋转映射、确定性脚本、display job记录与慢刷新注入；检查buffer lease与过期完成丢弃。
- [ ] 写滑出/多点/触摸错误/切页取消、600ms长按一次、刷新中最多一条导航意图测试。
- [ ] 实现路由与取消token；render只画图，页面不执行PMU/存储提交副作用。
- [ ] 实现buffer lease、job token与stale丢弃；扫描中禁止worker写目标buffer。
- [ ] 故障注入line queue underrun，验证降12MHz后目标页恢复而非永久白屏；再失败进入保护。
- [ ] 真机验证屏下三键、强刷、旋转坐标、菜单/锁屏来源返回，更新初值阈值。
- [ ] 提供基础保存屏障、系统默认锁页与浅睡唤醒接口；T08接入内部壁纸缓存与原方向恢复，0.1无需等待T13完整电源/升级阶段才可锁屏。

**验收：**主机事件与显示错误用例通过；真机快速连点/反向不会无穷翻页或错页。

### T06 字体服务与清晰度

当前状态：部分：选择/预览/偏好/上传已接，完整管理与清晰度验收待做。细分证据见PROJECT_STATUS。

历史0.0.6起建立固定FreeType TTF port、受限分配/真实度量/灰阶与二值绘制、常驻UI子集及PC实际TXT捕获，见FONT_PORT。T06的变化轴选择/LRU/完整fallback/上传安全/fuzz/真机仍未验收。

**拟建文件：**`components/pn_font/pn_font.c`、`pn_metrics.c`、`pn_cache.c`、`pn_variation.c`、`pn_fallback.c`、`tests/host/test_font.c`、`tools/font_compare.py`、`assets/fonts/README.md`。

**接口：**UI与reader独立`pn_font_context`，measure与raster分离；font_id为内容摘要；cache_key包含px/variation/mode/gamma。

- [ ] 写复合字形、无wght轴、非法loca/点数/variation片段、缺字fallback与预算测试。
- [ ] 拆分移植受测TTF实现，支持真实axis clamp与静态字重选择，不只复制固定wght范围。
- [ ] 实现1MiB文本cache、PDF退让384KiB、按需I/O与LRU；重复开关字体回到资源基线。
- [ ] 真机对比28/36/44/56/72px、不同字体、0.6/0.8/1.0覆盖曲线、灰阶与二值。
- [ ] 制作完整TF字体部署包与许可说明，UI字库覆盖所有界面。
- [ ] 建立字体内容摘要索引、内部家族/样式/可变轴信息与样例缺字结果；上传替换或删除使用中字体先关闭源，UI常驻字体不受影响。

**验收：**无整章度量光栅化；字体错误不使UI失效；选择覆盖曲线有照片依据，堆限额内可运行。

### T07 TXT/EPUB与增量分页内核

当前状态：主体TXT/EPUB代码已接，兼容/异步分页/热翻页门未完成。细分证据见PROJECT_STATUS。

历史0.0.9起建立TXT流式编码/源映射、文件source、有界页节点、真实TTF绘制、PC与设备阅读会话及逐书续读，见TEXT_CORE、READER_SESSION。TXT/EPUB、目录与排版后续已接，现阶段仍需异步/预绘制、兼容与完整产品验收，不能作为T07整体通过。

**拟建文件：**`components/pn_format/include/pn_document.h`、`pn_txt.c`、`pn_epub.c`、`pn_zip.c`、`pn_block.c`、`pn_css.c`、`components/pn_reader/pn_session.c`、`pn_layout.c`、`pn_locator.c`、`pn_prepaint.c`、`tests/host/test_txt.c`、`test_epub.c`、`test_layout.c`、`test_locator.c`。

**接口：**document capabilities/BlockStream与源映射；reader_request返回prepared_page与location；layout_key见RENDERING，规范化版本写索引。

- [ ] 建编码/段落/长章/坏ZIP/越界路径/nav与NCX/基础CSS/脚注fixtures；每个失败都验证输出清空和资源释放。
- [ ] 实现有界解析与256KiB工作窗口，不整本加载；长段续段保源偏移。
- [ ] 测中文标点禁则、英文词、标题孤行、页脚高度、字号改动保持原文锚点、跨窗口字符不丢。
- [ ] 实现当前/邻页优先、512页磁盘索引分段、generation取消与热页预绘制；完成前总页数显示省略。
- [ ] 跑TXT/EPUB语料与硬件热翻页、跨章、冷末页定位，保留可取消等待提示。

**验收：**标准语料内容不丢字、不重字、位置换设置不跳章首；host sanitizer通过，满足0.1性能目标。

### T08 书库、封面与产品UI

当前状态：部分：基础书库/最近/书签/设置与列表封面缩略图/缓存已接，网格、封面回退/替换、管理/壁纸待完成。细分证据见PROJECT_STATUS。

历史0.0.9已有六条可见文件分页、PC/设备选择TXT、返回和逐书续读，见CATALOG。封面卡仍为格式占位；最近/书签列表和字体选择后续已接；完整元数据、封面、资源管理、壁纸和全量旅程尚未完成。以下64记录指计划中的索引服务批次，当前扫描只保留六条可见候选。

**拟建文件：**`components/pn_library/pn_catalog.c`、`pn_cover.c`、`pn_search.c`、`pn_collection.c`、`components/pn_ui/pn_tokens.h`、`pn_widgets.c`、`components/pn_shell/pages/shelf.c`、`reader.c`、`typesetting.c`、`toc.c`、`bookmarks.c`、`settings.c`、`tests/host/test_catalog.c`、`test_cover.c`、`test_settings.c`。

**接口：**catalog分页64记录，UI页保存visible model，cover返回4bpp缩略图lease；draft settings应用/取消，不直接修改live settings。

- [ ] 验证1000本冷库首页不等所有封面、长UTF-8书名不越界、自然/最近排序稳定。
- [ ] 实现封面优先级、侧车、自绘占位、六张可见缓存；提取失败仍入库。
- [ ] 按UI_UX与三屏图实现，控件80px以上命中，原生字体而非低清放大。
- [ ] 桌面模拟器覆盖书架/阅读/排版/字体/壁纸/锁屏旅程，固定字体摘要与输入脚本，审核4bpp golden；真机另查纸屏观感。
- [ ] 按READ-05/06实现至少100书签/书和20最近历史、去重/跳转返回/重命名/删除确认/跨重启索引；清进度不清书签。
- [ ] 实现目录/书签/章内搜索、排版预设与250ms预览合并、应用/取消回原锚点。
- [ ] 实现空态、缺字/缺卡/坏书/保存失败，拍照走完整首次使用与单手旅程。

**验收：**LIB/COV/READ/FONT/TYPE/FLIP P0可用；UI所有页面有返回出口，无截字/遮挡/仅灰阶辨状态。

**本次新增文件与接口：**`components/pn_personalization/pn_font_index.c`、`pn_wallpaper.c`、`pn_wallpaper_cache.c`、`components/pn_shell/pages/fonts.c`、`wallpapers.c`、`tests/host/test_wallpaper.c`、`test_font_management.c`。`pn_wallpaper_prepare(source,transform,budget,token)`输出684×1216/4bpp候选；`pn_wallpaper_apply(candidate)`经存储屏障提交A/B槽；`pn_wallpaper_load()`只返回有效代或系统默认。字体管理调用T06服务，不另建不一致的光栅引擎。

- [ ] 实现字体列表、样例预览、全局/逐书选择与删除确认，使用中删除需先有替代/确认回退；缺卡有明确上传指引。
- [ ] 实现四种锁屏模式、JPEG/PNG方向与透明白底、contain/cover/旋转/裁切位置、应用/取消/恢复默认。
- [ ] 对A/B缓存写入、同步、CRC、选择提交逐点故障注入；无TF或原图消失后仍可使用已应用缓存，不增加常驻全屏帧。

**扩展验收：**FONT-02设备管理、WALL-01/WALL-02预处理与预置样本通过，T09用户上传接通后满足0.1端到端验收。

### T09 AP/STA网页传书与基本导入

当前状态：部分：网页/热点/扫码已接，STA配网与真机传输待做。细分证据见PROJECT_STATUS。

**拟建文件：**`components/pn_transfer/pn_pairing.c`、`pn_http.c`、`pn_credentials.c`、`web/index.html`、`web/transfer.js`、`components/pn_shell/pages/transfer.c`、`tests/web/transfer.test.js`、`tests/host/test_pairing.c`、`test_upload_commit.c`。

**接口：**TRANSFER API v1；T09先实现单文件流＋part/backup与授权，端点沿同协议保留会话结构；T11升级持久分块，不能把临时会话称为断点续传。

- [ ] 测授权失败、Origin/Host、非法名字、Unicode、重复同SHA与覆盖确认、容量不足。
- [ ] 实现AP二维码两步、STA配网、保存凭据单blob、退出停止并join HTTP；UI不拿密码日志。
- [ ] 测TTF预检/替换失败留旧字体、文件提交各phase失败、HTTP断开删除未提交临时流。
- [ ] 真实网页脚本在mock DOM/net跑批量队列、取消、失败继续、无innerHTML输入；真实手机再测。
- [ ] 真机吞吐与二维码；进入传书停正文/PDF工作，退出恢复原来源。
- [ ] 网页加入字体/壁纸标签，字体≤32MiB/壁纸JPEG-PNG≤8MiB及独立像素限额；校验失败保留旧资源，成功不自动应用。
- [ ] 实现配对授权的字体/壁纸列表与删除端点、字体使用中409、原图删除后的壁纸缓存保留提示；网页mock测试覆盖无TF和不支持格式。

**验收：**0.1可方便传TXT/EPUB/字体/壁纸，并在设备预览应用；FONT-02与WALL-02上传流程通过真实网页与故障测试；不声称跨断电续传已完成。

### T10 PDF原生产品化与视图

当前状态：未完成，依赖T02。细分证据见PROJECT_STATUS。

**拟建文件：**`components/pn_pdf/pn_viewport.c`、`pn_pdf_toc.c`、`pn_pdf_search.c`、`components/pn_shell/pages/pdf_tools.c`、`tests/host/test_viewport.c`、`test_pdf_location.c`。

**依赖：**T02门通过，T04身份和T05显示；document fixed_page能力进入T07 reader会话，不走文本layout。

- [ ] 视口测试CropBox/90°旋转/fit/zoom/分区次序，书签与恢复使用原PDF页号。
- [ ] 实现684×96条带、4bpp合成、image/cache预算及取消；每个strip检查token与media epoch。
- [ ] 实现整页/宽适配/裁边/离散倍率/横屏/目录，文本层搜索可用才显示。
- [ ] 40本样本走首屏/翻页/缩放/唤醒/拔卡，错误页可返回书架或换页，不能锁死会话。
- [ ] 扩展许可发行材料与PDF支持说明，扫描本与文字本分别记录性能。

**验收：**READ-03 PDF原生达标；原版不出现无效字号/段落控件，pdf heap＋系统总峰值达标。

### T11 持久分块续传与导入辅助

当前状态：部分：事务续传已接，丢失创建回复找回与转换助手待做。细分证据见PROJECT_STATUS。

**拟建文件：**`components/pn_transfer/pn_upload_session.c`、`pn_chunk.c`、`pn_commit_recovery.c`、`web/hash-worker.js`、`tools/importer/cli.py`、`tests/host/test_chunk.c`、`test_commit_recovery.c`、`tests/web/resume.test.js`。

**接口：**upload_id/next_offset/complete按TRANSFER；persist_ACK只在同步块和A/B manifest后返回；电脑工具采用相同HTTP v1。

- [ ] 写重发上一块/错误摘要/越序/取消/32MiB文件断网再选/跨启动新配对恢复用例。
- [ ] 实现64KiB块和A/B manifest、未确认尾块裁剪、完整SHA与真实持久同步路径。
- [ ] 对commit每个phase自动故障注入，backup保留警告不漏入库事件，旧进度按content_id保留。
- [ ] 网页增量hash worker限内存；手机熄屏、刷新、重选同文件继续，未知摘要模式到complete核验。
- [ ] 电脑工具支持MOBI/AZW3外部calibre转换、预览与批量发送，检测未安装/DRM失败明确说明。
- [ ] 对font/wallpaper kind同样验证分块续传、摘要、容量、覆盖恢复与跨开机配对；不把书籍限额套给字体/壁纸。

**验收：**XFER-02在断网/断电/拔卡样本可恢复；吞吐与ACK持久性同时达标，丢弃临时文件不伤旧书。

### T12 FB2/CBZ与完整书库功能

当前状态：未完成：FB2/CBZ与完整书库尚未交付。细分证据见PROJECT_STATUS。

**拟建文件：**`components/pn_format/pn_fb2.c`、`pn_cbz.c`、`pn_xml.c`、`components/pn_reader/pn_search.c`、`tests/host/test_fb2.c`、`test_cbz.c`、`test_search.c`。

**接口：**FB2输出BlockStream，CBZ输出fixed_page；复用budget、identity与location，不能另造永久页码方案。

- [ ] FB2深XML/base64超限/外部实体拒绝/诗歌/封面测试，CBZ自然排序与隐藏条目测试。
- [ ] 实现图片按需加载与缩放、CBZ分区，避免解码所有图页。
- [ ] 实现整书可取消搜索、集合/收藏/已读/批量管理，逐项失败显示。
- [ ] 真机小说/日文横排/漫画/诗歌测试，替换/清进度/删除不混淆。
- [ ] 更新格式范围和转换帮助，CBR等无效格式有清晰入口。

**验收：**LIB-03、READ-03 FB2/CBZ、READ-04通过；所有新增解析路径进入sanitizer/fuzz语料。

### T13 电源、USB产品通道与安全升级

当前状态：未完成：睡眠/USB/OTA产品闭环与真机门待做。细分证据见PROJECT_STATUS。

**拟建文件：**`components/pn_power/pn_sleep.c`、`pn_save_barrier.c`、`components/pn_transfer/pn_usb_msc.c`、`components/pn_update/pn_update.c`、`pn_boot_confirm.c`、`tests/host/test_power.c`、`test_usb_lease.c`、`test_update.c`。

**依赖：**USB依赖T03门，OTA依赖T01分区与T04 journal；升级包版本/板型/签名/摘要必须检查。

- [ ] 测锁屏保存失败拒绝关机、网络停止超时、空闲关轨、浅睡与PMU软睡恢复。
- [ ] USB若通过，实现所有TF消费者退出、卸载、独占、弹出后重挂和重新哈希；若未通过保持禁用并记录。
- [ ] 实现双槽固件签名/摘要、板型匹配、启动确认、回滚和配置兼容；不自动烧安全eFuse。
- [ ] 真机每个升级写入/切槽/首启确认阶段断电，恢复仍可刷机与继续阅读。
- [ ] 8小时阅读/24小时待机/长时传书电流测量，续航只基于实测容量与耗电报告。
- [ ] 锁屏从内部有效壁纸槽加载并GC16，横屏阅读恢复原方向；叠加静态提示不因时钟周期唤醒，缓存损坏回系统默认且不阻塞保存屏障。

**验收：**POWER-01、UPDATE-01通过；USB存在则XFER-03通过；耗电/升级恢复无不可逆误操作。

### T14 全体验与正式发布

当前状态：未完成：完整第一版尚未验收。细分证据见PROJECT_STATUS。

**拟建文件：**`tools/benchmark_reader.py`、`tools/run_fault_matrix.py`、`tests/fixtures/manifest.json`、`docs/USER_GUIDE.md`、`docs/FORMAT_SUPPORT.md`、`docs/CHANGELOG.md`、`.github/workflows/build.yml`、部署包说明。

- [ ] 对照VALIDATION全套矩阵，记录硬件/IDF/固件SHA/卡/字体/温度/供电；冷热与不同存储分组统计。
- [ ] UI长书名/大字号/缺字/空库/千本/左右手/取消/错误恢复实测与照片核查。
- [ ] 至少2块板、3张卡进行长时/温度/拔卡/断电测试；无法满足时声明仍为Beta，不以单板发布正式质量。
- [ ] CI接入真实主机测试、网页测试、headless截图/故障回放、两个独立SDKCONFIG固件构建、尺寸与许可证清单；可选QEMU smoke单列，构建可重复。
- [ ] 生成完整烧写镜像与manifest、回滚包、字体许可、使用指南和格式限制，所有公开文案一致。

**验收：**无开放P0故障；P1缺项有明示且不违背PDF要求；性能/可靠性/许可/恢复门通过，才能发布1.0。

## 5. 需求追踪

| 需求 | 实现任务 | 验收 |
| --- | --- | --- |
| LIB-01、LIB-02、COV-01 | T08 | 千本与封面矩阵 |
| LIB-03 | T12 | 集合/批量部分失败 |
| READ-01、READ-02、READ-05、READ-06 | T04/T07/T08 | 位置、编码、分页、书签 |
| READ-03 PDF | T02/T10 | PDF技术门＋产品门 |
| READ-03 FB2/CBZ、READ-04 | T12 | 格式与搜索 |
| FONT-01/TYPE-01 | T06/T07/T08 | 清晰度与锚点重排 |
| FONT-02 | T06/T08/T09/T11 | 上传校验、预览选择、使用中替换/删除、续传 |
| WALL-01 | T01/T05/T08/T13 | 私有A/B缓存、缺卡锁屏、唤醒与静态叠加 |
| WALL-02 | T08/T09/T11 | 壁纸上传、裁切旋转、应用/取消、恢复默认 |
| FLIP-01 | T05/T07 | 事件、显示、热翻页 |
| XFER-01 | T03/T09 | 网络与真实网页 |
| XFER-02 | T11 | 持久ACK/断点/覆盖恢复 |
| XFER-03 | T03/T13 | USB硬件/lease/恢复 |
| POWER-01/UPDATE-01 | T13 | 保存屏障/双槽回滚 |

实现者按本计划创建详细任务内的代码与测试，不在设计阶段假造尚未验证的整套实现。各任务结束提供代码、可复跑检查与必要功能文档；临时性能原始日志存docs/local或build，正式支持结论整理进公开文档。
