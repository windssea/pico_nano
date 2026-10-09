"""以参数列表调用可复跑开发入口。 / Run reproducible development commands via argument lists."""
import argparse
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
IDF_IMAGE = "espressif/idf:v6.1@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c"


def run(command, docker, log, container_image=IDF_IMAGE):
    if docker:
        command = ["docker", "run", "--rm", "--mount", f"type=bind,source={ROOT},target=/work",
                   "-w", "/work", container_image, *command]
    print("Running:", subprocess.list2cmdline(command), flush=True)
    with log.open("w", encoding="utf-8") as output:
        try:
            result = subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT, check=False)
        except OSError as exc:
            print(str(exc), file=sys.stderr)
            return 1
    print("\n".join(log.read_text(encoding="utf-8", errors="replace").splitlines()[-24:]))
    print(f"Full log: {log}", flush=True)
    return result.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["host", "sim", "firmware-ci", "firmware-board", "factory-data", "sdl", "docs", "epub-sample", "sdl-run"])
    parser.add_argument("--native", action="store_true", help="Use installed tools instead of the pinned IDF container")
    parser.add_argument("--archive", type=pathlib.Path, help="Local EPUB inside the workspace for epub-sample")
    parser.add_argument("--all-resources", action="store_true", help="Compare every ZIP resource for epub-sample")
    args = parser.parse_args()
    logs = ROOT / "build-dev" / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    docker = not args.native
    container_image = IDF_IMAGE
    if args.action == "epub-sample":
        if not args.archive:
            parser.error("epub-sample requires --archive")
        archive = args.archive.resolve()
        if not archive.is_file():
            parser.error("EPUB file does not exist")
        if docker:
            try:
                sample_path = archive.relative_to(ROOT).as_posix()
            except ValueError:
                parser.error("Docker sample must be inside the workspace")
        else:
            sample_path = str(archive)
        binary = "build-host/zip_dump" if docker else str(ROOT / "build-host" / ("zip_dump.exe" if sys.platform == "win32" else "zip_dump"))
        sample = ["python3" if docker else sys.executable, "tools/test_epub_sample.py", sample_path,
                  "--binary", binary, "--report", "build-dev/epub-sample/report.json"]
        if args.all_resources:
            sample.append("--all")
        commands = [["cmake", "-S", "tests/host", "-B", "build-host", "-G", "Ninja", "-DPN_SANITIZERS=ON"],
                    ["cmake", "--build", "build-host", "--target", "zip_dump"], sample]
    elif args.action == "docs":
        commands = [[sys.executable, "tools/check_docs.py"]]
        docker = False
    elif args.action in ("sdl", "sdl-run"):
        if docker:
            container_image = "pico-nano-sdl:dev"
            status = run(["docker", "build", "--tag", container_image, str(ROOT / "tools/docker")], False, logs / "sdl-image.log")
            if status:
                return status
        if args.action == "sdl-run":
            # 给人用的可见窗口：Release、无sanitizer，响应速度接近真实；不跑测试。/ For people using the visible window: Release without sanitizers for realistic speed; no tests.
            commands = [["cmake", "-S", "simulator", "-B", "build-sim-sdl-run", "-G", "Ninja", "-DPN_SIM_SDL=ON", "-DCMAKE_BUILD_TYPE=Release"],
                        ["cmake", "--build", "build-sim-sdl-run"]]
        else:
            commands = [["cmake", "-S", "simulator", "-B", "build-sim-sdl-idf", "-G", "Ninja", "-DPN_SIM_SDL=ON", "-DPN_SANITIZERS=ON"],
                        ["cmake", "--build", "build-sim-sdl-idf"], ["ctest", "--test-dir", "build-sim-sdl-idf", "--output-on-failure"]]
    elif args.action == "host":
        commands = [["cmake", "-S", "tests/host", "-B", "build-host", "-G", "Ninja", "-DPN_SANITIZERS=ON"],
                    ["cmake", "--build", "build-host"], ["ctest", "--test-dir", "build-host", "--output-on-failure"],
                    ["python3", "-m", "unittest", "discover", "-s", "tests/tools", "-v"]]
    elif args.action == "factory-data":
        commands = [["cmake", "-S", "tests/host", "-B", "build-host", "-G", "Ninja", "-DPN_SANITIZERS=ON"],
                    ["cmake", "--build", "build-host", "--target", "pn_fs_image"],
                    ["./build-host/pn_fs_image", "build-dev/factory-data.bin"],
                    ["./build-host/pn_fs_image", "build-dev/factory-wallpaper.bin", "wallpaper"]]
    elif args.action == "sim":
        (ROOT / "build-sim" / "artifacts").mkdir(parents=True, exist_ok=True)
        commands = [["cmake", "-S", "simulator", "-B", "build-sim-idf", "-G", "Ninja", "-DPN_SANITIZERS=ON"],
                    ["cmake", "--build", "build-sim-idf"], ["ctest", "--test-dir", "build-sim-idf", "--output-on-failure"],
                    ["./build-sim-idf/pn_sim", "--headless", "--capture", "build-sim/artifacts/pattern.pgm"]]
    else:
        profile = "ci" if args.action == "firmware-ci" else "defaults"
        build = "build-ci-s3" if profile == "ci" else "build-board"
        sdkconfig = f"/work/{build}/sdkconfig" if docker else (ROOT / build / "sdkconfig").as_posix()
        base = ["idf.py", "-B", build, "-DIDF_TARGET=esp32s3", f"-DSDKCONFIG={sdkconfig}",
                f"-DSDKCONFIG_DEFAULTS=sdkconfig.{profile}"]
        commands = [base + ["build"], base + ["size"]]
    for index, command in enumerate(commands):
        status = run(command, docker, logs / f"{args.action}-{index}.log", container_image)
        if status:
            return status
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
