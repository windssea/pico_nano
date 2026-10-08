# 字体引擎与真实TXT捕获

0.0.6加入同源FreeType字体port和常驻中文UI子集，PC模拟器可从实际TXT生成灰阶阅读页。0.0.8已将真实字体/会话接到设备TXT入口（见 [设备接入](DEVICE_PORT.md)），仍未上板及接完整产品流程。这是第一版的中间实现，不是功能验收完成。

## 来源与许可

FreeType固定2.14.3，采用官方镜像VER-2-14-3归档，SHA-256为dc49de6b01a266eef4876a4dd34d9842c475d3e28ff2eff63bd2fb760ab56261。完整vendor未修改，744文件摘要在LICENSES/freetype-manifest.json。选择FreeType License（FTL），保留vendor/LICENSE.TXT与docs/FTL.TXT及各文件附带许可；产品资料必须保留归属说明。配置在vendor外，仅注册TT/sfnt/psnames/smooth/raster；禁用PNG/zlib/Brotli/BZIP2/HarfBuzz/SVG/环境变量路径。

Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved.

常驻字体源为Google Fonts Noto Sans SC，可再发行许可见assets/fonts/OFL.txt。原字体摘要a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da。tools/subset_ui_font.py固定wght=400，取公开设计文档/当前UI源码/自有TXT样例字符，修改子集家族名为Read Pico UI，保留原版权与OFL；输出摘要、工具版本和字符集在LICENSES/ui-font-manifest.json。当前字符映射与字节数见上述manifest和生成输出；这不是完整中文正文fallback。

本字体子集及其衍生字体仍为SIL OFL 1.1，不随项目代码改为Apache。生成时需要fontTools，运行/编译只用已固定的TTF及Python嵌入脚本。--replay使用manifest记录的字符集和fontTools版本，关闭生成时间重写；当前重复生成摘要一致。字体源不常驻PSRAM，构建时嵌入只读字节；大小计入产品flash预算，不能因为当前未用符号被链接器删除而当作零成本。

## 内存与源契约

pn_font_open只接受单TTF的TrueType glyf源，最大32MiB，Unicode cmap；CFF/OTF/TTC/WOFF不接受。source采用与文本同一read_at/validate契约，字体懒读，禁止并发修改或关闭。对象初始impl=NULL，owner串行调用；pool/源寿命覆盖字体对象，先close引擎再关闭源/释放介质租约。

FT_New_Library使用自定义FT_Memory，alloc/free/realloc全部走pn_pool；realloc失败保留旧指针，错误路径释放face/library/engine。模块建立时内存失败也中止，不容许不完整注册伪装成功。字体调用前验证源，缓存命中也可拒绝旧epoch；引擎stream短读/错误被传回status。没有默认无界malloc回退。

打开/尺寸操作/字体查询/光栅目前同步，没有RTOS任务、取消deadline或stack水位验证；上板前需要独立font/reader worker栈与调度。FreeType解析仍不是安全沙箱，仅有预算不能证明任意恶意字体安全；上传结构预检、长时间fuzz、复杂轮廓限制/超时和使用中替换流程尚未完成。

## 度量与绘制

pn_font_advance返回26.6像素advance，不光栅化整章。Tab使用4个空格宽度；调用方处理换行。pn_font_vertical返回当前ascender/descender，分页line_height不能小于它们之和。尺寸8–128px是port限额，产品正文UI仍按28–72px设计。

pn_font_draw在目标像素高度直接光栅化，x保留26.6相位，baseline按像素对齐；使用覆盖率写入4bpp，不缩放整页。gray为线性覆盖（gamma=1候选），binary阈值128仅落黑白，不能把灰阶边缘送DU。裁切用64-bit坐标防溢出，不改邻像素；超大/未知位图形式明确拒绝。当前只缓存FreeType内部当前glyph slot，没有1MiB LRU，热翻页性能仍未验证。

缺字返回EMPTY供上层fallback。捕获入口以方框显示缺字并报告数量，不能把UI子集当成全中文字库。缺字退路目前只有这一预览方框，完整用户fallback链/缺字提示、可变字重轴选择、kerning、位图gamma校准、字体安装/删除/替换仍需实现。

## PC真实TXT捕获

```bash
./build-sim-idf/pn_sim --headless --book tests/fixtures/reader_sample.txt --capture build-sim/artifacts/reader.pgm
./build-sim-idf/pn_sim --headless --book /path/book.txt --font /path/font.ttf --page 2 --size 56 --capture build-sim/artifacts/page2.pgm
```

普通模式默认1MiB pool。捕获复用同源decoder/layout/FreeType/framebuffer及display所有权；每个文件使用独立READ租约，结束时关字体/源/租约和glyph缓冲。页面从开头顺序计算，页号是真正构建的第N页但总页数未知，当前定位不是大书快速索引。默认左对齐、44px、段距及原换行；没有段落合并/目录/设置持久化。标题/页脚是预览，不能冒充现代产品UI。

--page为1–10000，--size为28–72；--book不可与ownership合用，--font需要book。--budget不足时失败并回收，不写出新捕获文件。缺失/错误文件、越过末页、字体失败都返回非零。0.0.7的--book非headless窗口已接交互会话（见 [阅读会话](READER_SESSION.md)）；此节静态--capture入口仍不写位置。书架尚未实现。

自有reader_sample.txt截图由C代码生成；主机例程峰值约613KB（1MiB上限，包括分配头）并回收used/live=0。另以17,772,300-byte完整Noto Sans SC变量TTF的默认实例做实际源捕获，主机同一1MiB pool峰值745244 bytes，无缺字且结束归零；该单样本不是全部字体安全/变体轴验收。它不是ESP32/S3整机PSRAM或扫描带宽基线；内置字体rodata、系统栈、BSP缓冲和主机后端另计。

## 验证与剩余接线

```powershell
python tools/dev.py host
python tools/dev.py sim
python tools/dev.py firmware-ci
python tools/dev.py firmware-board
python tools/dev.py docs
```

host新增font测试：真实中文/拉丁度量、灰阶边缘、二值像素、极端坐标裁切、缺字、改字号、预算不足及每个观测到的分配点故障回收；FreeType也启用ASan/UBSan。sim检查真实TXT两页原位置相邻、图像差异、灰阶文字与不足预算释放。依赖/字体摘要纳入docs验证。

共享阅读器、原位置、显示确认、书架和书签界面已接入；完整字体管理/回退、锁屏壁纸和传输仍待完成，截图不替代产品验收。设备端尚无字体实测/阅读运行，未烧写。

书签名称已支持常驻字库缺字时，回退到会话已选正文TTF的独立36px引擎；不改变正文大小，不替换按钮字体。未选择完整字体/回退打开失败仍显示方框，书架及正文的完整用户字体回退链仍待完善。测试小字体只含A和单个中文字形，OFL来源与摘要见tests/fixtures/FONT_LICENSE.md及LICENSES/font-fixture-manifest.json；不能作为产品字库。

## 本机用户字体测试

mockDoc中的字体和小说保持原样且不随Git/CI分发。安装了fontTools的本机Python可生成字符覆盖报告及probe输入（build-dev被忽略）：

```powershell
python tools/inspect_font_sample.py "mockDoc/LXGWWenKai-Regular.ttf" --epub "mockDoc/绍宋.epub"
```

在已挂载工作区为/work的开发容器，或原生Linux根目录运行：

```bash
cmake --build build-host --target font_probe
./build-host/font_probe mockDoc/LXGWWenKai-Regular.ttf build-dev/font-sample/codepoints.txt
./build-sim-idf/pn_sim --headless --book tests/fixtures/reader_sample.txt --font mockDoc/LXGWWenKai-Regular.ttf --size 44 --capture build-dev/font-sample/reader-44.pgm
```

font_probe使用1MiB池和真实file/source租约，对指定字符集逐个在28/32/44/56/72px运行同源字体度量、灰阶及二值光栅；抽查A/中及每轮首字符的像素范围，报垂直度量、缺字、峰值及关闭后used/live。它是手动样本测试工具，不在CI自动依赖用户字体。全字符字形可以成功不代表书中所有生僻字都存在：覆盖报告将EPUB中可解析body的非空白字符与cmap比较，报告缺字，不解读CSS字体替换或图片文字。

以上检查不安装字体、不生成产品字体子集，也不测试GPOS/GSUB复杂排版、可变轴、真实TF吞吐、面板灰阶和刷新耗时。静态阅读捕获可确认基本中文排版；字体管理及完整正文备用字库仍需接入。

正文应用自0.0.33使用[字体回退链](FONT_FALLBACK.md)，PNG/JPEG前关闭备用引擎；低层pn_font仍只处理一个字体，缺字返回EMPTY，所有其他错误保持。
