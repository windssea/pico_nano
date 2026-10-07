# JPEG原生解码层

新增pn_image/pn_jpeg，使用固定libjpeg-turbo3.1.4.1官方源码包。host与ESP32-S3编译同一通用C解码路径，无SIMD/编码器/TurboJPEG接口。原包633文件、许可证和包装源码保持原样，SHA见LICENSES/jpeg-manifest.json；配置与系统内存适配位于vendor外。

This software is based in part on the work of the Independent JPEG Group.

## 边界与内存

支持8位灰度/RGB/YCbCr、基线及渐进式Huffman JPEG。尺寸最多8192×8192且总像素最多16Mi，输入最多32MiB，渐进式最多128扫描。CMYK/YCCK、算术、高精度和无损JPEG明确UNSUPPORTED；不解析EXIF旋转、ICC/gamma，不执行网络请求，不安装图片。

pn_jpeg_probe借pool解析JPEG头，不验证整个图片/源EOF。pn_jpeg_draw使用一个RGB扫描行，最近邻缩放与整数灰阶量化直接写4bpp frame；目标1..4096px，可裁切。不会保存整图RGB。渐进式所需DCT系数仍可能较大，不能把“逐行输出”当作“渐进式内存与高度无关”。libjpeg内存后端的small/large和系数虚拟数组均受pool约束，无默认malloc和磁盘backing store。预算不足NO_MEMORY；库内部可能尝试较小分配，只要仍在pool预算内可成功，故障测试核对成功时像素不变或失败时无输出，均须零剩余分配。

不伪造EOI，截断/恢复警告视为错误；完成解码后必须EOI、无尾随字节且源返回已校验EMPTY。文件/ZIP源的IO、介质失效或取消错误保留。info仅完整成功更新。失败可能已写部分frame，调用方必须丢弃，不能推屏或保存进度。setjmp状态在pool对象中，错误后销毁codec、扫描行与scope并回收全部分配。

JPEG不自带CRC；完整EOF不证明文件没有任意像素变化，EPUB应额外要求ZIP资源CRC。字节/像素/扫描限额尚不构成RTOS分片或CPU deadline保证；长恶意文件/模糊测试、真机时间和资源验收仍需完成。

## 当前验证与未完集成

jpeg_oracle使用自有RGB渐变基线/渐进式JPEG，与独立Pillow解码灰阶矩阵逐像素比较；还检验1byte短读、源失效、截断、尾随数据、每个观察到的分配故障及低预算，结束used/live均为0。测试矩阵存tests/fixtures/jpeg-samples.json，不依赖CI安装Pillow；用户小说/字体不入库。

《绍宋》封面是1092×1533渐进式。独立JPEG CLI目标620×870在4MiB预算明确失败，在6MiB预算可绘制，pool峰值5427740bytes（含目标frame），结束为零。与本机Pillow JPEG9解码对照，539400目标像素中44像素相差一个4bpp灰阶，其余一致；尚未把这个差异归因为已验证的特定舍入算法，后续仍需细查。未宣称lossless像素还原或真机PSRAM组合峰值验收。

当前JPEG层已接入 [出版物图片路径](IMAGE_RESOURCES.md) 与epub_capture，书架封面缓存、壁纸管理或设备/SDL EPUB交互仍未接入；本版本只能说明原生解码层与资源检查已有实现。已把PNG/JPEG共同探测和绘制接入出版物，下一步继续安排大渐进式封面的专用scratch/缓存和字体/显示内存生命周期，再做目录/续读/书签会话。不能直接把6MiB解码预算叠加到其他所有常驻缓存上，ESP32-S3总8MiB PSRAM仍需要实测与严格整体预算。
