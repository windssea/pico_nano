"""汇总同条件阶段诊断，保留失败，不混合设备/主机。 / Summarize matching phase runs, retaining failures and separating backends."""
import argparse
import json
import math
import pathlib
import re


def summarize(text, invalid=None):
    groups = {}
    for line in text.splitlines():
        line = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", line)
        if "profile phase=" not in line:
            continue
        fields = dict(re.findall(r"(\w+)=([^\s]+)", line))
        # 串口环满可能截断/拼接行，保留损失报告，不能冒充有效计时。/ A full serial ring can truncate/join lines; report losses instead of treating them as valid timings.
        try:
            if line.count("profile phase=")!=1:
                raise ValueError("joined records")
            for name in ("phase", "elapsed_us", "status", "peak", "attempts"):
                if name not in fields:
                    raise ValueError("missing "+name)
            for name in ("elapsed_us", "status", "peak", "attempts", "bytes", "reads", "seeks", "hits"):
                if name in fields:
                    int(fields[name])
        except ValueError as exc:
            if invalid is not None:
                invalid.append({"line": line, "error": str(exc)})
            continue
        key = (fields.get("backend", "unknown"), fields["phase"])
        row = groups.setdefault(key, {"times": [], "failures": 0, "peak": 0, "attempts": [], "statuses": {}, "io": {k: 0 for k in ("bytes", "reads", "seeks", "hits")}})
        for counter in row["io"]:
            row["io"][counter] = max(row["io"][counter], int(fields.get(counter, 0)))
        row["times"].append(int(fields["elapsed_us"]))
        row["failures"] += int(fields["status"]) != 0
        status = fields["status"]
        row["statuses"][status] = row["statuses"].get(status, 0) + 1
        row["peak"] = max(row["peak"], int(fields["peak"]))
        row["attempts"].append(int(fields["attempts"]))
    output = []
    for (backend, phase), row in sorted(groups.items()):
        values = sorted(row["times"])
        percentile = lambda p: values[max(0, math.ceil(len(values) * p) - 1)]
        output.append({"backend": backend, "phase": phase, "samples": len(values),
                       "non_ok": row["failures"], "statuses": row["statuses"], "p50_us": percentile(.5),
                       "p95_us": percentile(.95), "max_us": values[-1],
                       "max_pool_phase_bytes": row["peak"], "max_attempts": max(row["attempts"]), "max_io": row["io"]})
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    invalid = []
    content = json.dumps(summarize(args.log.read_text(encoding="utf-8", errors="replace"), invalid), ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(content, encoding="utf-8")
        args.output.with_suffix(".invalid.json").write_text(json.dumps(invalid, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    else:
        print(content, end="")
    if invalid:
        import sys
        print(f"Incomplete serial profile records: {len(invalid)}; excluded from timing statistics", file=sys.stderr)
