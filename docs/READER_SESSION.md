# TXT阅读会话、PC交互与续读契约

0.0.7加入同源pn_reader与pn_reader_app，并连接PC SDL阅读窗口。用户可以实际翻页、回上一页、调整字号，关闭后按书籍内容身份恢复。0.0.8已将控制器接入固定TXT设备入口、输入/推屏/内部文件系统（见 [设备接入](DEVICE_PORT.md)），但未上板验证；不能把PC操作称为真机功能验收。

## 准备与显示确认

pn_reader使用调用方提供的source、字体度量和glyph缓冲，不分配，不负责文件开关或光栅。init传非零session/generation；跨会话必须使用不同的单调session，所有source/metrics/buffer/save在调用期间保持有效；owner串行调用。

prepare只生成当前page和receipt，不更改visible或save。receipt含owner/token/ticket/意图/完整源锚点；有pending时再次prepare返回BUSY。complete只接收准确receipt；错误对象/票号/锚点/重复确认拒绝；旧token返回STALE_JOB，当前显示失败返回IO。成功且source仍有效才更新visible；保存策略只接受这时的位置。源失效不提交旧卡结果。

receipt/ticket不回绕，耗尽后LIMIT。reflow验证layout并推进generation，丢弃pending与旧历史但保留已显示源位置，随后CURRENT重排同一锚点。glyph缓冲不是不可变像素帧：每次prepare可覆盖中间数据，调用方只在成功prepare后绘制，推屏像素仍由display lease保护。

NEXT从最后成功显示页的end继续，PREVIOUS优先最近32页锚点栈；缓存外从书首重算至当前源位置，截断到该边界形成前一页。回退缓存淘汰不限制书长，但没有分段TF页索引，大书冷回退可能较慢。JUMP严格扫描到字符边界；FIRST回书首，CURRENT保留当前源锚点。没有永久页码或未知总页数推算。

若保存策略的presented更新失败，真实显示事实仍记录在visible，返回保存错误供上层阻拦关闭；不能假装失败前的页面仍是屏幕所见。所有接口当前同步，无worker/取消时间片与deadline保证。

## 应用控制器

pn_reader_app_open使用真实文件源，完整SHA-256决定book_id，再严格probe编码；常驻UI字体和用户正文字体为两个独立引擎。一个684×1216像素帧和最多4096glyph工作缓冲由pn_pool分配；正文限定620×960 viewport，不能覆盖header/按钮。接口保存的是原字节位置，改字号不跳章首。

无逐书配置时正文默认44px、左对齐、原始换行、25%段距；按钮为上页/下页/缩小/放大，正文限制28–72px步进2。UI字体固定24px不随正文改变，未完整匹配最终六项工具栏/书架设计；标题只显示产品名/格式，真实书名/章节尚未接元数据。

step按prepare→draw lease→render→publish→start→真实present回调→display complete→reader complete→save tick顺序调用。回调必须完成真正的窗口呈现/物理扫描才返回OK；先排队就返回OK会导致错误保存位置。灰阶初屏GC16，随后最多12次成功GL16更新后请求GC16；推屏失败后重试强制GC16。刷新请求由设备适配执行，不能用回调成功冒充实际扫描完成。渲染/显示错误不提交新位置，旧实际锚点保留。

## 文件、介质与保存

PC open便利接口创建自己的文件介质模型，仅面向同owner的主机文件操作。设备必须调用open_on_media借用BSP确认挂载的全局共享media，不能创建第二个介质仲裁器绕过传书/USB栅栏。READ租约覆盖书/字体寿命，写入与USB独占会被阻挡；退出阅读后释放才能传书/交接。

media_lost由owner检测后调用：detach立即失效旧源，停正文字体/书与字体文件并释放旧租约，推进阅读/显示代次，不画屏/重挂载/格式化，保留已显示原位置与常驻UI字体。若旧扫描正在present回调里，必须安全完成，旧token完成不可保存新位置。真实卡检测/驱动通知仍待板级接线。

state_dir为独立可写内部文件系统目录，PC默认sim-data；可传NULL完全不写持久位置。当前只创建给定的一层目录，不创建未知父路径、不格式化。目录中每个book SHA有`.a/.b`位置文件；同内容移动路径可续读，同路径同大小换内容得到独立新记录，旧记录不清除。

此版本同一state_dir只允许一个应用owner/实例，没有跨进程锁或全局内部存储服务。state介质独立于书TF，持有独占状态租约，不阻塞TF读；设备必须将目录放内部实际挂载的data分区，不可把同张TF伪装成独立介质。内部FS的非破坏挂载/初始化/修复、短写窗口仲裁和各服务之间的队列仍需实现。

实际呈现更新dirty，5次有效翻页/首dirty30秒自动保存；窗口100ms tick检查但只在需保存时写入，失败自动重试限频。close先flush；保存失败返回错误并保留整个会话，禁止释放或切电。修复后可再次close。保存成功后的只读源fclose若错误，会清理资源并返回该错误，此时impl已NULL；这与保存失败保留不同。硬退出/操作系统杀进程属于意外退出，仅靠最后有效A/B记录。

当前只持久化TXT源位置，不保存字号/字体/段落偏好，不记录最近20历史/书签列表。异常版本/损坏日志不自动清空；恢复记录不是有效字符边界时，操作报错，需要后续修复/用户选择书首流程，不偷偷误恢复。

## PC运行与控制

Linux/WSL需要SDL2开发包、图形会话；普通Windows PowerShell+Docker的无窗口捕获不自动显示桌面窗口。

```bash
cmake -S simulator -B build-sim-native -G Ninja -DPN_SIM_SDL=ON -DPN_SANITIZERS=ON
cmake --build build-sim-native
./build-sim-native/pn_sim --book /path/book.txt --font /path/font.ttf --state-dir sim-data
ctest --test-dir build-sim-native --output-on-failure
```

键盘：Right/PageDown/Space下一页，Left/PageUp上一页，+/−调字号，Home书首，Esc尝试保存并关闭；忽略自动重复的KEYDOWN。底部四个按钮对应同样操作。SDL逻辑尺寸处理窗口坐标，鼠标命中保留按钮间隔；真实桌面键鼠/不同DPI缩放仍待人工验证。

保存失败时保留窗口并在窗口标题提示，Esc重试；当前错误呈现只是标题，没有完成全屏中文错误页/重试入口。不能把此原型当最终友好错误体验。

交互窗口要求--page=1且不与--capture同时使用；--headless模式保留静态页捕获入口，不能传--state-dir。交互模式不指定state-dir时使用忽略的sim-data。既有静态捕获UI与交互控制器工具栏不同，后续会统一。

```bash
./build-sim-native/pn_session_snapshot tests/fixtures/reader_sample.txt build-sim/artifacts/session.pgm
```

snapshot使用同一个应用控制器/present回调输出首屏，禁用持久化，只供UI检查；不模拟真实面板。ARGB后台为主机开销，逻辑4bpp、应用工作缓冲与字体分配记入池；主机SDL/stdio/栈不当设备PSRAM数据。

## 验证与第一版剩余范围

host reader验证待显示不保存、失败/篡改/重复/旧代拒绝、45次前后页超过缓存、跳转与重排同源位置、失效源；reader_app用真实TXT/TTF/文件日志验证显示失败保留、字号锚点、跨对象重开、同名同大小替换、保存损坏后拒绝关闭/修复重试、共享READ阻止WRITE、media_lost回收。

启用SDL时自动增加reader_window测试，使用dummy视频驱动和真正SDL事件队列触发next/previous/字号/quit，在独立进程重开并核对offset；这是软件事件验证，未证明物理桌面显示或S3时序。测试环境使用Ubuntu/SDK镜像SDL2 2.30.0及ASan/UBSan；没有用无法链接sanitizer的Alpine构建代替验证。

第一版仍需板级main/worker/输入/保存屏障、内部FS、书架/封面/目录、逐书设置与书签索引/历史、字体上传/完整fallback/变化轴/LRU、锁屏壁纸、AP/STA、EPUB，以及PDF前置技术/许可与真机门。目标保持完整，不以当前单TXT窗口定义完成。

## 书签覆盖界面

0.0.11 overlay借用同一帧与常驻24px UI字体，不改变成功显示的正文锚点；退出后正文强刷。书签页面已通过同源控制器连接两种SDL窗口和设备触摸。顶部增加书签入口与跳转后的返回按钮。有外部正文字体时，按需创建独立36px名字回退引擎，缓存至会话关闭；媒体失效时先关闭该引擎再关字体源。池仍受2MiB上限约束；打开回退失败时保留常驻UI和缺字退路。last_confirmed在每次导航尝试前清零，不能复用此前成功事件来退出当前失败跳转的界面。完整操作见 [书签界面](BOOKMARK_UI.md)。

0.0.13新增逐书排版和显示确认后的配置提交，字号快捷键也持久保存。配置存在时优先于open初始pixels，静态捕获入口仍按显式CLI参数，不加载逐书样式。见 [排版设置](TYPESETTING_SETTINGS.md)。
