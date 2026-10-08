# EPUB作者样式与图片版式

0.0.21在出版物正文路径接入head中的本地link样式表和style元素。它与原有style属性、基础语义样式和分页器共用支持的声明；不执行CSS URL，不联网，不安装书中字体。设备/PC交互阅读后续已接入此模块，不能把host捕获当产品完整阅读验收。

## 加载与资源边界

pn_xhtml_parse先完整扫描章节XML/CRC，按head中的文档顺序读取样式源；每个link资源也必须完整ZIP CRC及验证EOF通过。之后重开同一章节发出正文事件，继续完整XML/CRC及媒体代次校验。失败回收所有样式资源，stats不更新；页节点仍在全部成功后才valid。样式缓冲是有界源码，正文不保留DOM或生成整章TXT。

href使用既有容器路径归一，拒绝外链、query、fragment、xml:base及越出ZIP。缺失样式资源为CORRUPT，不静默套默认样式。rel按空白token与ASCII大小写识别stylesheet；alternate或disabled link跳过。type为空/缺失或text/css可用，media为空/缺失、all或screen可用；其他媒体声明跳过。只收head中的style/link，body内style不参与作者样式。低层pn_xhtml_parse_input不可重开输入，仍仅处理元素style属性，不能借此推断出版物样式测试覆盖了低层输入。

最多16个源、每源64KiB、总源码128KiB、512条有效选择器规则。源码缓冲从4KiB增长并计入pool，旧缓冲复制后释放；中途分配失败也回收。每个简单复合选择器最多255字节、8个class/id部分，一组最多16项。超出源/源码/规则预算返回LIMIT；未支持的选择器列表整体跳过。源码与规则容量、增长时的新旧缓冲及XML/ZIP状态均受调用方总pool预算约束。

## 选择器与级联

支持ASCII标签、通配符、class、id及简单复合，例如img、.cover、#front、img.logo、img.a.b#front；class按空白token匹配，顺序不限。支持逗号列表；包含未支持选择器的列表整体跳过，避免把后代选择器错误解释为同一元素。标签/class/id精确匹配，当前严格XHTML路径采用区分大小写策略。

每个支持的属性独立比较important、选择器优先级、文档/声明顺序。id高于class，class高于标签；同等important下行内属性高于规则。非法声明不覆盖此前合法值或占用优先级。注释在原位替换为空白，字符串内的注释记号保留；引号、转义、括号内分号不会拆开声明。

支持display/visibility、font-weight/font-style、white-space、width/height/max-width/max-height和text-align，含义与限制见 [正文事件](EPUB_BODY.md) 和 [页节点](EPUB_PAGINATION.md)。图片几何不继承；行对齐继承，保留图片比例并约束视口。隐藏祖先仍隐藏后代，visibility:visible不能重新显示隐藏祖先内容；不实现浏览器完整可见性模型。源样式的字体尺寸、字体族、段距、缩进等尚未解释，不能覆盖已选择的用户字体或直接套入未经验证的嵌入字体。

后代/子/兄弟/属性/伪类选择器、CSS转义标识符、非ASCII标识符、嵌套规则和所有at-rule整体跳过；@import不读取，@font-face不安装，@media不展开。层叠层、inherit/initial/unset/revert、变量、em/rem/calc、浮动、盒模型、RTL和两端对齐未实现。未闭合块/字符串/注释或源码NUL返回CORRUPT，当前不是浏览器容错CSS解析器。采用的基础优先级关系参考 [W3C CSS Cascading and Inheritance](https://www.w3.org/TR/css-cascade/)；上述子集及差异以本契约为准。

## 验证与维护

运行python tools/dev.py host。epub_capture用自有EPUB和PNG逐像素检验外部class 40%宽/继承右对齐，验证复合/多class/id、行内与important、源码/声明顺序、非法值和注释；坏CSS CRC、单源及规则超限不得创建捕获。epub_body检验外部隐藏、隐藏脚本不被重新显示、缺失/不安全引用、源数量/总量/格式限制，以及所有观察到的分配点，包括源码增长，结束pool used/live均为0。

本机《绍宋》测试：481章节的归一正文统计和UTF32LE CRC与此前独立XML对照结果一致。首个小说章节原有img.logo width:40%和div.logo text-align:right生效，图片在620px可用宽度中为248×454px，位于页frame x404/y100；独立PNG算法核对112592像素一致。章节标题和正文接在图片后，同一页无需只显示整页插图。它未证明全部原书CSS版式已还原，字号56重排与后续页也只是工程捕获。使用的测试字体仍有缺字，后续页可以出现方框，完整备用字库仍待接入。

不改变locator v1：预扫描不增加元素编号，正文位置仍来自原资源XML元素和解码文本区间；字号变化保留源语义定位。当前每页会重复收集样式和完整扫描章节，尚无共享样式/热页缓存、RTOS分片deadline或真机速度验收。原书与字体只读，截图和对照结果留在忽略的build目录。
