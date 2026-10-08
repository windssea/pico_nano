# 扫码传书

设备热点传输页在READY时显示两枚二维码：先扫左侧连接WiFi热点，再扫右侧打开传书网页，最后输入页面显示的六位配对码。文字名称、口令与地址保留，扫码不成功时可手动操作。二维码不包含授权令牌；停止和失败页面不展示有效二维码。

WiFi码使用 `WIFI:T:WPA;S:…;P:…;H:false;;`，名称/口令中的反斜杠、分号、逗号、冒号和双引号转义，中文名称保持UTF8。[ZXing载荷说明](https://github.com/zxing/zxing/wiki/Barcode-Contents#wi-fi-network-config-android-ios-11)定义这一格式。

使用固定[Nayuki v1.8.0 C库](https://github.com/nayuki/QR-Code-generator/tree/v1.8.0/c)与原始MIT声明，源摘要由check_docs核对。wrapper不分配堆：最大版本10、最大输入240字节、至少M纠错、UTF8 ECI 26、四模块白色静区和整数像素黑白模块；超出预算或无效UTF8拒绝，不绘制半个矩阵。绘图不访问网络、TF或设置。

host的qr检查转义、容量/输入拒绝和真实矩阵，transfer_view检查实际页面。以下命令在已完成host构建后生成演练捕获：

```powershell
docker --context default run --rm --mount type=bind,source=D:\windssea\dev\pico_nano,target=/work -w /work espressif/idf:v6.1@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c build-host/test_transfer_view build-dev/transfer-qr-view.pgm
docker --context default run --rm --mount type=bind,source=D:\windssea\dev\pico_nano,target=/work -w /work espressif/idf:v6.1@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c build-host/test_qr build-dev/qr-pair.pgm
python tools/test_qr_decode.py build-dev/transfer-qr-view.pgm build-dev/qr-pair.pgm
```

独立OpenCV检查解码后的两段内容与演练值完全一致，包括中文SSID。OpenCV当前实现可能输出ECI支持提示；这次UTF8内容的准确比较通过，仍不能替代Android/iOS相机、手机WiFi加入与真实墨水屏扫码测试。上述default context只适用于当前本机可用引擎，其他环境按Docker context ls选择，不能盲目重启引擎。
