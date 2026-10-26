# SPDX-License-Identifier: Apache-2.0
"""隔离存档根，不接硬件。 / Isolated save root without hardware access."""
import subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as root:subprocess.run([sys.argv[1],root],check=True)
