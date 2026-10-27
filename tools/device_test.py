"""诊断镜像的串口自动测试会话。 / Serial automation session for the diagnostic image.

打开串口会触发一次USB复位，所以由`serve`常驻持有端口，其余子命令经本机TCP发给它。
Opening the port triggers one USB reset, so `serve` keeps the port open and other subcommands talk to it over local TCP.

只适用于`tools/dev.py firmware-diag`构建的`-diag`镜像；时间为设备esp_timer毫秒。
Only for `-diag` images built by `tools/dev.py firmware-diag`; times are device esp_timer milliseconds.
"""
import argparse
import base64
import json
import pathlib
import re
import socket
import socketserver
import struct
import sys
import threading
import time
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
HOST, PORT = "127.0.0.1", 47821
WIDTH, HEIGHT, BOARD_ROW = 684, 1216, 608
FRAME_BYTES = WIDTH * HEIGHT // 2
GESTURES = ("tap", "long", "swipe", "key")
# 产品日志里与时延/异常相关的行一并返回，便于对照。/ Product log lines about timing or faults are returned alongside.
PRODUCT_LOG = re.compile(r"Whole-book index|Present |Display stages|Book prepare|Prepared book|Reader action|Cache file|System font open|Actions |underrun|Wake key|Locking|Input first|profile phase|stack phase|rst:|Guru|abort|panic|assert", re.I)
PNT = re.compile(r"PNT (\d+) (.*)$")


def board_to_gray(board):
    """板级1216×684反向竖屏4bpp还原为684×1216的8bit灰度。 / Restore the board's inverted-portrait 4bpp layout to 684×1216 8-bit gray."""
    out = bytearray(WIDTH * HEIGHT)
    for x in range(WIDTH):
        row = board[(WIDTH - 1 - x) * BOARD_ROW:(WIDTH - x) * BOARD_ROW]
        for half, byte in enumerate(row):
            y = half * 2
            out[y * WIDTH + x] = (byte & 15) * 17
            out[(y + 1) * WIDTH + x] = (byte >> 4) * 17
    return bytes(out)


def write_png(path, gray, width=WIDTH, height=HEIGHT):
    raw = b"".join(b"\x00" + gray[y * width:(y + 1) * width] for y in range(height))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


class Session:
    """持有串口，记录原始日志并按序号匹配回执。 / Own the port, log raw lines and match replies by sequence."""

    def __init__(self, port, out):
        import serial
        self.out = out
        out.mkdir(parents=True, exist_ok=True)
        self.raw = (out / "serial.log").open("a", encoding="utf-8", errors="replace")
        self.lines = []  # (host_time, text)
        self.cond = threading.Condition()
        self.seq = int(time.time()) % 100000 * 10
        self.lock = threading.Lock()  # 一次只执行一个请求 / One request at a time
        self.serial = serial.Serial()
        self.serial.port, self.serial.baudrate, self.serial.timeout = port, 115200, 0.2
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.open()
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        pending = b""
        while True:
            try:
                data = self.serial.read(4096)
            except Exception as exc:  # 端口消失（USB传书/复位） / Port vanished (USB transfer/reset)
                self._append(f"HOST serial error: {exc}")
                time.sleep(1)
                try:
                    self.serial.close()
                    self.serial.open()
                    self._append("HOST serial reopened")
                except Exception:
                    pass
                continue
            if not data:
                continue
            pending += data
            *complete, pending = pending.split(b"\n")
            for line in complete:
                self._append(line.decode("utf-8", errors="replace").rstrip("\r"))

    def _append(self, text):
        now = time.time()
        self.raw.write(f"{now:.3f} {text}\n")
        self.raw.flush()
        with self.cond:
            self.lines.append((now, text))
            self.cond.notify_all()

    def _wait(self, start, predicate, timeout):
        """从索引start起等待predicate返回非None。 / Wait from index start until predicate returns non-None."""
        deadline = time.time() + timeout
        index = start
        with self.cond:
            while True:
                while index < len(self.lines):
                    result = predicate(self.lines[index][1])
                    index += 1
                    if result is not None:
                        return result, index
                left = deadline - time.time()
                if left <= 0:
                    return None, index
                self.cond.wait(left)

    def _send(self, command):
        self.seq += 1
        with self.cond:
            mark = len(self.lines)
        self.serial.write(f"{self.seq} {command}\n".encode())
        return self.seq, mark

    @staticmethod
    def _reply(seq, text):
        match = PNT.search(text)
        if match and int(match.group(1)) in (seq, 0) and not match.group(2).startswith(("display", "ready")):
            if int(match.group(1)) == seq or match.group(2).startswith("err"):
                return match.group(2)
        return None

    def displays(self, start_index, end_index):
        out = []
        for _, text in self.lines[start_index:end_index]:
            match = PNT.search(text)
            if match and match.group(1) == "0" and match.group(2).startswith("display"):
                fields = dict(item.split("=", 1) for item in match.group(2).split()[1:])
                out.append({k: (v if k == "kind" else int(v)) for k, v in fields.items()})
        return out

    def logs(self, start_index, end_index):
        return [text for _, text in self.lines[start_index:end_index] if "PNT " not in text and PRODUCT_LOG.search(text)]

    def command(self, command, quiet_ms=1500, timeout=30.0, first_ms=8000):
        """执行一条指令；手势等done后再等显示静默quiet_ms。 / Run one command; gestures wait for done and then for quiet_ms without display jobs."""
        with self.lock:
            verb = command.split()[0]
            if verb == "shot":
                raise ValueError("use shot()")
            seq, mark = self._send(command)
            reply, index = self._wait(mark, lambda t: self._reply(seq, t), 5.0)
            result = {"command": command, "seq": seq, "reply": reply}
            if reply is None or reply.startswith("err") or verb not in GESTURES:
                return result

            def done(text):
                match = PNT.search(text)
                if match and int(match.group(1)) == seq and match.group(2).startswith(("done", "err")):
                    return match.group(2)
                return None
            finish, index = self._wait(index, done, timeout)
            result["finish"] = finish
            if not finish or finish.startswith("err"):
                return result
            fields = dict(item.split("=", 1) for item in finish.split()[1:])
            start, end = int(fields["start"]), int(fields["end"])
            # 先等首个显示（慢页可能超过静默窗口），之后quiet_ms内没有新display记录即静默；无显示最多等first_ms。
            # Wait for the first display first (slow pages can outlast the quiet window), then quiet means no new display record within quiet_ms; without any display wait at most first_ms.
            last_seen = time.time()
            deadline = time.time() + timeout
            first_deadline = time.time() + first_ms / 1000
            count = 0
            while time.time() < deadline:
                with self.cond:
                    self.cond.wait(0.1)
                    now_count = len([d for d in self.displays(mark, len(self.lines)) if d["end"] >= start])
                if now_count != count:
                    count, last_seen = now_count, time.time()
                if (count and time.time() - last_seen >= quiet_ms / 1000) or (not count and time.time() >= first_deadline):
                    break
            with self.cond:
                stop = len(self.lines)
            displays = [d for d in self.displays(mark, stop) if d["end"] >= start]
            result.update({"start": start, "release": end, "displays": displays, "logs": self.logs(mark, stop)})
            if displays:
                result["first_display_ms"] = displays[0]["end"] - start
                result["last_display_ms"] = displays[-1]["end"] - start
                result["scan_ms_total"] = sum(d["ms"] for d in displays)
            return result

    def shot(self, path, timeout=60.0):
        with self.lock:
            seq, mark = self._send("shot")
            chunks, info = {}, {}

            def collect(text):
                match = PNT.search(text)
                if not match or int(match.group(1)) not in (seq, 0):
                    return None
                body = match.group(2)
                if body.startswith("err") and int(match.group(1)) == seq:
                    return body
                if int(match.group(1)) != seq or not body.startswith("shot "):
                    return None
                parts = body.split()
                if parts[1] == "begin":
                    info.update(item.split("=", 1) for item in parts[2:])
                elif parts[1] == "end":
                    info.update(item.split("=", 1) for item in parts[2:])
                    return "end"
                elif len(parts) == 4:
                    data = base64.b64decode(parts[3])
                    if f"{zlib.crc32(data):08x}" == parts[2]:
                        chunks[int(parts[1])] = data
                return None
            status, _ = self._wait(mark, collect, timeout)
            if status != "end":
                return {"error": status or "timeout"}
            board = b"".join(chunks[k] for k in sorted(chunks))
            if len(board) != FRAME_BYTES or f"{zlib.crc32(board):08x}" != info.get("crc"):
                return {"error": "corrupt", "bytes": len(board), "chunks": len(chunks)}
            write_png(pathlib.Path(path), board_to_gray(board))
            return {"path": str(path), "known": info.get("known") == "1", "stream_ms": int(info.get("ms", 0))}


def serve(args):
    out = args.out or ROOT / "build-dev" / "device-test" / time.strftime("%Y%m%d-%H%M%S")
    session = Session(args.port, pathlib.Path(out))

    class Handler(socketserver.StreamRequestHandler):
        def handle(self):
            request = json.loads(self.rfile.readline())
            try:
                if request.get("shot"):
                    result = session.shot(request["shot"])
                elif request.get("wait"):
                    # 只在主机端等待日志，不向设备发指令，避免轮询干扰扫描。/ Wait on host-side logs only, sending nothing to the device so polling cannot disturb scans.
                    pattern = re.compile(request["wait"])
                    with session.cond:
                        start = len(session.lines) if request.get("new") else 0
                    found, _ = session._wait(start, lambda t: t if pattern.search(t) else None, float(request.get("timeout", 60)))
                    result = {"found": found}
                elif request.get("tail"):
                    with session.cond:
                        result = {"lines": [t for _, t in session.lines[-int(request["tail"]):]]}
                else:
                    result = session.command(request["command"], request.get("quiet_ms", 1500), request.get("timeout", 30.0), request.get("first_ms", 8000))
            except Exception as exc:
                result = {"error": repr(exc)}
            with (session.out / "results.jsonl").open("a", encoding="utf-8") as log:
                log.write(json.dumps(result, ensure_ascii=False) + "\n")
            self.wfile.write((json.dumps(result, ensure_ascii=False) + "\n").encode())

    socketserver.ThreadingTCPServer.allow_reuse_address = True
    with socketserver.ThreadingTCPServer((HOST, PORT), Handler) as server:
        print(f"serving {args.port} on {HOST}:{PORT}, log {session.out}", flush=True)
        server.serve_forever()


def request(payload, timeout=120.0):
    with socket.create_connection((HOST, PORT), timeout=timeout) as sock:
        sock.sendall((json.dumps(payload) + "\n").encode())
        data = b""
        while not data.endswith(b"\n"):
            part = sock.recv(65536)
            if not part:
                break
            data += part
    return json.loads(data)


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="action", required=True)
    s = sub.add_parser("serve", help="Hold the serial port and accept local requests")
    s.add_argument("--port", default="COM3")
    s.add_argument("--out", type=pathlib.Path)
    c = sub.add_parser("cmd", help="Run commands in order, e.g. 'tap 85 1150' 'key middle'")
    c.add_argument("commands", nargs="+")
    c.add_argument("--quiet-ms", type=int, default=1500, help="Display-idle window after a gesture")
    c.add_argument("--brief", action="store_true", help="Print one summary line per command")
    p = sub.add_parser("shot", help="Save the shown panel reference as PNG")
    p.add_argument("path", type=pathlib.Path)
    w = sub.add_parser("wait", help="Wait host-side until a log line matches, without polling the device")
    w.add_argument("pattern")
    w.add_argument("--timeout", type=float, default=60)
    w.add_argument("--new", action="store_true", help="Only lines arriving after this request")
    t = sub.add_parser("tail", help="Print the last raw lines")
    t.add_argument("count", type=int, nargs="?", default=40)
    args = parser.parse_args()
    if args.action == "serve":
        return serve(args)
    if args.action == "shot":
        print(json.dumps(request({"shot": str(args.path.resolve())}), ensure_ascii=False))
        return 0
    if args.action == "wait":
        print(json.dumps(request({"wait": args.pattern, "timeout": args.timeout, "new": args.new}, timeout=args.timeout + 10), ensure_ascii=False))
        return 0
    if args.action == "tail":
        print("\n".join(request({"tail": args.count})["lines"]))
        return 0
    for command in args.commands:
        result = request({"command": command, "quiet_ms": args.quiet_ms})
        if args.brief:
            print(f"{command:28} reply={result.get('reply')} first={result.get('first_display_ms')} last={result.get('last_display_ms')} "
                  f"jobs={len(result.get('displays', []))} scan={result.get('scan_ms_total')}")
        else:
            print(json.dumps(result, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
