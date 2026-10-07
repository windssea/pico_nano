# 回退测试字体

font-fallback.ttf只包含A和“篇”的测试字形，由与常驻UI字体相同的固定Noto Sans SC源生成，家族名改为Read Pico Fallback Fixture。原版权信息保留在字体name表中，许可仍为[SIL OFL 1.1](../../assets/fonts/OFL.txt)。它不是正文或产品字体，不代表完整中文字库。

原源摘要、生成参数和产物摘要见[字体样本清单](../../LICENSES/font-fixture-manifest.json)。复现：`python tools/subset_fallback_fixture.py SOURCE.ttf`，SOURCE摘要必须匹配固定源；使用清单中的fontTools版本。没有原源时测试/编译直接使用该小样本，不需要网络或fontTools。
