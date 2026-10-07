"""对照独立Python codec与源偏移。 / Compare independent Python codecs and source offsets."""
import pathlib
import random
import subprocess
import sys
import tempfile
rng = random.Random(20261006)
scalars = [chr(cp) for cp in [0, 9, 10, 13, 32, 65, 0x4e2d, 0xffff, 0x10000, 0x10ffff]]
for _ in range(1000):
    cp = rng.randrange(0x110000)
    if not 0xd800 <= cp <= 0xdfff:
        scalars.append(chr(cp))
unicode_text = "".join(scalars) + "\r\n\rX\n"
def expected(text, encoding, skip):
    result, offset, index = [], skip, 0
    while index < len(text):
        char = text[index]
        begin = offset
        offset += len(char.encode(encoding))
        index += 1
        if char == "\r":
            if index < len(text) and text[index] == "\n":
                offset += len("\n".encode(encoding)); index += 1
            char = "\n"
        result.append((ord(char), begin, offset))
    return result
with tempfile.TemporaryDirectory() as directory:
    path = pathlib.Path(directory) / "text"
    for encoding, mode, bom in [("utf-8", 1, b"\xef\xbb\xbf"), ("utf-16-le", 2, b"\xff\xfe"), ("utf-16-be", 3, b"\xfe\xff")]:
        path.write_bytes(bom + unicode_text.encode(encoding))
        result = subprocess.run([sys.argv[1], str(path), str(mode)], capture_output=True, text=True, check=True)
        rows = [tuple(int(x, 16 if i == 0 else 10) for i, x in enumerate(line.split())) for line in result.stdout.splitlines()]
        assert rows == expected(unicode_text, encoding, len(bom)), encoding
    data, chars = bytearray(), []
    for lead in range(0x81, 0xff):
        for trail in range(0x40, 0xff):
            pair = bytes([lead, trail])
            try:
                char = pair.decode("gbk")
            except UnicodeDecodeError:
                continue
            data.extend(pair); chars.append(char)
    path.write_bytes(data)
    result = subprocess.run([sys.argv[1], str(path), "4"], capture_output=True, text=True, check=True)
    rows = [tuple(int(x, 16 if i == 0 else 10) for i, x in enumerate(line.split())) for line in result.stdout.splitlines()]
    assert rows == expected("".join(chars), "gbk", 0)
    print(f"text: UTF8/UTF16 random scalar/source mapping and all {len(chars)} defined GBK pairs verified")
