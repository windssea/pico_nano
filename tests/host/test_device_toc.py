"""实际目录/设备适配器使用原创EPUB；SDK扫描仍为替身。 / Actual TOC/device adapter with original EPUB; SDK scans remain doubles."""
import os,pathlib,subprocess,sys,tempfile
from toc_motion_fixture import create
with tempfile.TemporaryDirectory() as folder:
    book=create(pathlib.Path(folder)/'toc.epub')
    subprocess.run([sys.argv[1]],env=dict(os.environ,PN_TEST_TOC_BOOK=str(book),PN_TEST_TOC_FONT=sys.argv[2]),check=True,timeout=90)
