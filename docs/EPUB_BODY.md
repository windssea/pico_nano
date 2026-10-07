# XHTML正文事件与语义位置

0.0.17新增pn_xhtml，为原生EPUB正文提供流式语义事件及id锚点查找。后续已有 [页节点与捕获](EPUB_PAGINATION.md)，但没有接到设备/PC交互阅读，也没有把EPUB转换成TXT；完整CSS、语义位置持久化及EPUB续读/书签仍待接入。完整书籍结构/目录分别见 [出版物](EPUB_PUBLICATION.md) 和 [目录模型](EPUB_NAVIGATION.md)。

## 流与事务

pn_xhtml_parse从出版物自己的ZIP打开已有spine资源。输入经pn_xml处理，继承32MiB资源、深度64、随机盐、命名空间和实体限制，不联网获取DTD，不执行脚本。解析器不保留DOM、整章正文或临时TXT；消费者逐事件接收TEXT、BLOCK_OPEN/CLOSE、BREAK、ANCHOR、IMAGE。支持严格XHTML（UTF8/UTF16等由XML转换），不是HTML5容错修复器，未知命名实体如依赖外部DTD的nbsp仍会失败，尚未加入离线HTML实体兼容表。

所有事件在完整资源EOF/CRC/XML与媒体代次验证前均暂定。sink可以返回取消/IO/内存错误，但不能把提前停止当作资源已验证；收满当前页后可停止生成glyph并继续解析，以完成CRC。失败时消费者须丢弃暂定页面，不能提交续读位置；统计仅完整成功后复制，失败保持原输出。解析分配全走pool，帧栈等状态在池内，关闭资源流不关闭出版物或借用源。

必须有规范XHTML html根和唯一直接body。head不发正文；script/style/template及hidden属性不发正文，隐藏状态沿后代保留。外来命名空间不发文字；SVG image的本地href/xlink:href可作为图片引用事件，未解释SVG几何、路径或固定版式。

## 块、空白与基础样式

区分容器、段落、h1–h6标题、li列表项及pre，避免排版器把div和内嵌p都当成段距。br/hr发BREAK；图片发规范容器路径和alt，不解码、不代替其CRC验证，也不自动把alt拼进永久文本。src必须是可归一的本地路径，无外链、query、fragment或xml:base；图片存在性/内容校验留给图片资源层。

普通正文折叠XML空白并去块边缘空白，跨b/span等内联元素保持词间空格；空格事件引用原文本的首个空白位置。NBSP和其他Unicode空白保持原标量。pre/pre-wrap保留解码后的空白与换行；没有用户字体或像素缩放参与正文语义生成。

b/strong、i/em、标题与pre提供继承标志。style属性仅解释display none及block/inline/inline-block、visibility hidden/collapse/visible、font-weight基础关键字/100–900、font-style基本关键字、white-space normal/pre/pre-wrap，以及 [分页文档](EPUB_PAGINATION.md) 定义的图片尺寸和行对齐；忽略其他声明，处理所支持声明的顺序与important优先级。分号在引号/括号内不拆声明，不执行URL。隐藏祖先仍不显示后代，本子集不是完整CSS可见性级联实现。

出版物路径已接入 [有界作者样式](EPUB_STYLES.md)，包括head样式表与简单标签/class/id选择器；字体族/尺寸/段距、浮动/表格/列表编号、复杂脚本和SVG布局尚未解释；样式标志还不意味着已做真机粗体/斜体呈现。后续排版需把正文事件与既有逐书设置结合，不让书中嵌入字体覆盖用户选择。

## 位置与锚点

PN_XHTML_LOCATOR_VERSION=1描述资源内位置，须结合原书SHA和规范资源路径使用。kind区分元素边界和文本位置：element是全文元素先序号（含head/非显示元素，从1开始），run是该元素的直接解码文本区间号（从0开始，子元素边界分隔区间），offset是区间内原始解码Unicode标量偏移，包含被折叠/隐藏的原始文本。注释、PI和CDATA边界不另建区间，字符实体展开与连续文本合并；不随XML读取块大小或字号变化。

这是本项目语义位置，不是标准EPUB CFI，也不是XML/生成文本字节偏移。不能按element/run元组做全局文本先后比较：父元素在子元素之后仍可能有直接文本。恢复必须按同一版本重新扫描匹配文本位置或元素事件，再由阅读器确认实际显示；持久格式与保存策略尚未接入。

ANCHOR发body中的id/xml:id和元素边界。pn_xhtml_anchor完整扫描并返回唯一匹配，缺失EMPTY，重复CORRUPT，隐藏目标UNSUPPORTED，失败输出不变。未提供传统a name别名、跨资源回退或“最近可见位置”猜测。目录fragment可由这一层核对，但产品跳转仍需真实页呈现后才能保存。

## 验证

```powershell
python tools/dev.py host
```

xhtml检查短读1/17/256/4096byte下文本与语义位置一致、中文和实体、跨内联空白、pre、隐藏脚本/内容、基础style及quoted分号、非法根/重复body、取消与所有实际分配点回收。epub_body使用实际ZIP资源检验归一字符CRC、UTF16、唯一/缺失/重复/隐藏锚点、CRC末尾失败不得输出成功统计、所有观测分配故障。它们不包含产品分页/显示/持久化验收。

开发容器/原生Linux可只读测试本机书：

```bash
cmake --build build-host --target body_probe
./build-host/body_probe mockDoc/绍宋.epub
./build-host/body_probe mockDoc/绍宋.epub OEBPS/Text/version.xhtml some-id
```

body_probe只输出逐章统计与归一文本UTF32LE的CRC32，或锚点位置，不输出小说正文。最后退出0、used/live=0才算成功；CRC32仅作独立oracle比对，不是密码学摘要。用户书和本机报告被Git忽略，源码/CI只分发自有夹具。主机峰值与吞吐不能当面板、TF或整机PSRAM实测。
