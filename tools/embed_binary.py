"""将已固定字体嵌入构建产物。 / Embed pinned font into build output."""
import pathlib
import sys
data = pathlib.Path(sys.argv[1]).read_bytes()
rows = ["#include <stddef.h>", "#include <stdint.h>", "const uint8_t pn_ui_font_bytes[] = {"]
rows.extend("    " + ",".join(str(x) for x in data[i:i+32]) + "," for i in range(0, len(data), 32))
rows.extend(["};", f"const size_t pn_ui_font_size = {len(data)};", ""])
pathlib.Path(sys.argv[2]).write_text("\n".join(rows), encoding="utf8")
