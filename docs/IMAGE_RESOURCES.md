# 出版物PNG/JPEG图片资源

0.0.24加入pn_image内容路由和pn_epub_image资源桥接，host epub_capture已接入；可从原EPUB绘制PNG插图、基线/渐进式JPEG及JPEG封面。书架封面缓存、壁纸安装、设备/SDL EPUB交互会话仍需接入，不能把捕获当第一版完整验收。

## 内容识别与校验

pn_image_probe/draw读取8byte签名并重放给原PNG/JPEG解码器，无seek、整文件副本或额外解码策略。按实际内容选择格式，扩展名/OPF MIME不能使JPEG误送PNG解码器；未知格式UNSUPPORTED，过短/无进展源CORRUPT，介质/IO/取消错误保留。图片元数据仅成功后复制，失败原输出不变。PNG/JPEG支持范围分别见 [PNG](PNG_RENDERING.md) 和 [JPEG](JPEG_RENDERING.md)。

pn_epub_image_probe/draw从借用出版物所属ZIP打开规范路径，错误或成功均关闭自己的流，不关闭出版物/源。probe仅尺寸/类型，不能当图片校验完成；draw成功须图片完整终止/无尾随字节、完整资源EOF以及ZIP CRC已验证。资源CRC损坏即使图片能解码也必须失败。XHTML引用的图片缺失为CORRUPT，不返回可能被阅读层误当作章节结束的EMPTY。暂定frame在图片、正文及其他页资源都成功之前不能呈现或提交阅读位置。

路径归一由XHTML事件层完成；资源桥接不根据另一个ZIP的索引打开文件。page_prepare只验证正文并获得暂定图片尺寸，后续页绘制须对每个图片节点调用完整draw。某个未显示图片尚未被完整校验，不能因章节XML成功而给它盖验收章。

## 捕获内存生命周期

捕获最后一页验证成功后，将4096节点/16图片槽scratch压缩为实际数量，释放未用容量；保持节点、位置、原绘制顺序和图片尺寸。达到空白页时可释放零节点缓冲，不改变空白语义锚点。压缩分配失败明确失败，不创建新捕获。

遇图片节点先关闭正文FreeType engine，保留借用TTF文件源；图片绘制结束才按下一字形的实际字号重新打开。UI常驻中文字体在图片完成后才打开绘制页头/页脚。逐节点绘制顺序不变，PNG透明度仍对当时已有frame合成，不将图片全部提前画而改变覆盖顺序。普通TXT会话生命周期未由此改变，设备/SDL EPUB会话以后需显式采用类似预算安排。

原捕获默认pool仍2MiB，不静默突破预算；大渐进式需显式预算，其系数取决于原图而不是缩略图尺寸。《绍宋》原封面通过EPUB/ZIP路径目标620×870，显式6MiB预算下，压缩scratch后pool峰值5718098bytes，结束used/live为0。之前未压缩节点时为6167138bytes。这个pool含出版物、当前帧与所需阶段临时对象，仍不包括设备EPD驱动的其他帧、Wi-Fi、RTOS/系统内存；不能宣称8MiB PSRAM整体已足够。

JPEG与独立Pillow解码器之间此前44像素一级灰阶差异仍未查明，没有被内容路由消除。新的EPUB捕获图像区域539400像素与同一JPEG解码器独立输出一致，这只是桥接/尺寸/坐标对照，不是第二次独立JPEG算法验证。原书与TTF不修改。

## 验证与使用

python tools/dev.py host运行image_route：PNG/基线/渐进式内容识别、1byte短读与前缀重放、源失效后info保持、负坐标裁切及未知类型。epub_capture加入真实ZIP中JPEG（即使PNG扩展名/MIME）、逐像素基础检查、截断/尾随JPEG及图片能解码但ZIP CRC损坏不创建新捕获；既有PNG/CSS/正文和错误用例保留。

容器/原生Linux示例（chapter为0起spine序号，非小说章号）：

```bash
PN_CAPTURE_BUDGET=6291456 ./build-host/epub_capture mockDoc/绍宋.epub mockDoc/LXGWWenKai-Regular.ttf 0 1 44 build-dev/epub-sample/epub-cover-page.pgm
```

spine0是封面，工程页脚显示“本节第…页”，不能根据spine0编号写成小说“第一章”。捕获只输出当前资源局部页计数，未保存永久页码。后续跨章/上一页/目录/续读/书签必须由语义位置会话实现，不能用这条CLI的工程页号代替。
