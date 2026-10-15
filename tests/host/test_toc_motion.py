import pathlib,subprocess,sys,tempfile
from toc_motion_fixture import create
with tempfile.TemporaryDirectory() as folder:
    book=create(pathlib.Path(folder)/'toc.epub')
    subprocess.run([sys.argv[1],str(book),sys.argv[2]],check=True,timeout=60)
