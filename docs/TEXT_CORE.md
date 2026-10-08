# TXT解码、源映射与分页维护契约

0.0.5新增共享C的pn_text组件：严格解码、编码验证、有界页节点和租约文件源。0.0.6已在静态捕获入口连接真实字体栅格（见 [字体port](FONT_PORT.md)），0.0.7/0.0.8已连接共享阅读session与设备触摸TXT入口（见 [设备接入](DEVICE_PORT.md)），书架/目录和完整产品流程仍待完成；本阶段不是第一版完成。

## 解码与身份位置

source提供不变size、read_at和可选validate。reader仅含256-byte缓存，不整书加载。read_at可短读；文件源用stdio读取，错误返回明确状态；before-size的零读视为IO。next输出Unicode标量及原文件begin/end偏移，UTF8续字节、过长序列、代理区和越界标量报CORRUPT；UTF16拒绝奇数长度、无配对代理，支持LE/BE及补充平面。

BOM先于编码，显式编码与BOM冲突拒绝。open(AUTO)只识别BOM，否则选UTF8，不把局部有效字节当全书识别。probe在source.size不超过byte_limit时完整严格验证：BOM编码固定，否则先UTF8，失败再GBK；两者皆错返回CORRUPT，不静默替换。该上限约束源大小，回退时可读两遍；当前probe同步，导入后台队列/取消deadline待接入，不可阻塞UI。

CRLF合为LF且源范围包含两字节（UTF16为四字节），单CR也归一成LF，LF保留。next错误/EOF不推进cursor或更改输出；缓存可被读取操作更新。seek从content_begin严格扫描到目标，拒绝字符内部/代理尾部/CRLF内部，错误不改变reader。当前seek是O(offset)，还没有checkpoint索引，不能宣称大书续读性能达标。

GBK双字节映射由tools/gen_gbk.py用Python严格gbk codec生成24066槽uint16表，定义21791个双字节映射；ASCII单字节直通。拒绝未定义编码、0x7f尾字节、GB18030四字节，不承诺微软CP936单字节Euro扩展。生成表48,132-byte rodata；不手改，host自动check并逐个编码对照Python解码结果。

## 文件源寿命

pn_text_file_t首次使用前清零，不可移动；open后重复open返回BUSY。它不挂载/格式化，不创建/写文件。调用方在真实挂载后取得READ/WRITE租约，保持至所有reader停止并close后，再release；旧代也执行fclose并清空句柄。最大INT32_MAX源字节，避免目标32-bit fseek溢出，超过返回LIMIT。

每次decoder open/next/seek调用可选validate；文件port即使缓存命中/EOF也验证当前租约，避免换卡后继续消费旧缓存。实际拔卡仍需介质owner检测detach；没有硬件拔卡监听时纯软件验证不能感知物理卡变化。文件I/O前后还校验租约。源不可并发修改/替换；close前必须停止所有reader，不能复用file对象让旧reader读新文件。

## 页节点

pn_text_paginate接收reader、正文区域、行距/基线/段首缩进/段距及字体advance回调。advance用26.6固定点，度量与栅格分离；低层分页器只调用度量回调，真实字体绘制已由后续reader_app接入；基本布局测试使用明确模拟度量，实际捕获使用TTF度量，均不能替代真机性能。

页glyph缓冲由调用方提供，最多4096项；函数不分配。每项保留Unicode/source range/x/baseline/advance，换行也留节点，绘制者跳过LF。width/height上限4096、单个字形超过区域或缓冲不足返回LIMIT，不偷偷截断；失败清valid/count且不消费原reader，缓冲内部可有不再有效的中间结果。

当前支持左对齐、按字体度量换行、空白处词回退、超长词字符边界拆分、段首缩进/段距和基本开闭标点禁则。窄至无法容纳成对字符时允许拆分但不丢字。页begin/end和next_paragraph_start可继续构建下页，页码不进入持久进度。

尚需BlockStream语义段落/标题、自动段落模式、两端有限伸缩、完整语言禁则/成对省略号数字单位、widow/orphan、字体fallback/kerning/变化轴、图片、layout_key/checkpoints与取消/工作时间片。不能把当前基础换行当成完整RENDERING实现。

## 复跑

```powershell
python tools/dev.py host
python tools/dev.py sim
python tools/dev.py firmware-ci
python tools/dev.py firmware-board
python tools/dev.py docs
python tools/gen_gbk.py --check
```

host对照随机Unicode及补充平面UTF8/UTF16LEBE、原位置/BOM/换行和所有GBK已定义双字节；固定测试含单字节短读、非法编码与边界、probe预算、错误不推进。分页200组布局/文本连续性检查每个字符和原始偏移；另测词回退、中文行首标点、容量/超宽失败。文件源用真实临时TXT测试和软件epoch变化，含缓存/EOF状态的租约失效。全部运行ASan/UBSan；两套固件编译同源组件，不等于真机阅读验证。
