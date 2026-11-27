"""生成网页安装器静态站点，包含 PicoNano 共享素材预览/打包、开源词典、自动同步版本更新日志、固件和清单。
Build the web-installer static site with PicoNano shared-asset previews/packs, open-source dictionaries, auto-synced changelog, firmware and manifest."""
import argparse
import functools
import hashlib
import http.server
import io
import json
import pathlib
import re
import shutil
import struct
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
GITEE_SHARE_REPO = "windssea/pico_nano"
GITEE_SHARE_TAG = "share-assets"
ZIP_FIXED_TIME = (2026, 1, 1, 0, 0, 0)

# 清单键 → 发布目录内文件名。/ Manifest key -> file name inside a release folder.
FILES = {
    "bootloader": "bootloader.bin",
    "partitionTable": "partition-table.bin",
    "otaData": "ota_data_initial.bin",
    "app": "pico_nano.bin",
    "factoryData": "first-install/factory-data.bin",
    "factoryWallpaper": "first-install/factory-wallpaper.bin",
}
# flash_args 中必须出现的固定偏移。/ Fixed offsets that flash_args must list.
OFFSETS = {"bootloader.bin": 0x0, "partition-table.bin": 0x8000}
RELEASE = re.compile(r"pico_nano-(\d+(?:\.\d+)+)-esp32s3")
UPGRADE = re.compile(r"pico_nano-(\d+(?:\.\d+)+)-([0-9a-f]{7})-upgrade")
FULL = re.compile(r"pico_nano-(\d+(?:\.\d+)+)-([0-9a-f]{7})-full")

# PicoNano 共享素材分类（与设备存储卡上传目录一一对应）。
# PicoNano shared-asset categories (1:1 aligned with device SD upload targets).
SHARE_CATEGORIES = (
    {
        "id": "mask",
        "kind": "image",
        "dir": "image/mask",
        "title": "透明遮罩",
        "shortTitle": "遮罩",
        "badge": "锁屏透明遮罩",
        "description": "带透明通道的锁屏遮罩图，叠加在阅读画面或壁纸上方使用。点开可模拟墨水屏阅读正文叠加预览。",
        "uploadTarget": "传书页「阅读遮罩」（/piconano/masks/）",
        "extensions": (".png",),
        "previewMode": "mask",
    },
    {
        "id": "wallpaper",
        "kind": "image",
        "dir": "image/wallpaper",
        "title": "锁屏壁纸",
        "shortTitle": "壁纸",
        "badge": "684×1216 锁屏图",
        "description": "适合小纸 Pico 竖屏比例（684×1216）的整屏锁屏壁纸，上传时自动转为 16 灰阶。",
        "uploadTarget": "传书页「锁屏壁纸」（/piconano/wallpapers/）",
        "extensions": (".png", ".jpg", ".jpeg"),
        "previewMode": "wallpaper",
    },
    {
        "id": "background",
        "kind": "image",
        "dir": "image/background",
        "title": "阅读背景",
        "shortTitle": "阅读背景",
        "badge": "正文纸纹底图",
        "description": "阅读正文时的纸纹与浅色底图，上传后可在「排版设置 → 背景」中选用。",
        "uploadTarget": "传书页「阅读背景」（/piconano/backgrounds/）",
        "extensions": (".png", ".jpg", ".jpeg"),
        "previewMode": "background",
    },
    {
        "id": "banner",
        "kind": "image",
        "dir": "image/banner",
        "title": "首页横幅",
        "shortTitle": "首页横幅",
        "badge": "684×146 顶部横幅",
        "description": "首页「正在阅读」上方横幅背景图（建议 684×146），上传后在「书架与主页 → 正在阅读上方背景」选用。",
        "uploadTarget": "传书页「首页横幅」（/piconano/banners/）",
        "extensions": (".png", ".jpg", ".jpeg"),
        "previewMode": "banner",
    },
    {
        "id": "cover",
        "kind": "image",
        "dir": "image/cover",
        "title": "书籍封面",
        "shortTitle": "封面",
        "badge": "3:4 自定义书封",
        "description": "为本地 TXT 或缺少封面的电子书准备的自定义书封，命名与书名一致即可在书架自动匹配。",
        "uploadTarget": "传书页「书籍封面」（/piconano/covers/）",
        "extensions": (".png", ".jpg", ".jpeg"),
        "previewMode": "cover",
    },
    {
        "id": "font",
        "kind": "font",
        "dir": "font",
        "title": "字体分享",
        "shortTitle": "字体",
        "badge": "TTF / OTF 开源字体",
        "description": "可合法再分发的开源阅读字体（TTF / OTF），支持网页实时试看字形并下载导入设备。",
        "uploadTarget": "传书页「字体」（/piconano/fonts/）",
        "extensions": (".ttf", ".otf"),
        "previewMode": "font",
    },
    {
        "id": "book",
        "kind": "book",
        "dir": "book",
        "title": "书籍分享",
        "shortTitle": "书籍",
        "badge": "EPUB / TXT 公版书",
        "description": "公版名著与开源电子书（EPUB / TXT），下载后直接经传书页导入即可开始阅读。",
        "uploadTarget": "传书页「书籍」（/piconano/books/）",
        "extensions": (".epub", ".txt"),
        "previewMode": "book",
    },
)


def image_dimensions(data):
    """纯标准库解析 PNG / JPEG 尺寸，不依赖 Pillow。 / Parse PNG/JPEG width and height using stdlib only."""
    if len(data) >= 24 and data[:8] == b"\x89PNG\r\n\x1a\n" and data[12:16] == b"IHDR":
        return struct.unpack(">II", data[16:24])
    if len(data) >= 4 and data[:2] == b"\xff\xd8":
        pos = 2
        while pos + 4 <= len(data):
            if data[pos] != 0xFF:
                pos += 1
                continue
            while pos < len(data) and data[pos] == 0xFF:
                pos += 1
            if pos >= len(data):
                break
            marker = data[pos]
            pos += 1
            if marker in (0xD8, 0xD9, 0x01) or 0xD0 <= marker <= 0xD7:
                continue
            if pos + 2 > len(data):
                break
            seg_len = struct.unpack(">H", data[pos:pos + 2])[0]
            if seg_len < 2 or pos + seg_len > len(data):
                break
            if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
                if pos + 7 <= len(data):
                    h, w = struct.unpack(">HH", data[pos + 3:pos + 7])
                    return w, h
                break
            pos += seg_len
    return 0, 0


def build_deterministic_zip(entries):
    """生成时间戳固定、条目有序的可复现 ZIP 字节串。 / Build deterministic ZIP bytes with fixed timestamps and sorted entries."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for arcname, payload in sorted(entries, key=lambda pair: pair[0]):
            info = zipfile.ZipInfo(arcname, date_time=ZIP_FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, payload)
    return buf.getvalue()


def gitee_share_download_url(file_name, gitee_repo=GITEE_SHARE_REPO, gitee_tag=GITEE_SHARE_TAG):
    """构造 Gitee 共享素材 Release 附件下载地址。 / Construct a Gitee share-assets release download URL."""
    return f"https://gitee.com/{gitee_repo}/releases/download/{gitee_tag}/{file_name}"


def scan_share_assets(root, gitee_repo=GITEE_SHARE_REPO, gitee_tag=GITEE_SHARE_TAG):
    """扫描 assets/share/ 生成共享素材清单、分类 ZIP 字节与兼容 PicoNano.zip。
    Scan assets/share/ to build the shared-asset manifest, per-category ZIP packs, and backward-compatible PicoNano.zip."""
    share_root = root / "assets/share"
    categories = []
    packs = {}
    mask_entries = []
    for spec in SHARE_CATEGORIES:
        folder = share_root / spec["dir"]
        items = []
        pack_entries = []
        if folder.is_dir():
            for path in sorted(folder.iterdir(), key=lambda p: p.name):
                if not path.is_file() or path.name.startswith(".") or path.name.lower() == "readme.md":
                    continue
                ext = path.suffix.lower()
                if ext not in spec["extensions"]:
                    continue
                data = path.read_bytes()
                digest = hashlib.sha256(data).hexdigest()
                rel = f"{spec['dir']}/{path.name}"
                entry = {
                    "name": path.name,
                    "stem": path.stem,
                    "ext": ext.lstrip("."),
                    "relPath": rel,
                    "path": f"assets/share/{rel}",
                    "size": len(data),
                    "sha256": digest,
                }
                if spec["kind"] == "image":
                    width, height = image_dimensions(data)
                    if width > 0 and height > 0:
                        entry["width"] = width
                        entry["height"] = height
                items.append(entry)
                pack_entries.append((f"PicoNano-{spec['id']}/{path.name}", data))
                if spec["id"] == "mask":
                    mask_entries.append((f"PicoNano/{path.name}", data))
        cat_obj = {
            "id": spec["id"],
            "kind": spec["kind"],
            "dir": spec["dir"],
            "title": spec["title"],
            "shortTitle": spec["shortTitle"],
            "badge": spec["badge"],
            "description": spec["description"],
            "uploadTarget": spec["uploadTarget"],
            "previewMode": spec["previewMode"],
            "extensions": list(spec["extensions"]),
            "count": len(items),
            "totalSize": sum(item["size"] for item in items),
            "items": items,
        }
        if pack_entries:
            pack_name = f"PicoNano-share-{spec['id']}.zip"
            pack_bytes = build_deterministic_zip(pack_entries)
            packs[pack_name] = pack_bytes
            cat_obj["pack"] = {
                "fileName": pack_name,
                "path": f"assets/share-packs/{pack_name}",
                "giteeUrl": gitee_share_download_url(pack_name, gitee_repo, gitee_tag),
                "size": len(pack_bytes),
                "sha256": hashlib.sha256(pack_bytes).hexdigest(),
            }
        categories.append(cat_obj)

    compat_zip = None
    if mask_entries:
        compat_zip = build_deterministic_zip(mask_entries)
    elif (root / "assets/wall/PicoNano.zip").is_file():
        compat_zip = (root / "assets/wall/PicoNano.zip").read_bytes()

    manifest = {
        "schema": 1,
        "giteeRepo": gitee_repo,
        "giteeTag": gitee_tag,
        "giteeReleaseUrl": f"https://gitee.com/{gitee_repo}/releases/tag/{gitee_tag}",
        "categories": categories,
    }
    return manifest, packs, compat_zip


def read_full(folder):
    """校验同源码完整包的全部镜像，不借用历史引导文件。 / Validate every image in a full package without borrowing historical boot files."""
    match = FULL.fullmatch(folder.name)
    metadata = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    if (metadata.get("mode") != "full" or metadata.get("target") != "esp32s3" or
            metadata.get("board") != "RDP-G01-W" or metadata.get("version") != match.group(1) or
            not metadata.get("source_commit", "").startswith(match.group(2))):
        raise SystemExit(f"{folder.name}: invalid full package identity")
    files = {}
    for key, name in FILES.items():
        data = (folder / name).read_bytes()
        entry = metadata["files"][name]
        digest = hashlib.sha256(data).hexdigest()
        if entry["sha256"] != digest or entry["bytes"] != len(data):
            raise SystemExit(f"{folder.name}/{name}: checksum mismatch")
        files[key] = {"name": name, "size": len(data), "sha256": digest}
    if (files["partitionTable"]["size"] != 0xc00 or not 0 < files["app"]["size"] <= 0x600000 or
            not 0 < files["bootloader"]["size"] <= 0x8000 or files["otaData"]["size"] != 0x2000 or
            any(files[k]["size"] != 0x100000 for k in ("factoryData", "factoryWallpaper"))):
        raise SystemExit(f"{folder.name}: invalid full image sizes")
    version = f"{match.group(1)}+{match.group(2)}"
    return {"version": version, "commit": metadata["source_commit"], "builtAt": metadata.get("built_at", ""),
            "mode": "full", "verifiedOnHardware": False, "path": f"firmware/{version}-full/", "files": files, "_folder": folder}


def read_upgrade(folder):
    """发布保留布局升级，只暴露应用与校验表。 / Publish a preserving upgrade with only the app and reference table."""
    match = UPGRADE.fullmatch(folder.name)
    metadata = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    files = {}
    for key, name in (("app", "pico_nano.bin"), ("partitionTable", "expected-partition-table.bin")):
        data = (folder / name).read_bytes()
        entry = metadata["files"][name]
        digest = hashlib.sha256(data).hexdigest()
        if entry["sha256"] != digest or entry["bytes"] != len(data):
            raise SystemExit(f"{folder.name}/{name}: checksum mismatch")
        files[key] = {"name": name, "size": len(data), "sha256": digest}
    if files["partitionTable"]["size"] != 0xc00 or not 0 < files["app"]["size"] <= 0x600000:
        raise SystemExit("Invalid preserving upgrade sizes")
    version = f"{match.group(1)}+{match.group(2)}"
    return {"version": version, "commit": metadata["source_commit"], "builtAt": metadata.get("built_at", ""), "mode": "app-only", "verifiedOnHardware": False,
            "path": f"firmware/{version}/", "files": files, "_folder": folder}


def version_key(version):
    return tuple(int(part) for part in version.split("."))


def read_release(folder):
    """校验一个发布目录并返回清单条目。/ Validate one release folder and return its manifest entry."""
    version = RELEASE.fullmatch(folder.name).group(1)
    sums = {}
    for line in (folder / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
        digest, name = line.split(None, 1)
        sums[name.lstrip("*")] = digest
    files = {}
    for key, name in FILES.items():
        data = (folder / name).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if sums.get(name) != digest:
            raise SystemExit(f"{folder.name}/{name}: SHA256SUMS mismatch")
        files[key] = {"name": name, "size": len(data), "sha256": digest}
    listed = {}
    for line in (folder / "flash_args").read_text(encoding="utf-8").splitlines()[1:]:
        offset, name = line.split()
        listed[name] = int(offset, 16)
    for name, offset in OFFSETS.items():
        if listed.get(name) != offset:
            raise SystemExit(f"{folder.name}: flash_args must place {name} at {offset:#x}")
    readme = (folder / "README.md").read_text(encoding="utf-8")
    commit = re.search(r"源码提交：([0-9a-f]{7,40})", readme)
    return {
        "version": version,
        "mode": "full",
        "legacy": True,
        "commit": commit.group(1) if commit else "",
        "verifiedOnHardware": False,
        "path": f"firmware/{version}/",
        "files": files,
        "_folder": folder,
    }


def mark_hardware_verified(releases):
    """按版本、完整源码提交和应用摘要匹配发布后的真机确认。 / Match post-release device confirmation by version, full source commit and application digest."""
    path = ROOT / "releases/hardware-verification.json"
    if not path.exists():
        return
    registry = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(registry, dict) or registry.get("schema") != 1 or not isinstance(registry.get("releases"), list):
        raise SystemExit("Invalid hardware verification registry")
    verified = set()
    for record in registry["releases"]:
        if (not isinstance(record, dict) or record.get("verified_on_hardware") is not True or
                not all(isinstance(record.get(field), str) for field in ("version", "source_commit", "application_sha256")) or
                not re.fullmatch(r"\d+(?:\.\d+)+", str(record.get("version", ""))) or
                not re.fullmatch(r"[0-9a-f]{40}", str(record.get("source_commit", ""))) or
                not re.fullmatch(r"[0-9a-f]{64}", str(record.get("application_sha256", "")))):
            raise SystemExit("Invalid hardware verification identity")
        key = (record["version"], record["source_commit"], record["application_sha256"])
        if key in verified:
            raise SystemExit("Duplicate hardware verification identity")
        verified.add(key)
    for release in releases:
        key = (release["version"].split("+")[0], release["commit"], release["files"]["app"]["sha256"])
        release["verifiedOnHardware"] = key in verified


def clean_markdown_text(text):
    """去掉 Markdown 链接与反引号标记，保留纯文本供网页安全渲染。
    Strip Markdown links and inline code ticks into clean plain text for safe textContent rendering."""
    text = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)
    text = text.replace("`", "")
    return re.sub(r"\s+", " ", text).strip()


def parse_version_sections(markdown_text, stop_boilerplate=False):
    """按 ## X.Y.Z（YYYY-MM-DD）提取版本更新说明，忽略内部日期开发日志小节。
    Extract ## X.Y.Z (YYYY-MM-DD) release sections, ignoring internal dated dev notes."""
    heading = re.compile(r"^##\s+(\d+(?:\.\d+)+)(?:（([^）]*)）|\s*\(([^)]*)\))?\s*$", re.MULTILINE)
    any_h2 = re.compile(r"^##\s+", re.MULTILINE)
    sections = []
    for m in heading.finditer(markdown_text):
        ver = m.group(1)
        date = (m.group(2) or m.group(3) or "").strip()
        start = m.end()
        nxt = any_h2.search(markdown_text, start)
        end = nxt.start() if nxt else len(markdown_text)
        body = markdown_text[start:end].strip()
        bullets = []
        paragraphs = []
        for raw_line in body.splitlines():
            line = raw_line.strip()
            if not line:
                continue
            if stop_boilerplate and (
                line.startswith("已有 PicoNano 用户") or line.startswith("正式板级应用")
                or line.startswith("正式构建源码") or line.startswith("最终同源主机")
                or line.startswith("提供保留布局升级")
            ):
                break
            if line.startswith("- ") or line.startswith("* "):
                cleaned = clean_markdown_text(line[2:])
                if cleaned:
                    bullets.append(cleaned)
            elif not line.startswith("#"):
                cleaned = clean_markdown_text(line)
                if cleaned:
                    paragraphs.append(cleaned)
        items = bullets if bullets else paragraphs[:4]
        if items:
            sections.append({"version": ver, "date": date, "items": items})
    return sections


def read_changelogs(root, limit=12):
    """从 docs/CHANGELOG.md 自动提取版本更新日志（缺省回退 docs/INSTALLATION.md）。
    Auto-extract version changelogs from docs/CHANGELOG.md (falling back to docs/INSTALLATION.md)."""
    by_version = {}
    order = []
    for rel_path, stop_bp in (("docs/CHANGELOG.md", False), ("docs/INSTALLATION.md", True)):
        path = root / rel_path
        if not path.is_file():
            continue
        for entry in parse_version_sections(path.read_text(encoding="utf-8"), stop_boilerplate=stop_bp):
            ver = entry["version"]
            if ver not in by_version:
                by_version[ver] = entry
                order.append(ver)
    entries = [by_version[v] for v in order]
    entries.sort(key=lambda e: version_key(e["version"]), reverse=True)
    return entries[:limit]


def read_dictionaries(root, gitee_repo=GITEE_SHARE_REPO, gitee_tag=GITEE_SHARE_TAG):
    """校验 assets/dicts/manifest.json 中的开源词典包并返回清单条目（含 Gitee 加速地址）。
    Validate open-source dictionary archives in assets/dicts/manifest.json and return manifest entries with Gitee URLs."""
    dicts_dir = root / "assets/dicts"
    manifest_path = dicts_dir / "manifest.json"
    if not manifest_path.is_file():
        return []
    raw = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict) or raw.get("schema") != 1 or not isinstance(raw.get("dictionaries"), list):
        raise SystemExit("Invalid assets/dicts/manifest.json")
    result = []
    for item in raw["dictionaries"]:
        zip_name = item.get("zip_name", "")
        if not zip_name or "/" in zip_name or "\\" in zip_name or not zip_name.endswith(".zip"):
            raise SystemExit(f"Invalid dictionary archive name: {zip_name}")
        archive = dicts_dir / zip_name
        if not archive.is_file():
            raise SystemExit(f"Missing dictionary archive: {archive}")
        data = archive.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if len(data) != item.get("size") or digest != item.get("sha256"):
            raise SystemExit(f"Dictionary checksum mismatch: {zip_name}")
        result.append({
            "id": item["id"],
            "title": item["title"],
            "subtitle": item.get("subtitle", ""),
            "direction": item.get("direction", ""),
            "wordcount": item.get("wordcount", 0),
            "license": item.get("license", ""),
            "source": item.get("source", ""),
            "sourceUrl": item.get("source_url", ""),
            "fileName": zip_name,
            "path": f"assets/dicts/{zip_name}",
            "giteeUrl": gitee_share_download_url(zip_name, gitee_repo, gitee_tag),
            "size": len(data),
            "sha256": digest,
        })
    return result


def build_share_manifest_doc(root, gitee_repo=GITEE_SHARE_REPO, gitee_tag=GITEE_SHARE_TAG):
    """构建包含全部共享素材分类与开源词典的统一在线清单文档。
    Build the unified online manifest document combining all shared-asset categories and open-source dictionaries."""
    share_manifest, share_packs, compat_zip = scan_share_assets(root, gitee_repo, gitee_tag)
    dictionaries = read_dictionaries(root, gitee_repo, gitee_tag)
    doc = {**share_manifest, "dictionaries": dictionaries}
    return doc, share_packs, compat_zip, dictionaries


def write_share_manifest(root=ROOT):
    """将统一在线素材清单写入仓库 assets/share/manifest.json，供国内 CDN 与传书页在线拉取。
    Write the unified online manifest to assets/share/manifest.json for CDN mirrors and the Transfer page."""
    doc, _, _, dictionaries = build_share_manifest_doc(root)
    target = root / "assets/share/manifest.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    total_share = sum(cat["count"] for cat in doc["categories"])
    print(f"Share manifest: {target} ({total_share} share assets; {len(dictionaries)} dicts)")
    return doc


def build(out, latest_only=False):
    releases = []
    for folder in (ROOT / "releases").iterdir():
        if not folder.is_dir():
            continue
        if RELEASE.fullmatch(folder.name):
            releases.append(read_release(folder))
        elif UPGRADE.fullmatch(folder.name):
            releases.append(read_upgrade(folder))
        elif FULL.fullmatch(folder.name):
            releases.append(read_full(folder))
    releases.sort(key=lambda r: (version_key(r["version"].split("+")[0]), r.get("mode") == "app-only", r.get("builtAt", "")), reverse=True)
    if not releases:
        raise SystemExit("releases/ has no pico_nano-<version>-esp32s3 folder")
    # 正式分发只展示最高版本的两种安装方式，历史源码与本地包不删。 / Stable distribution lists both installation modes of the highest version, retaining history and local packages.
    if latest_only:
        newest = releases[0]["version"].split("+")[0]
        releases = [item for item in releases if item["version"].split("+")[0] == newest]
    # 完整刷入后的设备也可只更新应用；额外白名单来自同源码同应用的已校验完整包。
    # Devices installed with a full package can update only the app; obtain the extra allowlist from a verified full package with identical source and app.
    for release in releases:
        if release.get("mode") != "app-only":
            continue
        matches = [other for other in releases if other.get("mode") == "full" and not other.get("legacy")
                   and other["version"] == release["version"] and other["commit"] == release["commit"]]
        if len(matches) > 1:
            raise SystemExit(f"{release['version']}: ambiguous full package")
        if not matches:
            continue
        full = matches[0]
        if any(release["files"]["app"][key] != full["files"]["app"][key] for key in ("size", "sha256")):
            raise SystemExit(f"{release['version']}: full/upgrade application mismatch")
        release["files"]["currentPartitionTable"] = {**full["files"]["partitionTable"], "name": "current-partition-table.bin"}
        release["_current_table"] = full["_folder"] / FILES["partitionTable"]
    # 先校验所有镜像，再匹配明确登记的正式版本，不继承其他版本的验证结论。 / Validate all images before matching explicitly recorded stable releases; never inherit another version's device status.
    mark_hardware_verified(releases)
    changelogs = read_changelogs(ROOT)
    notes_by_version = {entry["version"]: {"date": entry["date"], "items": entry["items"]} for entry in changelogs}
    share_doc, share_packs, compat_zip, dictionaries = build_share_manifest_doc(ROOT)
    share_manifest = {k: v for k, v in share_doc.items() if k != "dictionaries"}
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(ROOT / "installer", out)
    (out / "assets").mkdir(parents=True, exist_ok=True)
    if compat_zip is not None:
        (out / "assets/PicoNano.zip").write_bytes(compat_zip)
    # 输出共享素材原文件、分类打包 ZIP 与独立清单（含词典，与仓库 assets/share/manifest.json 同构）。
    # Emit shareable asset files, category ZIP packs, and standalone manifest (including dictionaries, matching assets/share/manifest.json).
    share_out = out / "assets/share"
    share_out.mkdir(parents=True, exist_ok=True)
    for cat in share_manifest["categories"]:
        for item in cat["items"]:
            src = ROOT / "assets/share" / item["relPath"]
            dst = share_out / item["relPath"]
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dst)
    (share_out / "manifest.json").write_text(json.dumps(share_doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if share_packs:
        packs_out = out / "assets/share-packs"
        packs_out.mkdir(parents=True, exist_ok=True)
        for pack_name, pack_bytes in share_packs.items():
            (packs_out / pack_name).write_bytes(pack_bytes)
    if dictionaries:
        dict_out = out / "assets/dicts"
        dict_out.mkdir(parents=True, exist_ok=True)
        for item in dictionaries:
            shutil.copyfile(ROOT / "assets/dicts" / item["fileName"], dict_out / item["fileName"])
        shutil.copyfile(ROOT / "assets/dicts/manifest.json", dict_out / "manifest.json")
    for release in releases:
        base_ver = release["version"].split("+")[0]
        if base_ver in notes_by_version:
            release["notes"] = notes_by_version[base_ver]
        target = out / release["path"]
        for key, entry in release["files"].items():
            (target / entry["name"]).parent.mkdir(parents=True, exist_ok=True)
            source = release["_current_table"] if key == "currentPartitionTable" else release["_folder"] / entry["name"]
            shutil.copyfile(source, target / entry["name"])
        del release["_folder"]
        release.pop("_current_table", None)
    manifest_payload = {
        "releases": releases,
        "changelog": changelogs,
        "dictionaries": dictionaries,
        "share": share_manifest,
    }
    (out / "firmware" / "manifest.json").write_text(json.dumps(manifest_payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (out / ".nojekyll").write_text("", encoding="utf-8")
    total_share = sum(cat["count"] for cat in share_manifest["categories"])
    print(f"Installer site: {out} ({', '.join(r['version'] for r in releases)}; {len(dictionaries)} dicts; {total_share} share assets; {len(changelogs)} changelog entries)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=pathlib.Path, default=ROOT / "build-dev" / "installer-site")
    parser.add_argument("--serve", type=int, metavar="PORT", help="Serve on http://localhost:PORT after building")
    parser.add_argument("--latest-only", action="store_true", help="Publish only the highest version; keep matching upgrade/full modes")
    parser.add_argument("--write-share-manifest", action="store_true", help="Refresh repository assets/share/manifest.json and exit")
    args = parser.parse_args()
    if args.write_share_manifest:
        write_share_manifest(ROOT)
        return
    out = args.out.resolve()
    build(out, args.latest_only)
    if args.serve:
        class Handler(http.server.SimpleHTTPRequestHandler):
            extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".js": "text/javascript"}
        handler = functools.partial(Handler, directory=str(out))
        print(f"Open http://localhost:{args.serve}/ in Chrome or Edge (Ctrl+C to stop)", flush=True)
        http.server.ThreadingHTTPServer(("127.0.0.1", args.serve), handler).serve_forever()


if __name__ == "__main__":
    main()
