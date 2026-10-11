"""逐本验证本地书库打开、翻页与返回，原书只读。 / Check opening, turning and returning for each local book without modifying originals."""
import argparse
import os
import pathlib
import shutil
import subprocess
import tempfile


def check(binary, books, font):
    samples = sorted(p for p in books.iterdir() if p.suffix.lower() in (".txt", ".epub"))
    if not samples:
        raise ValueError("No TXT/EPUB samples")
    with tempfile.TemporaryDirectory() as folder:
        root = pathlib.Path(folder)
        for index, sample in enumerate(samples):
            library = root / str(index)
            library.mkdir()
            shutil.copy2(sample, library / sample.name)
            result = subprocess.run([str(binary.resolve()), "--library", str(library), "--font", str(font.resolve()), "--state-dir", str(root / f"state-{index}")],
                env=dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_LIBRARY_SCRIPT="tap:100:500,next,back,quit"),
                capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, (sample.name, result.stdout, result.stderr)
            for expected in (f"library_open name={sample.name}", "library_turn pointer=0 status=0", "library_return status=0", "used=0 live=0"):
                assert expected in result.stdout, (sample.name, expected, result.stdout, result.stderr)
            print(f"PASS {sample.name}", flush=True)
    print(f"{len(samples)} books passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("books", type=pathlib.Path)
    parser.add_argument("font", type=pathlib.Path)
    args = parser.parse_args()
    check(args.binary, args.books, args.font)
