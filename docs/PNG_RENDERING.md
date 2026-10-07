# 原生PNG与灰阶图片

0.0.19新增pn_image/pn_png。host与固件使用固定libspng0.7.4/zlib1.3.2同源解码，逐扫描线转到4bpp帧。epub_capture已接PNG尺寸探测和真实图片绘制，JPEG与统一路由见 [出版物图片](IMAGE_RESOURCES.md)；完整CSS版式、产品图片/封面界面及壁纸安装仍未接入，不能称为全部图片功能完成。

## 来源与只读构建

使用 [libspng0.7.4官方版本](https://github.com/randy408/libspng/releases/tag/v0.7.4)，codeload归档SHA256 47ec02be6c0a6323044600a9221b049f63e1953faf816903e7383d4dc4234487，262个原始文件和LICENSE保留，BSD-2-Clause及附带资料许可不变。LICENSES/spng-manifest.json纳入自动摘要检查。外部CMake选择通用实现，不改vendor。

上游单文件还引用编码函数，但本产品只读。外部pn_spng_read_only.h将deflate/compress编码引用映射为明确失败的兼容函数，未扩大zlib六个inflate源构建，不引入标准compress的默认分配。Z_SOLO/Z_PREFIX保持，decoder只创建flags=0的读取ctx，公开适配只提供probe/draw。不得绕过适配层使用上游编码API。

libspng分配接口没有ctx，适配在同步解码期间设置线程局部pn_pool，结束恢复旧池；malloc/calloc/realloc/free含对齐头均计费。realloc失败保留旧指针。zlib由libspng的自定义allocator继续计费，不回退系统malloc。临时RGBA缓冲只有一个原始宽度的扫描线，上游另有少量过滤/读取缓冲，非整图像素存储。

## 输入与完整性

输入read允许短读，EOF须由调用方完成资源完整性校验后返回EMPTY；EPUB适配沿用pn_zip_stream_verified。probe只读取PNG签名/IHDR与其CRC，返回原始尺寸、色型、深度和交错参数，不验证整个图片，也不自动重绕输入。PNG probe错误输出不变。

draw检查PNG流、块CRC、解压校验、IEND和输入EOF；IEND后有数据拒绝。资源CRC/媒体错误保留原状态。失败可能已经改写部分逻辑frame，调用方必须丢弃未呈现的帧，不能推屏或提交阅读位置；info仅完整成功后发布。本层不调用显示硬件，不写源文件。

PNG语义兼容仍遵循上游：一些CRC正确却内容异常的非关键附加块可能被忽略，非法调色板索引按上游退为不透明黑。本层不是完整PNG符合性验证器；关键CRC、截断、流/尺寸/内存错误不会伪装成成功。未使用忽略CRC/Adler选项。

原图最大8192×8192且总像素不超过16Mi，输入最大32MiB，单块及附加块缓存上限2MiB，最多128个流中块。所有实际分配另受共享pool限制，大元数据仍可能NO_MEMORY。固定版本块数量setter在初始化前拒绝正常正数，适配层独立解析流中块长度/数量做上限检查，不修改vendor、不移除限制。

## 缩放、透明与帧

支持PNG合法色型/位深组合、透明度/tRNS和Adam7。解码scanline而非保留去交错整图，Adam7按当前pass的源坐标写对应目标像素，每个被采样像素只合成一次。16bit源转RGBA8后再转灰度。

目标框宽高1–4096，可位于帧外，64bit坐标裁切，不改变邻像素。源坐标采用最近邻缩放；灰度用固定77/150/29亮度权重和舍入，直通alpha与当前灰阶背景合成，再量化0–15。没有ICC/gamma色彩管理、面积/双线性抗锯齿或抖动校准，真实面板质量与缩放优化仍需测试。

epub_capture的图片度量先按自然比例适应可用宽高，再由 [作者样式](EPUB_STYLES.md) 约束实际页内尺寸与行对齐。《绍宋》的img.logo width40%及父级右对齐已经生效，580×1062源图在620px可用宽度中为248×454px，随后标题与正文可以接在同一页。尚未还原完整CSS版式，工程捕获不能作为最终现代排版交付。

## 复跑验证

```powershell
python tools/dev.py host
```

png_oracle独立用Python构造RGBA、Adam7、调色板/tRNS和16bit灰度数据，逐像素比对缩放/透明/4bpp结果；验证probe、1byte短读、读途中媒体失效、CRC/截断/尾随/过多块/超像素拒绝及全部实际分配故障回收。2048×1024原图缩成32×16，在256KiB总池内解码，证明该场景不分配8MiB整图RGBA。它不能证明任意图片安全、取消时间和设备吞吐。

epub_capture测试含自有PNG的真实EPUB页及PNG坏CRC不输出文件；现有书架/字体/阅读回归继续运行。可在开发容器/原生Linux使用：

```bash
cmake --build build-host --target png_dump epub_capture
./build-host/png_dump input.png --probe
./build-host/png_dump input.png 248 454 > output.pgm
./build-host/epub_capture mockDoc/绍宋.epub mockDoc/LXGWWenKai-Regular.ttf 4 1 44 build-dev/epub-sample/novel-png-page1.pgm
./build-host/epub_capture mockDoc/绍宋.epub mockDoc/LXGWWenKai-Regular.ttf 4 2 44 build-dev/epub-sample/novel-png-page2.pgm
```

png_dump必须最终退出0才能使用stdout；error时无图像输出，诊断在stderr。所有本机用户样本和产物被Git忽略，原书和字体不变。共享逻辑pool峰值不等于系统/BSP总PSRAM，截图不代表面板或功耗实测。
