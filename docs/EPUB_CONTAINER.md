# EPUB容器与本地资源层

0.0.14新增pn_format/pn_zip和pn_resource，为EPUB2/3的OPF、spine、目录、正文及封面提供基础。它们还没有连接电子书阅读入口，当前书架的EPUB仍只识别扩展名，不能宣称可读。

解析策略依据[W3C EPUB3.3 OCF ZIP结构](https://www.w3.org/TR/epub-33/#sec-zip-container)。此版本只实现容器资源访问，还未核对mimetype首项、container.xml和出版物结构等完整EPUB约束。压缩采用固定zlib1.3.2，同源host/设备版本，原始[官方版本/摘要](https://zlib.net/)与来源清单见LICENSES/zlib-manifest.json，许可保留于components/pn_zlib/vendor/LICENSE。只构建inflate、校验和及内部工具源，不构建gz文件/压缩写入或minizip；Z_SOLO禁止默认无界分配，Z_PREFIX避免ROM命名冲突。

## ZIP契约

ZIP对象借用不可变pn_text_source，可来自既有受租约保护的真实文件。每次读取、目录扫描、路径查询与流推进验证source；validate可以报告媒体代次或任务取消。关闭ZIP不关闭源，有流仍活动返回BUSY；先关流再关ZIP再释放源/租约。

接受stored0与raw deflate8、UTF-8路径、空资源、数据描述符（可带签名），最多32768条。拒绝加密/其他方法/ZIP64/多盘、超出ZIP32范围源、坏extra和非法版本/flags。每项压缩/原始大小最多64MiB、全部声明输出最多1GiB，这些是资源访问上界，不是一次内存分配。实际file port仍受INT32文件长度限制；不保证所有ZIP/EPUB都兼容。

EOCD精确匹配注释末尾，目录边界/计数逐条验证；中央和本地名称/方法/标志/大小/CRC一致，描述符也必须匹配。所有本地区间排序核对不重叠，目录结束必须吻合EOCD。路径是精确UTF-8名字，不复制全目录名称：仅保留紧凑条目表及源中的名称偏移，按哈希查找并用完整名处理碰撞/重复；重复路径拒绝。路径不允许绝对路径、反斜线、控制字符、点段、空段及保留路径符号，Unix symlink条目拒绝。不写文件，因此没有提取覆盖操作。

32768项索引约1.25MiB，目录尾窗口最大65557bytes，每条流4096byte输入加zlib状态和32KiB字典；输出由调用方持有，每次最多8192bytes。无整章/整本解压缓冲，也不把大数组放worker栈。分配全部经过pn_pool，不够返回NO_MEMORY并释放临时对象。实际长书阅读还有manifest/spine/字体/页面费用，当前2MiB reader池尚未接ZIP，不应从解包测试推断整机长EPUB预算达标。

解包片段是暂定数据。只在压缩流真正结束、精确消耗声明压缩长度、原长吻合且CRC成功后verified=true。多余压缩字节、输出超限、CRC坏或断尾均报错，错误size=0且后续流读保持失败。消费者必须按此边界验证资源完整性；不能因为得到首块就称完整章节可信。未知总长度/缺资源不返回成功空正文。

## 资源引用

pn_resource_resolve将href相对base文件归一化；percent只解码一次，fragment单独返回，严格校验UTF-8/NUL/控制字符。`.`折叠、`..`只允许容器内回退，拒绝根越界；不请求http/file/data等外部URI，不接受绝对路径或query引用。失败输出保持不变。原始ZIP名字不做二次percent解码，避免`%252e`被错误解释成逃逸点段。

这些限制是当前子集：尚无完整URL/IRI规范、外链展示或资源重定位。XML实体/容器/spine由下一层处理，不将整个EPUB转成TXT，也不把解压临时字节偏移当作永久阅读位置。

## 验证与剩余项

`python tools/dev.py host`包含zip_oracle，使用Python zipfile生成stored/deflate Unicode资源，与C输出逐字节比较（1/17/4096byte请求）、空项、不可seek的描述符、32768条目录；验证路径逃逸/重复/头部损坏/CRC/压缩破坏/加密/未知方法/ZIP64哨兵拒绝，以及观测分配故障回收和流活动BUSY/拔卡失效。resource测试覆盖相对路径、UTF-8 percent、fragment、一次解码与根边界。sanitizer不能替代长时间fuzz。

host工具zip_dump读取真实文件并把片段送stdout，必须检查退出状态；错误前已输出的暂定片段不能当有效资源。它不是电子书转换器，不产生永久进度。两套SDK编译确保同源资源代码可交叉编译，当前未由main调用，未上板验证CPU时间/取消响应/栈/PSRAM峰值。

本机真实书样本放在被Git忽略的mockDoc目录，不随源码或CI分发。可用下面的入口只读分析并逐字节比对C解包器与Python zipfile，输出不包含正文；默认抽取容器、出版物描述、目录、首/中/末/最大正文及封面候选，--all-resources核对全部普通资源。报告保存在被忽略的build-dev/epub-sample/report.json，记录前后源摘要核对、资源数量、C池峰值和最终CRC/零残留；这是容器测试，不能证明正文渲染、字体、封面展示、目录跳转或续读已经接通。

```powershell
python tools/dev.py epub-sample --archive "mockDoc/绍宋.epub" --all-resources
```

测试工具的XML读取仅是主机oracle，用来发现manifest缺项和目录目标文件缺失，不是产品XML解析器，也不是完整EPUB符合性校验器。EPUB2的标准cover meta和EPUB3的cover-image之外，带cover的图片名只报告为候选，不把候选冒充标准封面声明；目录fragment锚点及CSS语义尚不检查。用户提供样本若缺少标准声明，产品层后续需明确兼容策略。

0.0.15已有 [出版物结构](EPUB_PUBLICATION.md) 的container/OPF/manifest/spine/元数据和目录封面引用；ZIP与ID排序改为就地堆排序以避免libc临时堆缓冲。下一步仍需NCX/nav条目、流式正文和基础CSS、语义定位/书签、封面/图片、固定布局与友好错误；PDF前置许可/资源门和其余第一版范围仍保留。
