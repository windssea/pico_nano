"""续读两页之后继续翻页，覆盖设备让出与扫描重叠。 / Turn beyond both resume pages with device yielding and scan overlap."""
import os
import pathlib
import shutil
import sys
import tempfile
from test_resume import fixture, run
from test_epub_long_chapter import make_book

with tempfile.TemporaryDirectory() as tmp:
    root = pathlib.Path(tmp)
    fixture(root)
    font = root / 'font.ttf'
    shutil.copyfile(sys.argv[2], font)
    for kind in ('txt', 'epub'):
        book = root / ('book.' + kind)
        seed = root / (kind + '-seed')
        data, _ = run(sys.argv[1], kind, book, str(font), seed, root / (kind + '-seed-frames'), 'seed', dict(os.environ, PN_RESUME_SEMANTIC='1'))
        assert data['stored'] == 2
        if kind == 'epub':
            assert list((seed / 'semantic').glob('*.pnsm')), 'semantic cache was not seeded'
        reference = root / (kind + '-reference')
        shutil.copytree(seed, reference)
        shutil.rmtree(reference / 'reading' / 'resume')
        env = dict(os.environ, PN_RESUME_CONTINUE='1')
        _, expected = run(sys.argv[1], kind, book, str(font), reference, root / (kind + '-expected'), env=env)
        for overlap in (0, 1, 16, 64):
            state = root / f'{kind}-yield-{overlap}'
            shutil.copytree(seed, state)
            case_env = dict(env, PN_RESUME_YIELD='1')
            if overlap:
                case_env['PN_RESUME_OVERLAP'] = '1'
                case_env['PN_RESUME_OVERLAP_STEPS'] = str(overlap)
            result, actual = run(sys.argv[1], kind, book, str(font), state, root / f'{kind}-actual-{overlap}', env=case_env)
            assert actual == expected and len(actual) == 9, (kind, overlap, result)
            if kind == 'epub':
                assert result['loads'] <= len(actual), ('one probe per displayed navigation including OPEN/PREV', overlap, result)
            print(kind, 'overlap', overlap, result, flush=True)

    # 深位置热开首NEXT命中包，第二次立即NEXT不能持续逐字追赶。/ Deep warm opening hits the first NEXT packet; the immediate second NEXT must not chase the prefix scalar by scalar.
    book = root / 'deep.epub'
    make_book(book)
    seed = root / 'deep-seed'
    seeded, _ = run(sys.argv[1], 'epub', book, str(font), seed, root / 'deep-seed-frames', 'seed',
                    dict(os.environ, PN_RESUME_SEED_TURNS='80', PN_RESUME_SEMANTIC='1'))
    assert seeded['stored'] == 2
    reference = root / 'deep-reference'
    shutil.copytree(seed, reference)
    shutil.rmtree(reference / 'reading' / 'resume')
    _, expected = run(sys.argv[1], 'epub', book, str(font), reference, root / 'deep-reference-frames', 'rapid',
                      dict(os.environ, PN_RESUME_CONTINUE='1'))
    for sequence in ('rapid', 'read'):
        if sequence == 'read':
            # 先PREV会保留较大的已验证前文，旧版在后续NEXT超过4096小步。/ PREV retains a larger verified prefix; the old subsequent NEXT exceeds 4096 ticks.
            reference = root / 'deep-rewind-reference'
            shutil.copytree(seed, reference)
            shutil.rmtree(reference / 'reading' / 'resume')
            _, expected = run(sys.argv[1], 'epub', book, str(font), reference, root / 'deep-rewind-reference-frames', sequence,
                              dict(os.environ, PN_RESUME_CONTINUE='1'))
        for index_steps in (0, 1, 32, 500):
            state = root / f'deep-{sequence}-index-{index_steps}'
            shutil.copytree(seed, state)
            result, actual = run(sys.argv[1], 'epub', book, str(font), state, root / f'deep-{sequence}-frames-{index_steps}', sequence,
                                 dict(os.environ, PN_RESUME_CONTINUE='1', PN_RESUME_YIELD='1', PN_RESUME_INDEX_STEPS=str(index_steps)))
            assert len(actual) == (8 if sequence == 'rapid' else 9) and actual == expected, (sequence, index_steps, result)
            assert result['loads'] <= len(actual), ('one probe per displayed navigation including OPEN/PREV', result)
            print('deep', sequence, 'index steps', index_steps, result, flush=True)
