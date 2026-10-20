"""核验新正式包后移除Gitee旧发行记录，保留源码标签。 / Retire older Gitee releases only after verifying the new stable packages; retain source tags."""
import argparse
import json
import os
import pathlib
import re
import urllib.error
import urllib.request
from sync_gitee_release import API, ROOT, github_release, read_bundle, sync

REPO = "windssea/pico_nano"


def version(tag):
    """仅接受明确的三段产品标签。 / Accept explicit three-part product tags only."""
    if not isinstance(tag, str) or not re.fullmatch(r"v(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)", tag):
        raise ValueError("Expected a canonical product tag")
    return tuple(map(int, tag[1:].split(".")))


def candidates(rows, keep):
    """完整验证快照后选出较旧非草稿记录，不选择标签或源码。 / Validate the entire snapshot before selecting older non-draft releases, never tags or source."""
    threshold = version(keep)
    if not isinstance(rows, list):
        raise ValueError("Invalid release inventory")
    ids, tags, selected = set(), set(), []
    for row in rows:
        if not isinstance(row, dict) or type(row.get("id")) is not int or row["id"] <= 0:
            raise ValueError("Invalid release identity")
        tag = row.get("tag_name")
        if not isinstance(tag, str) or row["id"] in ids or tag in tags:
            raise ValueError("Ambiguous release inventory")
        ids.add(row["id"])
        tags.add(tag)
        try:
            older = version(tag) < threshold
        except ValueError:
            older = False
        if older and not row.get("draft"):
            selected.append({"id": row["id"], "tag_name": tag})
    return selected


def inventory(api):
    """先完整列出所有页，再允许任何删除。 / Enumerate every bounded page before allowing any deletion."""
    result = []
    for page in range(1, 102):
        batch = api.request(f"/repos/{REPO}/releases?page={page}&per_page=100")
        if not isinstance(batch, list):
            raise ValueError("Invalid Gitee release page")
        result.extend(batch)
        if len(batch) < 100:
            return result
    raise ValueError("Gitee release inventory exceeds bounded pagination")


def delete_release(api, item):
    """删除前复核标签与ID，删除后读回；不删除git标签。 / Recheck tag/ID before deletion and read back afterwards; never delete git tags."""
    tag = item["tag_name"]
    path = f"/repos/{REPO}/releases/tags/{tag}"
    current = api.request(path)
    if current is None:
        return "already-absent"
    if current.get("id") != item["id"] or current.get("tag_name") != tag:
        raise ValueError("Release identity changed; refusing deletion")
    headers = {"Accept": "application/json", "User-Agent": "PicoNano-release-retention", "Authorization": f"Bearer {api.token}"}
    req = urllib.request.Request(f"{api.origin}/repos/{REPO}/releases/{item['id']}", headers=headers, method="DELETE")
    try:
        with api.opener.open(req, timeout=120) as response:
            if response.status not in (200, 204):
                raise ValueError("Unexpected Gitee deletion status")
    except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError):
        raise ValueError("Gitee deletion failed; re-read inventory before retrying") from None
    if api.request(path) is not None:
        raise ValueError("Deleted release remains visible")
    return "deleted"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keep-tag", required=True)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--report", type=pathlib.Path, required=True)
    args = parser.parse_args()
    version(args.keep_tag)
    bundle = read_bundle(ROOT, args.keep_tag)
    github = github_release(bundle, REPO)
    if github.get("prerelease") or github.get("draft"):
        raise ValueError("Keeper must be a published stable GitHub release")
    token = os.environ.get("GITEE_TOKEN") if args.apply else None
    if args.apply and not token:
        raise ValueError("GITEE_TOKEN must be supplied through the environment")
    api = API("https://gitee.com/api/v5", token)
    keeper = api.request(f"/repos/{REPO}/releases/tags/{args.keep_tag}")
    if not keeper or keeper.get("tag_name") != args.keep_tag or keeper.get("prerelease") or keeper.get("draft"):
        raise ValueError("Keeper must be a published stable Gitee release")
    verified = sync(bundle, REPO, github, api, verify_only=True)
    rows = inventory(api)
    plan = candidates(rows, args.keep_tag)
    report = {"keep_tag": args.keep_tag, "verified_keeper": verified, "inventory_before": rows, "plan": plan, "removed": [], "apply": args.apply}
    def save():
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    save()
    if args.apply:
        for item in plan:
            status = delete_release(api, item)
            report["removed"].append({**item, "status": status})
            save()
        remaining = inventory(api)
        if candidates(remaining, args.keep_tag) or not any(r.get("tag_name") == args.keep_tag for r in remaining):
            raise ValueError("Final inventory failed retention checks")
        report["inventory_after"] = remaining
        report["passed"] = True
        save()
    print(f"Keeper {args.keep_tag}; planned {len(plan)}; removed {len(report['removed'])}")


if __name__ == "__main__":
    main()
