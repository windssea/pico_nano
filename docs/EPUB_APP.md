# 原生EPUB应用与PC窗口

0.0.27新增pn_epub_app，装配原文件SHA/租约、ZIP/出版物、实际FreeType字体、PNG/JPEG、显示owner和PNEP保存策略。PC独立阅读窗口已能实际绘制/翻页/调字号/关闭保存和跨进程恢复。设备主入口和PC书架已接入 [混合阅读路由](EPUB_ROUTING.md)，设备尚未真机验证；七项中文排版面板已接入；EPUB书签界面已接入；其他第一版功能未完成。

## 所有权和确认

open_on_media借用已挂载book media，open建立PC本地media模型；不挂载、格式化或更改VCOM。字体文件可空用常驻子集，非空按同一介质租约打开；不改原书/TTF。原字节SHA识别，在指定state目录创建逐SHA的.epub.a/b和.style.a/b；未知/损坏恢复不猜默认。

step完成章节、字体和PNG/JPEG图片ZIP CRC，publish到显示调度器再调用实际present，只有显示完成和源验证均成功才确认。失败显示保留原位置；保存失败可能在显示后，新画面仍是真实新位置。确认快照在拔卡后保留供保存，close先flush位置/未完成样式，失败保持会话和dirty供重试。媒体失效停止字体消费者、失效pending，不绘制或重挂载。

## 绘制和预算

684×1216逻辑4bpp，同源字体/动态基线、七项逐书样式、正文viewport裁切及中文标题/按钮。标题按用户书名，不虚构作者。缺字方框计数，完整外部备用字体链后续已接；广泛字库覆盖、粗斜体合成、完整CSS与塑形仍待验收。

4096节点/16图片槽为prepare scratch，验证后按实际数量压缩；下一请求重新提供足够容量。图片前关闭正文/UI临时engine，之后按需打开，保留绘制顺序和透明合成。首次/失败后/周期请求GC16，普通页GL16；真实波形/温度/耗时仍由设备port负责且未实测。PC ARGB为主机额外开销，不计设备pool。

本机《绍宋》与指定LXGW字体：原生应用FIRST绘制JPEG封面，显式6MiB pool峰值5769202bytes；TOC index3进入首小说章节并NEXT的正文峰值1539892bytes，原文位置text_003.xhtml element13/run0/offset14。结束pool均0。用户TTF在该页仍有2个缺字方框，备用字库需补。图像来自新app/display确认链路，不是旧page_capture；不说明整机8MiB PSRAM/面板已验收。

## 目录与排版接口

目录count/get懒加载完整NCX/nav，不改变位置；toc_jump解析id/章节并走真实显示，失败不改位置。分组/缺失idEMPTY，其余错误保留。设备/PC目录列表入口已接 [共享中文目录UI](EPUB_TOC_UI.md)。

style_apply实际原锚点重排/显示后PNTS逐书保存，支持七字段；显示失败回滚配置，写失败留保存屏障，重开用已有逐书设置。七项预览/取消面板现与TXT共用，顶部“排版”或PC按S打开，操作与保存边界见[排版设置](TYPESETTING_SETTINGS.md)。

## PC使用与验证

构建：python tools/dev.py sdl。容器/原生Linux运行 build-sim-sdl-idf/pn_sim --book mockDoc/绍宋.epub --font mockDoc/LXGWWenKai-Regular.ttf --state-dir build-dev/native-epub-state。

--book ZIP内容路由尝试原生EPUB，仍完整验证mimetype/OPF，不表示任意ZIP/CBZ已支持；EPUB默认pool6MiB，显式--budget优先。左右/翻页键/空格翻页，加减调整字号，Home回首、Esc关闭，底部中文按钮可点击，滑出/失焦取消。关闭保存失败保持窗口。PC书架已可打开TXT/EPUB，混合切换与最近阅读验证见路由文档。

host epub_app用自有EPUB/PNG、真实TTF/4bpp回调，覆盖失败显示、原锚点重排、逐书字段重开、目录正常/失效/失败推屏、关闭保存阻断后修复、拔卡确认位置及所有观察到的open分配失败回收。SDL epub_window用真实事件/renderer覆盖键盘/鼠标、字号及跨进程恢复，结束pool0；dummy通过不是物理窗口视觉/面板实测。

后续EPUB书签、封面缓存、完整字体/上传/传书/壁纸/PDF及硬件资源/延迟/功耗仍在完整第一版范围。

EPUB语义书签存储及会话接口现已实现，中文管理界面与入口自0.0.32接入，见[EPUB书签](EPUB_BOOKMARKS.md)。
