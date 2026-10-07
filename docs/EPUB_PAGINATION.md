# EPUB页节点与真实捕获

0.0.18新增pn_epub_page，把已实现的XHTML事件流转换为可绘制页节点，保留字号无关的源位置。host epub_capture可使用实际TTF生成4bpp页，并已接 [PNG绘制](PNG_RENDERING.md)；主界面与阅读会话尚未接入，完整CSS版式、目录操作、EPUB续读/书签仍待完成。它不是完整EPUB阅读验收。

## 输入、页与语义衔接

pn_epub_page_prepare从出版物自己的章节资源读取；低层page_input可接带验证EOF的输入流。start=NULL表示章节首，TEXT位置精确匹配，ELEMENT位置在对应事件开启消费，不按元组数值推断全局顺序。扫描目标前的事件仍用于重建是否段首，重排时保留源语义锚点，不复用TXT偏移。

节点携带原位置、样式、实际字体度量、26.6横坐标、基线及码点/图片槽。begin指向本页首节点（或有意义的空白事件），next是被留给下一页的源位置；has_next=false仅表示本章节无更多页，不代表整书结束。跨章节及上一页缓存属于后续会话层，不能把本页计数当永久页码。

收满当前页后停止字体/图片度量，但继续整个XHTML资源解析与CRC验证。仅最终完整成功才valid；后半段XML或CRC错误、内存/度量失败时valid=false、节点/图片数量归零，暂定缓冲不可呈现。这里只生成页，未调用显示owner、提交进度或写书签。

## 排版与图片能力

字体回调提供样式相关的真实advance/ascent/descent/行高/像素字号。行基线采用同行最大ascender，行高包含最大descender及配置/字体行高，避免标题与正文重叠；大行落不进页底时保留整行给下一页。EPUB适配不使用pn_layout_t.ascent的固定基线值，实际字体度量决定基线。首行缩进、段距来自调用方配置，容器与真实段落分开，不重复施加段距；pre换行/BR保留硬换行语义。

英文优先在空格后的词边界折行，过长词按字符切分；中文采用基本开/闭标点处理。跨行搬移节点保留原位置与样式，后续页重新从原事件定位，不复制生成的字符串来持久化。它还不是完整Unicode断行、复杂脚本塑形、断词和两端对齐实现。

图片也是页节点：尺寸回调收到规范资源路径及可用宽高，返回实际尺寸；路径与alt复制到调用方最多16个图片槽，图片与文字统一行基线。没有图片能力时，页面真正到达图片会UNSUPPORTED；缺图片槽/尺寸超限明确失败，不静默丢图。host捕获已接到PNG解码与图形绘制；JPEG已通过 [图片资源桥接](IMAGE_RESOURCES.md) 接入捕获，图片槽本身不是解码成功声明。

0.0.20支持style属性中的width/height/max-width/max-height：正数px或百分比，可含小数；width/height的auto和最大尺寸的none恢复默认。百分比以页内可用宽/高计算，图片按原比例适应约束框及视口，不拉伸变形；两轴显式设置仍为contain效果。百分比高度无需CSS包含块高度，因此属于阅读器子集，不是浏览器完整盒模型。零尺寸保留默认可见尺寸，负值、未知单位或超过10000单位的值忽略；不通过尺寸声明隐藏图片。图片度量回调须先返回合法自然比例适应视口的尺寸。

text-align的left/start、center、right/end沿元素继承，最终在行结束时按首节点的对齐设置移动整行；start/end目前按从左到右解释。图片尺寸不继承，父容器width不能直接当子图宽度。支持声明顺序与important优先级，非法声明不覆盖此前有效值。出版物路径的外部stylesheet与简单class/id选择器见 [作者样式](EPUB_STYLES.md)；HTML width/height属性、margin/float/块宽度、两端对齐和RTL仍未接通；不能拿行内样式通过推断书内CSS已生效。样式和分页变化不改变locator版本或元素/文本位置。

布局区域1–4096px，最多4096节点、16图片槽。所有页缓冲由调用方所有，临时布局和XML/ZIP分配计入pool；无整章文本/DOM。达到固定容量返回LIMIT，不把半页当正常满页。某个字形或同行组合本身放不进空页时明确LIMIT，避免返回重复的空白下一页。

当前每次分页仍从章节起点扫描到完整EOF，未有热页缓存/解析检查点/输入分片deadline保证。捕获按页号顺序重建，不能从主机表现推断真机热翻页已达标。

## 测试与真实捕获

```powershell
python tools/dev.py host
```

epub_page检查页衔接、父元素子节点后的精确恢复（非全局元组排序）、可变行度量、图片槽与基线、百分比/最大尺寸及继承对齐、重排锚点、中文标点续页、容量/行高限制、后半段坏XML不可呈现、所有观察到的分配点回收。epub_capture用自有EPUB与真实UI字体核对两页差异、下一页锚点等于第二页begin、不同字号捕获、预算不足和晚期坏XML不创建文件，以及40%右对齐PNG实际像素区域与外侧白底。用户小说和TTF不进入CI。

开发容器/原生Linux可生成本机样本页（chapter为从0起的spine序号，page从1起）：

```bash
cmake --build build-host --target epub_capture
./build-host/epub_capture mockDoc/绍宋.epub mockDoc/LXGWWenKai-Regular.ttf 2 1 44 build-dev/epub-sample/page1.pgm
./build-host/epub_capture mockDoc/绍宋.epub mockDoc/LXGWWenKai-Regular.ttf 2 1 56 build-dev/epub-sample/page1-56.pgm
```

684×1216 PGM来自同源4bpp绘制，不保存进度；字号28–72px，章节/页越界明确失败。标题节点使用默认正文的1.25倍尺寸，标志中的粗体/斜体尚未合成；这是工程捕获排版策略，不是产品字体设置。缺字按方框显示并计数，完整用户备用字库仍未接入。

《绍宋》序言及小说章节起始PNG图片和后续文字页可以捕获；JPEG封面已可通过完整ZIP校验捕获。小说起始logo的外部CSS宽度40%和父级右对齐已生效，插图后可接标题及正文；完整原书CSS版式仍未还原。不能绕过CSS/图片支持边界宣称完整阅读；后续须完善版式与缓存，再连接会话/保存。

产物仅在被Git忽略的build目录，原书/字体不变。字体度量、页节点与逻辑frame计入捕获pool；BSP/system/设备显示缓存和实际TF性能不在此结果内。截图不是面板照片、刷新或功耗实测。


原生导航、临时事件顺序及stop边界分页见 [EPUB会话](EPUB_SESSION.md)，尚不表示设备交互/持久化已接通。
