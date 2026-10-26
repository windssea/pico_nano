"""真实控制层反馈先于外置字体读取，热/冷唤醒与挂载屏障。 / Real-controller feedback precedes external font reads, with warm/cold wake and mount barriers."""
import os
import pathlib
import random
import shutil
import subprocess
import sys
import tempfile
import zipfile
from test_resume import fixture

root=pathlib.Path('/sdcard/piconano')
state=pathlib.Path('/data/progress')
for folder in (root,state):
    if folder.exists():shutil.rmtree(folder)
    folder.mkdir(parents=True)
with tempfile.TemporaryDirectory() as temporary:
    fixture(pathlib.Path(temporary))
    books=root/'books';books.mkdir()
    book=books/'feedback.epub';shutil.copyfile(pathlib.Path(temporary)/'book.epub',book)
    with zipfile.ZipFile(book,'a') as archive:
        archive.writestr('bulk-verification.dat',random.Random(20261026).randbytes(8*1024*1024),compress_type=zipfile.ZIP_STORED)
    fonts=root/'fonts';fonts.mkdir()
    shutil.copyfile(sys.argv[2],fonts/'HarmonyOS_Sans_SC_Regular.ttf')
    frames=pathlib.Path(temporary)/'frames';frames.mkdir()
    env=dict(os.environ,PN_FEEDBACK_BOOK=str(book),PN_OPEN_FRAMES=str(frames))
    result=subprocess.run([sys.argv[1]],env=env,text=True,capture_output=True,timeout=110)
    assert result.returncode==0,(result.stdout,result.stderr)
    assert 'feedback_boundaries warm_direct=1 cold_one_loader=1 mount_cancel_retry=1 mount_fail_released=1' in result.stdout,result.stdout
    assert 'menu_probe closed_pool_zero=1' in result.stdout,result.stdout
    print('\n'.join(line for line in result.stdout.splitlines() if line.startswith('feedback_')))
shutil.rmtree(root)
shutil.rmtree(state)
