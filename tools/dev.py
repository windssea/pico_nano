"""以参数列表调用可复跑开发入口。 / Run reproducible development commands via argument lists."""
import argparse
import pathlib
import subprocess
import sys
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[1]
IDF_IMAGE = "espressif/idf:v6.1@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c"


def prepare_console():
    """管道/终端统一UTF-8，避免Node符号使GBK控制台报错。 / Use UTF-8 for pipes/terminals so Node symbols cannot break a GBK console."""
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")


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
    prepare_console()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["host", "sim", "firmware-ci", "firmware-board", "factory-data", "sdl", "docs", "epub-sample", "sdl-run", "installer", "local-samples", "profile"])
    parser.add_argument("--native", action="store_true", help="Use installed tools instead of the pinned IDF container")
    parser.add_argument("--archive", type=pathlib.Path, help="Local EPUB inside the workspace for epub-sample")
    parser.add_argument("--all-resources", action="store_true", help="Compare every ZIP resource for epub-sample")
    parser.add_argument("--rounds", type=int, default=30, help="Profile rounds, 1-100; use matching samples/builds for comparisons")
    args = parser.parse_args()
    logs = ROOT / "build-dev" / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    docker = not args.native
    container_image = IDF_IMAGE
    if args.action == "profile":
        if not args.archive or not 1 <= args.rounds <= 100:
            parser.error("profile requires --archive BOOK and --rounds 1-100")
        archive = args.archive.resolve()
        if not archive.is_file():
            parser.error("Profile book does not exist")
        try:
            book = archive.relative_to(ROOT).as_posix() if docker else str(archive)
        except ValueError:
            parser.error("Docker sample must be inside the workspace")
        # 每次新建独立状态，不覆盖用户阅读位置。/ Fresh isolated state for every run, preserving user reading progress.
        profile_root = ROOT / "build-dev" / "profile" / uuid.uuid4().hex
        profile_root.mkdir(parents=True)
        state = profile_root.relative_to(ROOT).as_posix() if docker else str(profile_root)
        binary = "build-host/pn_profile_reading" if docker else str(ROOT / "build-host" / ("pn_profile_reading.exe" if sys.platform == "win32" else "pn_profile_reading"))
        commands = [["cmake", "-S", "tests/host", "-B", "build-host", "-G", "Ninja", "-DPN_SANITIZERS=ON"],
                    ["cmake", "--build", "build-host", "--target", "pn_profile_reading"],
                    [binary, book, state, str(args.rounds)]]
    elif args.action == "epub-sample":
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
    elif args.action == "local-samples":
        # 私人样本单独复跑，不作为公共CI依赖，不烧写。/ Private samples run separately from public CI, without flashing.
        container_image = "pico-nano-sdl:dev"
        commands = [["cmake", "-S", "tests/host", "-B", "build-host", "-G", "Ninja", "-DPN_SANITIZERS=ON"],
                    ["cmake", "--build", "build-host", "--target", "font_startup_probe", "font_system_startup_probe", "device_menu_probe", "device_shelf_validate", "device_lock_validate", "test_text_inspect", "font_probe", "font_samples_validate", "zip_dump", "zip_job_validate", "epub_dump", "epub_validate", "epub_app_capture", "test_xhtml_compat", "epub_job_validate", "cover_priority_validate", "cover_resident_validate", "semantic_probe_validate"],
                    ["cmake", "-S", "simulator", "-B", "build-sim-sdl-idf", "-G", "Ninja", "-DPN_SIM_SDL=ON", "-DPN_SANITIZERS=ON"],
                    ["cmake", "--build", "build-sim-sdl-idf"],
                    ["python3" if docker else sys.executable, "tools/test_local_samples.py", "--sim", "build-sim-sdl-idf/pn_sim", "--probe", "build-host/font_probe", "--zip-binary", "build-host/zip_dump"],
                    ["python3" if docker else sys.executable, "tools/test_local_shelf.py", "--sim", "build-sim-sdl-idf/pn_sim"],
                    ["python3" if docker else sys.executable, "tools/test_local_device_shelf.py", "--binary", "build-host/device_shelf_validate"],
                    ["python3" if docker else sys.executable, "tools/test_local_menu_probe.py", "--binary", "build-host/device_menu_probe"],
                    ["python3" if docker else sys.executable, "tools/test_local_menu_probe.py", "--binary", "build-host/device_menu_probe", "--prepare-books", "--baseline", "build-dev/local-menu-probe", "--output", "build-dev/local-menu-prepare"],
                    ["python3" if docker else sys.executable, "tools/test_local_menu_probe.py", "--binary", "build-host/device_menu_probe", "--cancel-system", "--output", "build-dev/local-menu-cancel"],
                    ["python3" if docker else sys.executable, "tools/test_local_menu_probe.py", "--binary", "build-host/device_menu_probe", "--system-failure", "missing", "--recovered-baseline", "build-dev/local-menu-probe", "--output", "build-dev/local-menu-missing"],
                    ["python3" if docker else sys.executable, "tools/test_local_menu_probe.py", "--binary", "build-host/device_menu_probe", "--system-failure", "corrupt", "--recovered-baseline", "build-dev/local-menu-probe", "--output", "build-dev/local-menu-corrupt"],
                    ["python3" if docker else sys.executable, "tools/test_local_root_navigation.py", "--sim", "build-sim-sdl-idf/pn_sim"],
                    ["python3" if docker else sys.executable, "tools/test_local_text_inspect.py", "--binary", "build-host/test_text_inspect"],
                    ["python3" if docker else sys.executable, "tools/test_local_text_decode.py", "--sim", "build-sim-sdl-idf/pn_sim"],
                    ["python3" if docker else sys.executable, "tools/test_local_font_settings.py", "--sim", "build-sim-sdl-idf/pn_sim"],
                    ["python3" if docker else sys.executable, "tools/test_local_fallback_startup.py", "--binary", "build-host/font_startup_probe"],
                    ["python3" if docker else sys.executable, "tools/test_local_system_startup.py", "--binary", "build-host/font_system_startup_probe", "--expect-reuse", "--output", "build-dev/local-system-startup"],
                    ["python3" if docker else sys.executable, "tools/test_local_system_startup.py", "--binary", "build-host/font_system_startup_probe", "--expect-reuse", "--warm-prepare", "--output", "build-dev/local-idle-startup"],
                    ["python3" if docker else sys.executable, "tools/test_local_system_startup.py", "--binary", "build-host/font_startup_probe", "--output", "build-dev/local-system-host-startup"],
                    ["python3" if docker else sys.executable, "tools/test_local_middle_key.py", "--sim", "build-sim-sdl-idf/pn_sim"],
                    ["python3" if docker else sys.executable, "tools/test_local_lock.py", "--binary", "build-host/device_lock_validate"],
                    ["python3" if docker else sys.executable, "tools/test_local_font_samples.py", "--binary", "build-host/font_samples_validate"],
                    ["python3" if docker else sys.executable, "tools/test_local_settings_font.py", "--binary", "build-host/test_settings_font"],
                    ["python3" if docker else sys.executable, "tools/test_local_mimetype.py", "--binary", "build-host/epub_dump"],
                    ["python3" if docker else sys.executable, "tools/test_local_memory.py", "--sim", "build-sim-sdl-idf/pn_sim"],
                    ["python3" if docker else sys.executable, "tools/test_local_cache.py", "--sim", "build-sim-sdl-idf/pn_sim"],
                    ["python3" if docker else sys.executable, "tools/test_local_unsupported.py", "--sim", "build-sim-sdl-idf/pn_sim"],
                    ["python3" if docker else sys.executable, "tools/test_local_chapters.py", "--binary", "build-host/epub_validate"],
                    ["python3" if docker else sys.executable, "tools/test_local_publication.py", "--binary", "build-host/epub_job_validate"],
                    ["python3" if docker else sys.executable, "tools/test_local_zip_job.py", "--binary", "build-host/zip_job_validate"],
                    ["python3" if docker else sys.executable, "tools/test_local_cover_priority.py", "--binary", "build-host/cover_priority_validate"],
                    ["python3" if docker else sys.executable, "tools/test_local_cover_priority.py", "--binary", "build-host/cover_priority_validate", "--rebind-before-write", "--output", "build-dev/local-cover-priority-rebind"],
                    ["python3" if docker else sys.executable, "tools/test_local_cover_resident.py", "--binary", "build-host/cover_resident_validate"],
                    ["python3" if docker else sys.executable, "tools/test_local_cover_fit.py", "--capture", "build-host/epub_app_capture"],
                    ["python3" if docker else sys.executable, "tools/test_local_semantic_probe.py", "--capture", "build-host/epub_app_capture", "--probe", "build-host/semantic_probe_validate"],
                    ["python3" if docker else sys.executable, "tools/test_local_compat.py", "--filter", "build-host/test_xhtml_compat", "--validate", "build-host/epub_validate", "--capture", "build-host/epub_app_capture"]]
    elif args.action == "docs":
        commands = [[sys.executable, "tools/check_docs.py"]]
        docker = False
    elif args.action == "installer":
        # 网页安装器：测试写入计划并生成 build-dev/installer-site；不连接设备。/ Web installer: test the plan and build the site; no device access.
        commands = [["node", "--test", "--test-reporter=tap", "tests/installer/plan.test.mjs"],
                    [sys.executable, "-m", "unittest", "discover", "-s", "tests/installer", "-p", "test_*.py", "-v"],
                    [sys.executable, "tools/installer_site.py"]]
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
    profile_status = 0
    local_status = 0
    for index, command in enumerate(commands):
        status = run(command, docker, logs / f"{args.action}-{index}.log", container_image)
        if status:
            if args.action == "profile" and index == 2:
                profile_status = status
            elif args.action == "local-samples" and index >= 4:
                # 样本失败仍完成其他矩阵，最终保留非零退出。/ Finish other corpus matrices after a sample failure, retaining a nonzero final exit.
                local_status = status
            else:
                return status
    if args.action == "profile":
        from summarize_profile import summarize
        import json
        import hashlib
        report = profile_root / "summary.json"
        with archive.open("rb") as source:
            identity = hashlib.file_digest(source, "sha256").hexdigest()
        result = {"backend": "host", "condition": "fresh_session_isolated_state_os_cache_uncontrolled", "sanitizers": True,
                  "rounds": args.rounds, "budget_bytes": 6 * 1024 * 1024, "book_sha256": identity,
                  "book_bytes": archive.stat().st_size, "exit_code": profile_status,
                  "phases": summarize((logs / "profile-2.log").read_text(encoding="utf-8", errors="replace"))}
        report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"Profile summary: {report}", flush=True)
        return profile_status
    if args.action == "host":
        # 浏览器异步行为用实际网页源与Node验证，独立于容器的Python/C运行时。/ Verify browser async behavior against actual page source in Node, independently of container Python/C runtimes.
        return run(["node", "--test", "tests/tools/test_transfer_resources.mjs"], False, logs / "host-web.log")
    return local_status


if __name__ == "__main__":
    raise SystemExit(main())
