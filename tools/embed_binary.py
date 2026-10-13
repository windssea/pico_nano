"""将已固定字体嵌入构建产物。 / Embed pinned font into build output."""
import pathlib
import hashlib
import sys
symbol = sys.argv[3] if len(sys.argv) > 3 else "pn_ui_font"
if not symbol.isidentifier() or not symbol.isascii():
    raise SystemExit("Invalid C symbol")
data = pathlib.Path(sys.argv[1]).read_bytes()
rows = ["#include <stddef.h>", "#include <stdint.h>", f"const uint8_t {symbol}_bytes[] = {{"]
rows.extend("    " + ",".join(str(x) for x in data[i:i+32]) + "," for i in range(0, len(data), 32))
rows.extend(["};", f"const size_t {symbol}_size = {len(data)};",
             f"const uint8_t {symbol}_sha256[32] = {{" + ",".join(str(x) for x in hashlib.sha256(data).digest()) + "};", ""])
pathlib.Path(sys.argv[2]).write_text("\n".join(rows), encoding="utf8")
