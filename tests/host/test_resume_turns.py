"""续读两页之后继续翻页，覆盖设备让出与扫描重叠。 / Turn beyond both resume pages with device yielding and scan overlap."""
import os
import pathlib
import shutil
import sys
import tempfile
from test_resume import fixture, run

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
        for overlap in (False, True):
            state = root / f'{kind}-yield-{overlap}'
            shutil.copytree(seed, state)
            case_env = dict(env, PN_RESUME_YIELD='1')
            if overlap:
                case_env['PN_RESUME_OVERLAP'] = '1'
            result, actual = run(sys.argv[1], kind, book, str(font), state, root / f'{kind}-actual-{overlap}', env=case_env)
            assert actual == expected and len(actual) == 9, (kind, overlap, result)
            print(kind, 'overlap', overlap, result, flush=True)
