"""同源窗口的TF分页、完整像素和缓存退路。 / TF pagination, exact pixels and cache fallbacks through the shared window controller."""
import hashlib
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from make_sim_library import cover_png


with tempfile.TemporaryDirectory(prefix="pn-tf-window-") as directory:
    root = pathlib.Path(directory)
    for kind in ("txt", "epub", "image"):
        books = root / kind
        books.mkdir()
        text = "Exact glyphs, original positions and verified reading caches remain available. " * 600
        source = books / ("book.txt" if kind == "txt" else "book.epub")
        if kind == "txt":
            source.write_text(text, encoding="utf-8")
        else:
            with zipfile.ZipFile(source, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
                archive.writestr("META-INF/container.xml", '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
                archive.writestr("book.opf", '<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="u"><metadata><d:title>TF cache test</d:title><d:identifier id="u">tf-cache-test</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="b" href="b.xhtml" media-type="application/xhtml+xml"/><item id="i" href="image.png" media-type="image/png"/></manifest><spine><itemref idref="a"/><itemref idref="b"/></spine></package>')
                first = '<p><img src="image.png"/></p>' if kind == "image" else '<p>' + text + '</p>'
                archive.writestr("a.xhtml", '<html xmlns="http://www.w3.org/1999/xhtml"><body>' + first + '</body></html>')
                archive.writestr("b.xhtml", '<html xmlns="http://www.w3.org/1999/xhtml"><body><p>' + text + '</p></body></html>')
                archive.writestr("image.png", cover_png())
        original = hashlib.sha256(source.read_bytes()).digest()
        cache = root / (kind + "-tf")
        state = root / (kind + "-state")

        def run(name, script, reading_cache, state_dir):
            frames = root / (kind + "-" + name)
            frames.mkdir()
            env = dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_CLOCK="12:34", PN_SIM_CAPTURE_READING="1", PN_SIM_FRAME_DIR=str(frames), PN_SIM_LIBRARY_SCRIPT="library,enter," + script + "home,wait:2200,quit")
            env.pop("PN_READING_CACHE", None)
            if reading_cache is not None:
                env["PN_READING_CACHE"] = str(reading_cache)
            result = subprocess.run([sys.argv[1], "--library", str(books), "--state-dir", str(state_dir)], env=env, capture_output=True, text=True, timeout=90)
            assert result.returncode == 0 and "used=0 live=0" in result.stdout, (result.stdout, result.stderr)
            assert "library_return status=0" in result.stdout and "shelf_open_failed" not in result.stdout, (result.stdout, result.stderr)
            assert all(value in ("0", "2") for value in re.findall(r"library_turn pointer=\d+ status=(\d+)", result.stdout)), result.stdout
            numbers = [int(value) for value in re.findall(r"reading_frame=(\d+)", result.stdout)]
            return [(frames / f"frame-{number:04d}.pgm").read_bytes() for number in numbers], result.stdout

        # 同一程序关闭/开启TF的策略对照；独立旧版对照在本机样本工具另做。/ Same-program TF-off/on strategy comparison; independent old-version comparisons use local corpus tooling separately.
        reference, _ = run("reference", "next,next,previous,", None, root / (kind + "-reference-state"))
        cold, output = run("cold", "next,next,previous,", cache, state)
        assert len(reference) == len(cold) == 4 and cold == reference, (kind, len(reference), len(cold), output)
        assert list((cache / "pages").glob("*.pni")) and list((state / "indexes").glob("*.pnrt")), output
        expected = [reference[1], reference[0], reference[1]]
        warm, output = run("warm", "previous,next,", cache, state)
        assert warm == expected, (kind, "warm", len(warm), output)
        for path in (cache / "pages").glob("*.pni"):
            data = bytearray(path.read_bytes()); data[-1] ^= 1; path.write_bytes(data)
        corrupt, output = run("corrupt", "previous,next,", cache, state)
        assert corrupt == expected, (kind, "corrupt", output)
        assert cache.resolve().is_relative_to(root.resolve())
        shutil.rmtree(cache)
        absent, output = run("absent", "previous,next,", cache, state)
        assert absent == expected, (kind, "absent", output)
        blocked = root / (kind + "-blocked")
        blocked.write_bytes(b"not a directory")
        unavailable, output = run("unavailable", "previous,next,", blocked, state)
        assert unavailable == expected and blocked.read_bytes() == b"not a directory", (kind, "unavailable", output)
        assert hashlib.sha256(source.read_bytes()).digest() == original
        print(kind, "TF index: cold/reopen/PREV/NEXT, corrupt/deleted/unwritable caches, 16 exact frame comparisons, untouched originals and zero pool leaks passed")
