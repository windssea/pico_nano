# 大封面回归图

`jpeg-cover-large.jpg` 是原创单色图，CC0-1.0；不含小说封面或外部图片。用于验证480章EPUB及1092×1533渐进式JPEG首帧在模拟器默认预算内可打开。

生成：Pillow 12.0.0，`Image.new('RGB', (1092, 1533), (90, 130, 180)).save(path, progressive=True, quality=80, subsampling=2)`。二进制是必要的可复跑回归fixture，生成结果不是产品壁纸。
