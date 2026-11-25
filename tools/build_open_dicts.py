"""从合法开源词库构建标准 StarDict（.ifo + .idx + DictZip .dict.dz）压缩包，供刷机页下载与 PicoNano 离线查词使用。
Build standard StarDict (.ifo + .idx + RFC 1952 DictZip .dict.dz) ZIP packages from open-source datasets
(ECDICT MIT, chinese-xinhua MIT, CC-CEDICT CC BY-SA 4.0) for the web installer page and PicoNano offline lookup.
"""
import argparse
import csv
import datetime
import gzip
import hashlib
import json
import pathlib
import re
import struct
import urllib.request
import zipfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_CACHE = ROOT / "build-dev" / "dict-sources"
DEFAULT_OUT = ROOT / "assets" / "dicts"

# DictZip 标准块大小（RFC 1952 FEXTRA RA 子字段兼容 16 位长度上限）。
# Standard DictZip chunk length (keeps worst-case compressed chunk within uint16_t).
DICTZIP_CHUNK_LEN = 58315

SOURCES = {
    "ecdict.csv": "https://raw.githubusercontent.com/skywind3000/ECDICT/master/ecdict.csv",
    "ecdict-LICENSE.txt": "https://raw.githubusercontent.com/skywind3000/ECDICT/master/LICENSE",
    "xinhua-word.json": "https://raw.githubusercontent.com/pwxcoo/chinese-xinhua/master/data/word.json",
    "xinhua-idiom.json": "https://raw.githubusercontent.com/pwxcoo/chinese-xinhua/master/data/idiom.json",
    "xinhua-ci.json": "https://raw.githubusercontent.com/pwxcoo/chinese-xinhua/master/data/ci.json",
    "xinhua-LICENSE.txt": "https://raw.githubusercontent.com/pwxcoo/chinese-xinhua/master/LICENSE",
    "cedict_ts.u8.gz": "https://www.mdbg.net/chinese/export/cedict/cedict_1_0_ts_utf-8_mdbg.txt.gz",
}

TAG_LABELS = {
    "zk": "中考",
    "gk": "高考",
    "cet4": "四级",
    "cet6": "六级",
    "ky": "考研",
    "toefl": "托福",
    "ielts": "雅思",
    "gre": "GRE",
}

EXCHANGE_LABELS = {
    "p": "过去式",
    "d": "过去分词",
    "i": "现在分词",
    "3": "第三人称单数",
    "r": "比较级",
    "t": "最高级",
    "s": "复数",
    "0": "原形",
}

TONE_MARKS = {
    "a": "āáǎàa",
    "e": "ēéěèe",
    "i": "īíǐìi",
    "o": "ōóǒòo",
    "u": "ūúǔùu",
    "ü": "ǖǘǚǜü",
    "A": "ĀÁǍÀA",
    "E": "ĒÉĚÈE",
    "I": "ĪÍǏÌI",
    "O": "ŌÓǑÒO",
    "U": "ŪÚǓÙU",
    "Ü": "ǕǗǙǛÜ",
}

CEDICT_LINE = re.compile(r"^(\S+)\s+(\S+)\s+\[([^\]]+)\]\s+/(.+)/\s*$")


def stardict_sort_key(word: str):
    """与 pn_stardict_strcmp() 完全一致的排序键：先 ASCII 忽略大小写，再按 UTF-8 字节序。
    Sort key matching pn_stardict_strcmp(): ASCII case-insensitive first, raw UTF-8 tie-breaker."""
    raw = word.encode("utf-8")
    low = bytes(c + 32 if 65 <= c <= 90 else c for c in raw)
    return (low, raw)


def encode_dictzip(raw: bytes, chunk_len: int = DICTZIP_CHUNK_LEN) -> bytes:
    """将明文 .dict 编码为标准 RFC 1952 DictZip（带 RA 分块索引表，支持随机解压与 gunzip 兼容）。
    Encode raw .dict bytes into a standard RFC 1952 DictZip stream with FEXTRA 'RA' chunk table."""
    chunks = []
    lengths = []
    total = len(raw)
    if total == 0:
        chunks.append(zlib.compress(b"", 9)[2:-4])
        lengths.append(len(chunks[0]))
    else:
        for off in range(0, total, chunk_len):
            part = raw[off:off + chunk_len]
            is_last = (off + chunk_len >= total)
            comp = zlib.compressobj(9, zlib.DEFLATED, -15)
            out = comp.compress(part)
            out += comp.flush(zlib.Z_FINISH if is_last else zlib.Z_SYNC_FLUSH)
            if len(out) > 65535:
                raise ValueError(f"DictZip chunk compressed size overflow: {len(out)}")
            chunks.append(out)
            lengths.append(len(out))

    chcnt = len(lengths)
    if chcnt > 32760:
        raise ValueError(f"Too many DictZip chunks ({chcnt}) for uint16 XLEN")

    ra_payload = struct.pack("<HHH", 1, chunk_len, chcnt) + b"".join(struct.pack("<H", n) for n in lengths)
    extra = b"RA" + struct.pack("<H", len(ra_payload)) + ra_payload
    # GZIP 10 字节头（FEXTRA=0x04，固定 mtime=0 确保构建可复现）。
    # 10-byte GZIP header (FEXTRA=0x04, mtime=0 for deterministic builds).
    header = struct.pack("<BBBBIBB", 0x1F, 0x8B, 0x08, 0x04, 0, 2, 3) + struct.pack("<H", len(extra)) + extra
    trailer = struct.pack("<II", zlib.crc32(raw) & 0xFFFFFFFF, total & 0xFFFFFFFF)
    return header + b"".join(chunks) + trailer


def build_stardict_files(stem: str, bookname: str, entries: dict[str, str], description: str):
    """把词条字典生成标准 StarDict 三件套字节（.ifo、.idx、.dict.dz）。
    Generate StarDict (.ifo, .idx, .dict.dz) bytes from a headword -> definition mapping."""
    ordered = sorted(entries.items(), key=lambda kv: stardict_sort_key(kv[0]))
    dict_parts = []
    idx_parts = []
    offset = 0
    for word, body in ordered:
        wb = word.encode("utf-8")
        if not wb or b"\x00" in wb:
            continue
        db = body.strip().encode("utf-8")
        if not db:
            continue
        idx_parts.append(wb + b"\x00" + struct.pack(">II", offset, len(db)))
        dict_parts.append(db)
        offset += len(db)

    idx_bytes = b"".join(idx_parts)
    dict_raw = b"".join(dict_parts)
    dz_bytes = encode_dictzip(dict_raw)
    wordcount = len(idx_parts)
    ifo_text = (
        "StarDict's dict ifo file\n"
        "version=2.4.2\n"
        f"wordcount={wordcount}\n"
        f"idxfilesize={len(idx_bytes)}\n"
        f"bookname={bookname}\n"
        f"description={description}\n"
        "sametypesequence=m\n"
    )
    return {
        f"{stem}.ifo": ifo_text.encode("utf-8"),
        f"{stem}.idx": idx_bytes,
        f"{stem}.dict.dz": dz_bytes,
    }, wordcount, len(dict_raw)


def write_deterministic_zip(path: pathlib.Path, files: dict[str, bytes]):
    """写出时间戳固定的可复现 ZIP 包（已压缩的 .dict.dz 用 STORE 避免重复压缩，其余用 DEFLATE）。
    Write a deterministic ZIP archive (STORE for pre-compressed .dict.dz, DEFLATE for others)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w") as zf:
        for name in sorted(files.keys()):
            data = files[name]
            compress = zipfile.ZIP_STORED if name.endswith(".dz") else zipfile.ZIP_DEFLATED
            zi = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            zi.compress_type = compress
            zi.external_attr = 0o644 << 16
            zf.writestr(zi, data, compress_type=compress, compresslevel=9 if compress == zipfile.ZIP_DEFLATED else None)


def pinyin_syllable_to_tone(syl: str) -> str:
    """将带数字声调的 CC-CEDICT 拼音音节转换为声调符号（如 zhong1 -> zhōng）。
    Convert a numbered CC-CEDICT pinyin syllable into tone-marked Unicode pinyin."""
    if not syl or syl[-1] not in "12345":
        return syl
    tone = int(syl[-1]) - 1
    body = syl[:-1].replace("u:", "ü").replace("U:", "Ü").replace("v", "ü")
    if tone == 4:
        return body
    lower = body.lower()
    # 标准标调优先级：a/e > ou 中的 o > 最后一个元音。
    # Standard pinyin tone placement: a/e > o in 'ou' > last vowel.
    idx = -1
    for ch in ("a", "e"):
        pos = lower.find(ch)
        if pos >= 0:
            idx = pos
            break
    if idx < 0 and "ou" in lower:
        idx = lower.find("o")
    if idx < 0:
        for pos in range(len(lower) - 1, -1, -1):
            if lower[pos] in "aeiouü":
                idx = pos
                break
    if idx < 0:
        return body
    ch = body[idx]
    marked = TONE_MARKS.get(ch, ch * 5)[tone]
    return body[:idx] + marked + body[idx + 1:]


def format_cedict_pinyin(raw: str) -> str:
    return " ".join(pinyin_syllable_to_tone(tok) for tok in raw.split())


def ensure_sources(cache_dir: pathlib.Path):
    cache_dir.mkdir(parents=True, exist_ok=True)
    for name, url in SOURCES.items():
        target = cache_dir / name
        if target.exists() and target.stat().st_size > 100:
            continue
        print(f"Downloading {name}...", flush=True)
        req = urllib.request.Request(url, headers={"User-Agent": "PicoNano-DictBuilder/1.0"})
        with urllib.request.urlopen(req, timeout=120) as resp:
            target.write_bytes(resp.read())


def build_ecdict(cache_dir: pathlib.Path):
    """构建 ECDICT 英汉常用词典（含全部考纲/牛津/柯林斯/高频/词形变换词条，约 12.4 万词）。
    Build ECDICT English-Chinese core dictionary (~124k exam/Oxford/Collins/frequency/inflected words)."""
    entries: dict[str, str] = {}
    with (cache_dir / "ecdict.csv").open("r", encoding="utf-8", newline="") as fp:
        for row in csv.DictReader(fp):
            word = (row.get("word") or "").strip()
            tr = (row.get("translation") or "").strip()
            if not word or not tr or len(word) > 64:
                continue
            tag = (row.get("tag") or "").strip()
            ox = (row.get("oxford") or "").strip()
            co = (row.get("collins") or "").strip()
            bnc = int(row.get("bnc") or 0)
            frq = int(row.get("frq") or 0)
            ex = (row.get("exchange") or "").strip()
            if not (tag or ox or co or bnc > 0 or frq > 0 or ex):
                continue

            parts = []
            meta = []
            phon = (row.get("phonetic") or "").strip().replace("\u04d9", "\u0259")
            if phon:
                meta.append(f"/{phon}/")
            badges = []
            if ox == "1":
                badges.append("牛津3000")
            if co and co.isdigit() and int(co) > 0:
                badges.append(f"柯林斯{co}★")
            for t in tag.split():
                if t in TAG_LABELS:
                    badges.append(TAG_LABELS[t])
            if badges:
                meta.append("[" + "·".join(badges) + "]")
            if meta:
                parts.append(" ".join(meta))

            tr_clean = tr.replace("\\n", "\n").replace("\\r", "").strip()
            parts.append(tr_clean)

            if ex:
                forms = []
                for item in ex.split("/"):
                    if ":" not in item:
                        continue
                    k, v = item.split(":", 1)
                    label = EXCHANGE_LABELS.get(k)
                    if label and v and v != word:
                        forms.append(f"{label}:{v}")
                if forms:
                    parts.append("词形：" + " / ".join(forms[:6]))

            body = "\n".join(parts)
            if word in entries:
                if body not in entries[word]:
                    entries[word] += "\n" + body
            else:
                entries[word] = body

    files, wc, raw_bytes = build_stardict_files(
        "ecdict-en-zh",
        "ECDICT 英汉核心词典",
        entries,
        "ECDICT English-Chinese core dictionary (MIT License, skywind3000/ECDICT)",
    )
    files["LICENSE-ECDICT.txt"] = (cache_dir / "ecdict-LICENSE.txt").read_bytes()
    return {
        "id": "ecdict-en-zh",
        "zip_name": "PicoNano-dict-ecdict-en-zh.zip",
        "title": "ECDICT 英汉核心词典",
        "subtitle": "英译中 · 音标、考纲标签与词形变化",
        "direction": "英 → 汉",
        "wordcount": wc,
        "license": "MIT",
        "source": "skywind3000/ECDICT",
        "source_url": "https://github.com/skywind3000/ECDICT",
        "raw_dict_bytes": raw_bytes,
    }, files


def clean_xinhua_text(text: str, max_len: int = 900) -> str:
    text = re.sub(r"[─—]{4,}", "", text or "")
    lines = [re.sub(r"\s+", " ", ln).strip() for ln in text.splitlines()]
    lines = [ln for ln in lines if ln]
    joined = "\n".join(lines)
    if len(joined) > max_len:
        joined = joined[:max_len].rstrip() + "…"
    return joined


def build_xinhua(cache_dir: pathlib.Path, include_ci: bool = True):
    """构建中华新华字词成语典（汉字 + 成语 + 汉语词语，MIT 协议）。
    Build Chinese Xinhua character, idiom and word dictionary (MIT License, pwxcoo/chinese-xinhua)."""
    entries: dict[str, list[str]] = {}

    def add(key: str, text: str):
        key = (key or "").strip()
        text = (text or "").strip()
        if not key or not text or len(key) > 32:
            return
        bucket = entries.setdefault(key, [])
        if text not in bucket:
            bucket.append(text)

    # 1. 汉字 / Single characters
    for row in json.loads((cache_dir / "xinhua-word.json").read_text(encoding="utf-8")):
        w = (row.get("word") or "").strip()
        if not w:
            continue
        meta = []
        if row.get("pinyin"):
            meta.append(f"拼音：{row['pinyin'].strip()}")
        if row.get("radicals"):
            meta.append(f"部首：{row['radicals'].strip()}")
        if row.get("strokes"):
            meta.append(f"笔画：{str(row['strokes']).strip()}")
        if row.get("oldword") and row["oldword"].strip() != w:
            meta.append(f"繁体：{row['oldword'].strip()}")
        exp = clean_xinhua_text(row.get("explanation") or "", max_len=700)
        parts = []
        if meta:
            parts.append(" · ".join(meta))
        if exp:
            parts.append(exp)
        if parts:
            add(w, "\n".join(parts))

    # 2. 成语 / Idioms
    for row in json.loads((cache_dir / "xinhua-idiom.json").read_text(encoding="utf-8")):
        w = (row.get("word") or "").strip()
        if not w:
            continue
        parts = []
        if row.get("pinyin"):
            parts.append(f"【成语】{row['pinyin'].strip()}")
        if row.get("explanation"):
            parts.append("释义：" + clean_xinhua_text(row["explanation"], max_len=400))
        if row.get("derivation") and row["derivation"].strip() != "无":
            parts.append("出处：" + clean_xinhua_text(row["derivation"], max_len=240))
        if row.get("example") and row["example"].strip() != "无":
            parts.append("示例：" + clean_xinhua_text(row["example"], max_len=240))
        if parts:
            add(w, "\n".join(parts))

    # 3. 词语 / Chinese words
    if include_ci:
        for row in json.loads((cache_dir / "xinhua-ci.json").read_text(encoding="utf-8")):
            w = (row.get("ci") or "").strip()
            exp = clean_xinhua_text(row.get("explanation") or "", max_len=500)
            if w and exp:
                add(w, exp)

    merged = {k: "\n\n".join(v) for k, v in entries.items()}
    stem = "xinhua-zh" if include_ci else "xinhua-char-idiom-zh"
    title = "中华新华字词成语典" if include_ci else "中华新华汉字成语典"
    subtitle = "汉解 · 1.6万汉字、3万成语与26万汉语词语" if include_ci else "汉解 · 1.6万汉字与3万成语释义出处"
    files, wc, raw_bytes = build_stardict_files(
        stem,
        title,
        merged,
        "Chinese Xinhua dictionary (MIT License, pwxcoo/chinese-xinhua)",
    )
    files["LICENSE-chinese-xinhua.txt"] = (cache_dir / "xinhua-LICENSE.txt").read_bytes()
    return {
        "id": stem,
        "zip_name": f"PicoNano-dict-{stem}.zip",
        "title": title,
        "subtitle": subtitle,
        "direction": "汉 → 汉",
        "wordcount": wc,
        "license": "MIT",
        "source": "pwxcoo/chinese-xinhua",
        "source_url": "https://github.com/pwxcoo/chinese-xinhua",
        "raw_dict_bytes": raw_bytes,
    }, files


def build_cedict(cache_dir: pathlib.Path):
    """构建 CC-CEDICT 汉英词典（简体与繁体双索引，CC BY-SA 4.0 协议）。
    Build CC-CEDICT Chinese-English dictionary (Simplified & Traditional indexed, CC BY-SA 4.0)."""
    entries: dict[str, list[str]] = {}
    license_lines = []
    with gzip.open(cache_dir / "cedict_ts.u8.gz", "rt", encoding="utf-8") as fp:
        for line in fp:
            if line.startswith("#"):
                if len(license_lines) < 40:
                    license_lines.append(line.lstrip("#").strip())
                continue
            m = CEDICT_LINE.match(line.strip())
            if not m:
                continue
            trad, simp, raw_py, defs = m.groups()
            py = format_cedict_pinyin(raw_py)
            gloss = "; ".join(d.strip() for d in defs.split("/") if d.strip())
            if not gloss:
                continue
            item_s = f"[{py}]" + (f" (繁：{trad})" if trad != simp else "") + f"\n{gloss}"
            b_s = entries.setdefault(simp, [])
            if item_s not in b_s:
                b_s.append(item_s)
            if trad != simp:
                item_t = f"[{py}] (简：{simp})\n{gloss}"
                b_t = entries.setdefault(trad, [])
                if item_t not in b_t:
                    b_t.append(item_t)

    merged = {k: "\n\n".join(v) for k, v in entries.items()}
    files, wc, raw_bytes = build_stardict_files(
        "cc-cedict-zh-en",
        "CC-CEDICT 汉英词典",
        merged,
        "CC-CEDICT Chinese-English dictionary (CC BY-SA 4.0, MDBG)",
    )
    license_text = (
        "CC-CEDICT — Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)\n"
        "https://creativecommons.org/licenses/by-sa/4.0/\n"
        "Source: https://www.mdbg.net/chinese/dictionary?page=cc-cedict\n\n"
        + "\n".join(license_lines).strip()
        + "\n"
    )
    files["LICENSE-CC-CEDICT.txt"] = license_text.encode("utf-8")
    return {
        "id": "cc-cedict-zh-en",
        "zip_name": "PicoNano-dict-cc-cedict-zh-en.zip",
        "title": "CC-CEDICT 汉英词典",
        "subtitle": "汉译英 · 简繁体双索引、声调拼音与英文释义",
        "direction": "汉 → 英",
        "wordcount": wc,
        "license": "CC BY-SA 4.0",
        "source": "MDBG CC-CEDICT",
        "source_url": "https://www.mdbg.net/chinese/dictionary?page=cc-cedict",
        "raw_dict_bytes": raw_bytes,
    }, files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=pathlib.Path, default=DEFAULT_CACHE)
    parser.add_argument("--out", type=pathlib.Path, default=DEFAULT_OUT)
    parser.add_argument("--skip-xinhua-ci", action="store_true", help="Build only characters + idioms for Xinhua")
    args = parser.parse_args()

    ensure_sources(args.cache)
    args.out.mkdir(parents=True, exist_ok=True)

    manifest_items = []
    for builder in (
        lambda: build_ecdict(args.cache),
        lambda: build_xinhua(args.cache, include_ci=not args.skip_xinhua_ci),
        lambda: build_cedict(args.cache),
    ):
        meta, files = builder()
        zip_path = args.out / meta["zip_name"]
        write_deterministic_zip(zip_path, files)
        zip_bytes = zip_path.read_bytes()
        meta["size"] = len(zip_bytes)
        meta["sha256"] = hashlib.sha256(zip_bytes).hexdigest()
        meta["files"] = [
            {"name": k, "size": len(v)}
            for k, v in sorted(files.items())
            if not k.startswith("LICENSE")
        ]
        manifest_items.append(meta)
        print(
            f"BUILT {meta['zip_name']}: words={meta['wordcount']:,} "
            f"raw={meta['raw_dict_bytes']/1048576:.2f} MiB -> zip={meta['size']/1048576:.2f} MiB "
            f"sha256={meta['sha256'][:12]}",
            flush=True,
        )

    manifest = {
        "schema": 1,
        "generated_date": datetime.date.today().isoformat(),
        "dictionaries": manifest_items,
    }
    (args.out / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Wrote {args.out / 'manifest.json'} ({len(manifest_items)} dictionaries)", flush=True)


if __name__ == "__main__":
    main()
