# EPUB出版物结构

0.0.15新增pn_xml与pn_epub，建立原生EPUB2/3的出版物模型：mimetype、container.xml、OPF元数据、manifest资源、spine章节身份、NCX/nav路径和封面引用。后续增加 [目录模型](EPUB_NAVIGATION.md) 和 [正文事件](EPUB_BODY.md)。main与PC阅读入口尚未接这些模型；正文分页、目录交互、封面解码与EPUB续读/书签仍在开发，不能把解析通过称为EPUB已经可读。

## 输入与发布边界

pn_epub_open借用已经打开的pn_zip；调用方保活ZIP与不可变source，并在关闭出版物后关闭ZIP、源和介质租约。不读取整本正文，不生成临时TXT。错误保持epub.impl为空，内部暂定模型全部回收；重复关闭安全，活动模型不可复制。

mimetype必须是ZIP首个本地条目、stored且精确内容application/epub+zip。container使用OCF命名空间，选择首个application/oebps-package+xml rootfile；OPF使用规范命名空间，接受version2.0或3.0。元数据取首个title/creator/language及unique-identifier指向的identifier，title与对应identifier不能为空。首尾XML空白去除，不猜缺失作者。前缀名字可变，UTF16 XML输入转换为UTF8回调；单个字段超上限明确失败，不截断半个字符。

metadata/manifest/spine各只能出现一个；所有本地manifest引用必须能找到普通ZIP资源，禁止越根、fragment、外部URI和xml:base（继承base尚未实现）。资源ID不可重复，spine idref按ID字典解析，与ZIP索引和manifest文件顺序分开，保留linear=no；必须至少有一个linear章节。当前spine只接受application/xhtml+xml，其他格式/fallback链尚未接入，返回UNSUPPORTED。spine资源身份提供id/path与本次ZIP索引；ZIP索引不能写进永久续读位置。

OPF全部解压到资源EOF并通过CRC/长度校验、XML结束且交叉引用成立后才发布模型。正文、目录和图片此时只验证引用存在，尚未解压或核对它们的CRC；书籍结构成功不证明全书所有资源有效。get-info/get-spine在复制前重新检查源代次，失效不更改输出；章节越界EMPTY。

## 目录、封面与固定版式

spine的toc ID须指向NCX；EPUB3的nav属性须指向XHTML且最多一个。本层返回路径，NCX/nav条目与fragment引用由独立pn_toc解析。未强制所有书都带目录以保留缺目录的兼容入口，不能称为完整EPUB符合性检查。

EPUB2 cover meta与EPUB3 cover-image指向图片资源；冲突声明拒绝。没有标准声明时只将文件名严格为cover.jpg/cover.jpeg/cover.png的图片记录为候选，cover_declared=false，尚不从guide/封面HTML解析图片。已有路径不意味着可解码或已经显示封面。

全局rendition:layout=pre-paginated或章节rendition:layout-pre-paginated设置fixed_layout；未知全局layout拒绝。此标志用于后续阅读层选择独立固定版式路径，不能按流式正文直接显示。当前还没有布局、CSS和字体继承解析。

encryption.xml存在则UNSUPPORTED；当前没有DRM、字体混淆或解密实现，也没有伪装成乱码继续阅读的路径。普通内嵌字体仅在manifest中分类，不自动安装或运行。

## XML与内存契约

采用固定[Expat2.9.0官方版本](https://github.com/libexpat/libexpat/releases/tag/R_2_9_0)及[流式API](https://libexpat.github.io/doc/api/latest/)，归档摘要和178个原始文件在LICENSES/expat-manifest.json，原许可COPYING保留。仅构建xmlparse/xmlrole/xmltok/xcs，外部配置不修改vendor。

XML输入每次4096byte，解析资源最多32MiB；深度64、每标签属性64、展开名1023byte、属性值4095byte。仅用解析完毕前有效的回调指针，不保存Expat内部指针。允许常见外部DOCTYPE声明，但不读取外部DTD、不开网络、不解析参数实体，内部DTD（包括自定义实体）明确拒绝。XML自身格式、未知实体、截断与无效命名空间由解析器拒绝；这些限制不能替代长期fuzz和取消时间测量。

pn_xml要求调用方每次传入来自可信随机源的16byte哈希盐，NULL/全零拒绝，在任何XML_Parse之前调用XML_SetHashSalt16Bytes。host epub_dump从/dev/urandom读取；测试夹具使用可复现盐。外部配置的XML_POOR_ENTROPY仅使跨平台源码可编译，本适配层强制显式盐，不使用上游低熵后备；设备接入须提供真实熵，不能复制测试常量。不要绕过pn_xml直接创建产品解析器。

Expat全部allocator走pn_pool；realloc记录原长度、复制并保持失败时旧指针。由于其memory suite没有ctx，适配层在同步解析期间设置线程局部池，嵌套调用恢复旧池，跨任务各用自己的TLS；模型和pool仍由owner串行访问。临时4096byte输入也计费。回调返回取消/介质/IO状态会保留原错误并回收。没有系统malloc预算回退。

manifest/spine最多32768，随需要增长指针/记录数组，ID最多255byte、路径1023byte、title/creator/identifier511byte、language63byte；所有模型分配计入同一个pool。ID字典和ZIP条目使用就地堆排序，避免libc qsort的隐藏临时分配。ZIP索引与模型合计能否容纳仍由pool决定，不承诺2MiB能放任意32768资源书籍。

## 开发验证

`python tools/dev.py host`包含XML及epub_package：不同前缀、中文实体、1byte短读/UTF16、外部DOCTYPE不读取、内部实体拒绝、深度、截断、callback取消、每个观测分配点失败回收；出版物元数据、乱序manifest/原spine顺序、linear=no、1000章节ID字典、EPUB3 nav/cover-image/fixed-layout、坏ID/引用/版本/namespace/xml:base、CRC末尾失败不得发布部分元数据、加密资源拒绝和媒体失效。

在开发容器或原生Linux根目录运行：

```bash
cmake --build build-host --target epub_dump
./build-host/epub_dump mockDoc/绍宋.epub
```

CLI只输出元数据和章节资源路径，不输出正文；退出0与used/live=0才算本次成功。本机用户样本和报告被Git忽略，不分发给CI。真实样本可用于比对Python XML/zipfile与C结构结果，但主机池峰值不等于整机PSRAM，也不代表TF吞吐/取消时延。固件交叉编译还没有执行本模型，不能替代上板与阅读验收。
