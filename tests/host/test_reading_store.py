# SPDX-License-Identifier: Apache-2.0
"""私有夹具检查根分类与保留策略。/ Check root diagnostics and retention using isolated fixtures."""
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory(prefix='pn-reading-store-') as root:
    subprocess.run([sys.argv[1],root],check=True)
