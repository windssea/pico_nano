# SPDX-License-Identifier: Apache-2.0
"""UTF-8/GB18030的真实续读与完整来源复核。/ Real resume packets and complete verification for UTF-8/GB18030."""
import pathlib
import os
import subprocess
import sys
import tempfile

for encoding in ('utf-8', 'gb18030'):
    with tempfile.TemporaryDirectory(prefix='pn-txt-pending-') as temporary:
        root = pathlib.Path(temporary)
        book = root / 'source.txt'
        book.write_bytes(('第一章 山河与归途\n每一次阅读都保留完整校验。春风吹过长街，远山映在水面。\n' * 12000).encode(encoding))
        state, cache = root / 'state', root / 'cache'
        state.mkdir()
        cache.mkdir()
        env=dict(os.environ)
        if encoding=='gb18030':env['PN_EXPECT_GBK_FALLBACK']='1'
        subprocess.run([sys.argv[1], str(book), str(state), str(cache)], env=env,check=True)
