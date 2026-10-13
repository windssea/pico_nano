"""汇总同条件阶段诊断，保留失败，不混合设备/主机。 / Summarize matching phase runs, retaining failures and separating backends."""
import argparse
import json
import math
import pathlib
import re


def summarize(text):
    groups = {}
    for line in text.splitlines():
        line = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", line)
        if "profile phase=" not in line:
            continue
        fields = dict(re.findall(r"(\w+)=([^\s]+)", line))
        key = (fields.get("backend", "unknown"), fields["phase"])
        row = groups.setdefault(key, {"times": [], "failures": 0, "peak": 0, "attempts": [], "statuses": {}})
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
                       "max_pool_phase_bytes": row["peak"], "max_attempts": max(row["attempts"])})
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    content = json.dumps(summarize(args.log.read_text(encoding="utf-8", errors="replace")), ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(content, encoding="utf-8")
    else:
        print(content, end="")
