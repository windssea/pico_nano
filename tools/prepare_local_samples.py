"""复制用户测试资源到独立运行目录，保留原件。 / Copy user test resources into an isolated runtime, preserving originals."""
import argparse
import hashlib
import json
import pathlib
import shutil
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[1]


def prepare(books, fonts, output):
    """逐文件记录身份；每次新目录避免旧样本混入。 / Record each source identity in a fresh directory without stale samples."""
    directories = [('books', books.resolve()), ('fonts', fonts.resolve())]
    for _, source in directories:
        if not source.is_dir():
            raise ValueError(f'Missing user resource directory: {source}')
    target = output.resolve() / uuid.uuid4().hex
    report = {'root': target.relative_to(ROOT).as_posix(), 'sources': []}
    for kind, source in directories:
        destination = target / kind
        destination.mkdir(parents=True)
        for original in sorted(source.rglob('*')):
            if not original.is_file():
                continue
            relative = original.relative_to(source)
            copy = destination / relative
            copy.parent.mkdir(parents=True, exist_ok=True)
            with original.open('rb') as stream:
                identity = hashlib.file_digest(stream, 'sha256').hexdigest()
            shutil.copy2(original, copy)
            with copy.open('rb') as stream:
                if hashlib.file_digest(stream, 'sha256').hexdigest() != identity:
                    raise ValueError(f'Source changed while copying: {original}')
            report['sources'].append({'kind': kind, 'path': original.relative_to(ROOT).as_posix(),
                                      'bytes': original.stat().st_size, 'sha256': identity})
        report[kind] = destination.relative_to(ROOT).as_posix()
    (target / 'wallpapers').mkdir()
    report['wallpapers'] = (target / 'wallpapers').relative_to(ROOT).as_posix()
    output.mkdir(parents=True, exist_ok=True)
    (target / 'sources.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (output / 'current.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--books', type=pathlib.Path, default=ROOT / 'tests/testBook')
    parser.add_argument('--fonts', type=pathlib.Path, default=ROOT / 'tests/testFont')
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/user-samples')
    args = parser.parse_args()
    print(json.dumps(prepare(args.books, args.fonts, args.output), ensure_ascii=False))
