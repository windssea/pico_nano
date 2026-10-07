# EPUB目录模型

0.0.16新增pn_toc，流式读取NCX与EPUB3 XHTML nav，生成原文件先序目录、标题、层级及章节/锚点引用。当前仍没有产品目录页面或EPUB阅读会话；成功解析并不证明锚点存在、正文可读或目录跳转已经接通。

## 资源身份与生命周期

pn_toc_open借用pn_epub，只能从出版物自己的ZIP打开资源，避免另一份同名ZIP与章节身份混用。顺序是关目录/资源流，再关出版物，再关ZIP、文件源和介质租约。错误保持toc.impl为空，重复close安全。模型/源保持不变，owner串行调用。

优先使用OPF声明的nav；没有nav才使用NCX。首选资源格式错误、CRC失败或资源不足时明确返回错误，不静默换成另一个目录。没有任何声明返回EMPTY；目录文件存在却没有有效目录、标题或必需目标返回CORRUPT。读取时沿用pn_xml的命名空间、实体、深度、随机盐与完整CRC边界，只有最终验证通过才发布全部条目。

出版物新增ZIP资源索引到章节序号的有序映射，采用就地排序与二分查找，不逐条扫描spine。相同资源在spine重复时取首个对应章节，linear=no仍可定位。序号与ZIP索引都是当前模型的临时值，永久续读/书签仍需独立语义定位，不能直接保存这些整数。

pn_epub_resource_open在出版物自己的ZIP打开精确路径流；调用方必须保持源有效并遵守既有流verified契约。它不负责URI归一或写文件，正文/目录消费者先使用pn_resource_resolve。

## NCX与nav支持

NCX要求规范命名空间的ncx/navMap/navPoint、navLabel/text及content src。navPoint按文件先序保留层级，忽略playOrder排序；每项有非空标题和章节目标，重复标签或content拒绝。navList等非主目录不参与模型。

EPUB3要求XHTML html/body，选择epub:type包含toc的唯一nav；忽略landmarks等其他导航。直接ol/li及每项a或span提供条目，a内的strong/em等内联文字保留，br视为空白。span是不可点击的分组标题，可有子列表；分组path/fragment为空、target=false、spine_index=SIZE_MAX。不是完整HTML5解析器，不修复缺失标签；非法嵌套、无标签的原始li文本等未作兼容推断。

标签按XML空白（空格、Tab、CR、LF）折叠、去首尾空白，保留Unicode字节和NBSP；最多511 UTF8 bytes，过长明确LIMIT，不截断半个字符。层级从0开始，受XML深度64限制。目录最多32768项，记录数组按需增长，标签缓冲64/128/256/512按需分配，路径与fragment紧凑保存；所有分配计入调用方pool。此数量上界不承诺2MiB能放任意大目录。

href/src相对目录文件解析，一次percent解码，分别保存规范路径和fragment。目标必须属于已解析spine（可含linear=no）；非章节资源返回UNSUPPORTED。外部URI、query、容器越界、xml:base与过长字段拒绝，不发起网络读取。锚点这里只保存身份，正文层必须验证存在性并给出友好反馈。

get-count/get-entry在复制前验证出版物及媒体代次，错误保持输出不变；越界EMPTY，不把不存在条目伪装成空标题。整个目录的CRC失败不得暴露已解析的前半部分。

## 验证入口

```powershell
python tools/dev.py host
```

epub_toc使用自有小书验证NCX与nav层级、分组、内联文字、空白、percent UTF8锚点、重复章节首个定位、UTF16、无效路径/声明/长标题/命名空间、目录CRC错误不发布、每个实际分配点失败回收及媒体失效输出保持。它与既有出版物/ZIP测试一起运行，用户小说不作为CI夹具分发。

开发容器/原生Linux可对本机书运行：

```bash
cmake --build build-host --target epub_dump
./build-host/epub_dump mockDoc/绍宋.epub --toc
```

CLI输出元数据、章节与目录身份，不输出正文。必须检查退出码与used/live=0；成功前输出片段不能当有效目录。只读原书，mockDoc及build-dev报告被Git忽略。主机池峰值不代表整机PSRAM、目录UI速度、TF吞吐或刷新时延。

下一层仍需XHTML正文块与基本样式、id锚点匹配、语义位置、目录交互、EPUB续读/书签及封面。相应范围没有以本模型替代。
