"""生成内置默认阅读黑体，不改界面字库。 / Generate the resident reading sans without changing the UI font."""
import argparse
import hashlib
import json
import pathlib
from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
ROOT = pathlib.Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("--replay", action="store_true")
    parser.add_argument("--extra-text", type=pathlib.Path, help="UTF-8 coverage characters to include without embedding their source files")
    args = parser.parse_args()
    ui = json.loads((ROOT / "LICENSES/ui-font-manifest.json").read_text(encoding="utf8"))
    if hashlib.sha256(args.input.read_bytes()).hexdigest() != ui["source_sha256"]:
        raise SystemExit("Unexpected source font hash")
    codes = set(ui["unicode_present"]) | set(range(32, 127)) | set(range(0x3000, 0x3040)) | set(range(0xff00, 0xfff0))
    for first in range(0xa1, 0xf8):
        for second in range(0xa1, 0xff):
            try:
                codes.update(map(ord, bytes((first, second)).decode("gb2312")))
            except UnicodeDecodeError:
                pass
    manifest_path = ROOT / "LICENSES/reading-font-manifest.json"
    if args.extra_text:
        if args.replay:
            parser.error("--extra-text cannot be combined with --replay")
        codes.update(map(ord, args.extra_text.read_text(encoding="utf-8")))
    if args.replay:
        stored = json.loads(manifest_path.read_text(encoding="utf8"))
        if stored["fonttools_version"] != __import__("fontTools").__version__:
            raise SystemExit("Use the stored fontTools version")
        codes = set(stored["unicode_requested"])
    font = TTFont(args.input, recalcTimestamp=False)
    font = instantiateVariableFont(font, {"wght": 400}, inplace=True)
    options = subset.Options()
    options.name_IDs = [0, 1, 2, 3, 4, 5, 6]
    worker = subset.Subsetter(options=options)
    worker.populate(unicodes=sorted(codes))
    worker.subset(font)
    names = {1: "Read Pico Reading Sans", 2: "Regular", 3: "ReadPicoReadingSans-1", 4: "Read Pico Reading Sans Regular", 6: "ReadPicoReadingSans-Regular"}
    for record in font["name"].names:
        if record.nameID in names:
            record.string = names[record.nameID].encode(record.getEncoding())
    destination = ROOT / "assets/fonts/read-pico-reading.ttf"
    font.recalcTimestamp = False
    font.save(destination)
    manifest = {key: ui[key] for key in ("source", "source_sha256", "ofl_sha256", "license", "weight")}
    coverage = stored["coverage"] if args.replay else "GB2312 + UI repertoire + CJK/fullwidth punctuation" + (" + supplemental repertoire" if args.extra_text else "")
    manifest.update(fonttools_version=__import__("fontTools").__version__, coverage=coverage, unicode_requested=sorted(codes), unicode_present=sorted(font.getBestCmap()), output_sha256=hashlib.sha256(destination.read_bytes()).hexdigest())
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf8")
    print(f"Reading font: {len(font.getBestCmap())} mappings, {destination.stat().st_size} bytes")

if __name__ == "__main__":
    main()
