# 图像来源与提示词

工具：内置 image_gen；不是CLI/API回退。原图保留，不覆盖既有素材。首选概念图由本次聊天生成并由用户选择第一张；后续由用户明确要求拆PNG、定684×1216尺寸与16阶，所以使用Pillow执行确定性的裁切、整数缩放、灰阶量化与界面排版。

## 熊猫帧原始生成提示词

Create ONLY an isolated game character PNG spritesheet on genuine transparent background, no UI, no text, no labels, no grid lines. Reference image is approved PicoNano Sudoku design; preserve its seated SIDE PROFILE panda facing RIGHT with rounded head, black ears eye patch back and limbs, white face and belly, holding a small reading tablet/book with pencil. Transform this exact character identity into refined restrained MONOCHROME PIXEL ART with clean square stepped edges, no ink splatters, no bamboo scenery. This is an animation production sheet. Canvas square 1024x1024, EXACT 3 columns x 2 rows equally sized cells, exactly SIX pandas, one per cell centered with generous transparent padding. Every panda identical apparent scale, fixed body, identical feet baseline within each cell, no resizing between frames. Keep tablet fixed in same place, only head/eyelids or one paw changes slightly. TOP ROW from left to right: neutral reading eyes open; same pose eyes closed blinking; same pose head lifted slightly looking toward viewer. BOTTOM ROW left to right: same seated panda one paw near chin thinking; same seated panda left paw lifted halfway in celebration; same seated panda left paw raised higher in a quiet wave, tablet resting on lap. Black white and few neutral gray shades only, no color. No colored cheeks, no stars, no captions, no frame borders, no checkerboard painted into pixels. Pixel-art around logical48x48 per character enlarged with nearest-neighbor look. Retain white opaque interiors of head and belly; only outside silhouettes transparent. Six complete uncropped characters arranged as specified.

输入参考：`approved-concept.png`。实际生成尺寸与请求可能不同，最终以拆分脚本和manifest记录为准。

## 远山原始生成提示词

Produce a SINGLE standalone UI background PNG asset extracted/recreated from the pale Chinese ink mountain header in the supplied approved PicoNano Sudoku mockup. ONLY pale distant mountains and a hint of still lake mist, all on PURE WHITE background. NO panda NO bamboo NO text NO logo NO UI NO borders. Very wide landscape composition about 4:1 width:height, mountain ridge center in upper half, bottom fades completely to white, left quarter almost pure white for overlaid UI title. Right side has the most detailed peak. Grayscale only, extremely pale G12..G15 appearance with maximum dark ink approximately #bbbbbb, elegant traditional Chinese ink wash like the approved image. Keep edges and outside blank white, no gray rectangle backdrop, no paper texture. Intended final export is a684x160 pixel e-ink header, keep broad shapes readable when reduced. This is an isolated material, not another UI mockup.

输入参考：`approved-concept.png`。最终按比例缩放并右对齐到684×160纯白画布。

## 后处理

`build_design.py`保留源引用、裁切坐标、字体路径、布局与量化公式。仅生成素材需要ImageGen；重新排版和导出不再调用模型。所有文字、棋盘与按钮采用确定性绘制，题面及通关答案来自相同的已验证中等题记录。
