"""生成三字形字体回退测试样本，保留OFL并修改家族名。 / Generate a three-glyph fallback fixture, retaining OFL and renaming the family."""
import argparse
import hashlib
import json
from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from subset_ui_font import ROOT, SHA

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input")
    args = parser.parse_args()
    source = __import__("pathlib").Path(args.input)
    if hashlib.sha256(source.read_bytes()).hexdigest() != SHA:
        raise SystemExit("Unexpected source font hash")
    font = TTFont(source, recalcTimestamp=False)
    font = instantiateVariableFont(font, {"wght": 400}, inplace=True)
    options = subset.Options()
    options.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14]
    worker = subset.Subsetter(options=options)
    worker.populate(unicodes=[0x41, 0x7BC7, 0x9F98])
    worker.subset(font)
    names = {1: "Read Pico Fallback Fixture", 2: "Regular", 3: "ReadPicoFallbackFixture-1", 4: "Read Pico Fallback Fixture Regular", 6: "ReadPicoFallbackFixture-Regular"}
    for record in font["name"].names:
        if record.nameID in names:
            record.string = names[record.nameID].encode(record.getEncoding())
    font.recalcTimestamp = False
    destination = ROOT / "tests/fixtures/font-fallback.ttf"
    font.save(destination)
    manifest = {"source_sha256": SHA, "license": "OFL-1.1", "weight": 400,
                "fonttools_version": __import__("fontTools").__version__, "unicode_present": sorted(font.getBestCmap()),
                "output_sha256": hashlib.sha256(destination.read_bytes()).hexdigest()}
    (ROOT / "LICENSES/font-fixture-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf8")
    print(f"Fallback fixture: {destination.stat().st_size} bytes, {manifest['unicode_present']}")

if __name__ == "__main__":
    main()
