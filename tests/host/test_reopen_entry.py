"""原创EPUB的重开身份生命周期。 / Reopen identity lifecycle with an original EPUB."""
import pathlib
import subprocess
import sys
import tempfile
from test_resume import fixture

with tempfile.TemporaryDirectory() as temporary:
    root = pathlib.Path(temporary)
    fixture(root)
    state = root / 'state'
    state.mkdir()
    font = pathlib.Path(__file__).resolve().parents[2] / 'assets/fonts/HarmonyOS_Sans_SC_Regular.ttf'
    subprocess.run([sys.argv[1], str(root / 'book.epub'), str(font), str(state)], check=True, timeout=60)
