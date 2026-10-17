"""由OFL回退测试字体生成CFF轮廓OTF夹具；只转换轮廓格式，字形、映射与名称保持。
Generate a CFF-outline OTF fixture from the OFL fallback test font; only the outline format changes, keeping glyphs, mapping and names.

用法 / Usage: python tools/make_otf_fixture.py   (需要 fontTools / requires fontTools)
"""
import pathlib

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.t2CharStringPen import T2CharStringPen
from fontTools.ttLib import TTFont

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'tests/fixtures/font-fallback.ttf'
DESTINATION = ROOT / 'tests/fixtures/font-fallback-cff.otf'


def main() -> None:
    source = TTFont(SOURCE)
    order = source.getGlyphOrder()
    glyphs = source.getGlyphSet()
    charstrings = {}
    for name in order:
        pen = T2CharStringPen(glyphs[name].width, glyphs)
        glyphs[name].draw(pen)
        charstrings[name] = pen.getCharString()
    builder = FontBuilder(source['head'].unitsPerEm, isTTF=False)
    builder.setupGlyphOrder(order)
    builder.setupCharacterMap(source.getBestCmap())
    family = source['name'].getDebugName(1) or 'Fallback'
    builder.setupCFF(family.replace(' ', '') + '-CFF', {'FullName': family}, charstrings, {})
    metrics = {name: source['hmtx'][name] for name in order}
    builder.setupHorizontalMetrics(metrics)
    hhea = source['hhea']
    builder.setupHorizontalHeader(ascent=hhea.ascent, descent=hhea.descent)
    builder.setupNameTable({'familyName': family, 'styleName': 'Regular'})
    os2 = source['OS/2']
    builder.setupOS2(sTypoAscender=os2.sTypoAscender, sTypoDescender=os2.sTypoDescender, usWinAscent=os2.usWinAscent, usWinDescent=os2.usWinDescent, usWeightClass=os2.usWeightClass)
    builder.setupPost()
    builder.save(DESTINATION)
    print(DESTINATION.relative_to(ROOT), DESTINATION.stat().st_size)


if __name__ == '__main__':
    main()
