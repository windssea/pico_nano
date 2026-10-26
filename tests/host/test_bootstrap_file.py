"""原创小书与隔离目录，全部事务边界。 / Original small book and isolated directories for every commit boundary."""
import pathlib
import subprocess
import sys
import tempfile
from test_resume import fixture
with tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp)
    fixture(root)
    work=root/'work'
    work.mkdir()
    subprocess.run([sys.argv[1],str(root/'book.epub'),str(work)],check=True)
