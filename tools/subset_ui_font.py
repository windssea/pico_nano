"""生成常驻中文UI字体子集。 / Generate resident Chinese UI font subset."""
import argparse
import hashlib
import json
import pathlib
from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
ROOT = pathlib.Path(__file__).resolve().parents[1]
SHA = "a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da"
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("--replay", action="store_true", help="Use stored character set and fontTools version")
    args = parser.parse_args()
    if hashlib.sha256(args.input.read_bytes()).hexdigest() != SHA:
        raise SystemExit("Unexpected source font hash")
    paths = list((ROOT / "docs").glob("*.md")) + list((ROOT / "main").rglob("*.c")) + list((ROOT / "tests/fixtures").glob("*.txt"))
    paths += list((ROOT / "components/pn_ui").rglob("*.c")) + list((ROOT / "components/pn_shell").rglob("*.c")) + list((ROOT / "components/pn_library").rglob("*.c")) + list((ROOT / "components/pn_reader").rglob("*.c"))
    text = "".join(p.read_text(encoding="utf8") for p in sorted(paths))
    codes = sorted(set(range(32, 127)) | {ord(c) for c in text if ord(c) > 127})
    if args.replay:
        stored = json.loads((ROOT / "LICENSES/ui-font-manifest.json").read_text(encoding="utf8"))
        if stored["fonttools_version"] != __import__("fontTools").__version__:
            raise SystemExit("Use the stored fontTools version to reproduce the font")
        codes = stored["unicode_requested"]
    font = TTFont(args.input, recalcTimestamp=False)
    font = instantiateVariableFont(font, {"wght": 400}, inplace=True)
    options = subset.Options()
    options.name_IDs = [0, 1, 2, 3, 4, 5, 6]
    worker = subset.Subsetter(options=options)
    worker.populate(unicodes=codes)
    worker.subset(font)
    names = {1: "Read Pico UI", 2: "Regular", 3: "ReadPicoUI-1", 4: "Read Pico UI Regular", 6: "ReadPicoUI-Regular"}
    for record in font["name"].names:
        if record.nameID in names:
            record.string = names[record.nameID].encode(record.getEncoding())
    destination = ROOT / "assets/fonts/read-pico-ui.ttf"
    font.recalcTimestamp = False
    font.save(destination)
    manifest = {"source": "https://github.com/google/fonts/tree/main/ofl/notosanssc", "source_sha256": SHA,
                "ofl_sha256": "1c05c68c34f9708415aada51f17e1b0092d2cea709bf4a94cd38114f9e73d7d9", "license": "OFL-1.1", "weight": 400, "fonttools_version": __import__("fontTools").__version__,
                "unicode_requested": codes, "unicode_present": sorted(font.getBestCmap()),
                "output_sha256": hashlib.sha256(destination.read_bytes()).hexdigest()}
    (ROOT / "LICENSES/ui-font-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf8")
    print(f"UI font: {len(font.getBestCmap())} glyph mappings, {destination.stat().st_size} bytes")
if __name__ == "__main__":
    main()
