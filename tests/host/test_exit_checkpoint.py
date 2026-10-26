# SPDX-License-Identifier: Apache-2.0
"""原创夹具、隔离缓存，不接硬件。 / Original fixtures and isolated caches without hardware access."""
import pathlib,subprocess,sys,tempfile
from test_resume import fixture
with tempfile.TemporaryDirectory() as folder:
 root=pathlib.Path(folder);fixture(root)
 subprocess.run([sys.argv[1],str(root/'book.txt'),str(root/'book.epub'),folder],check=True)
