"""用独立标准JSON解析器核对协议响应。 / Verify protocol replies with an independent standard JSON parser."""
import json
import subprocess
import sys

result = subprocess.run([sys.argv[1]], capture_output=True, text=True, timeout=30)
assert result.returncode == 0, (result.stdout, result.stderr)
reply = json.loads(result.stdout)
assert reply == {"sessionId": "a" * 32, "files": {'file "id"\\名': "b" * 32}}, reply
