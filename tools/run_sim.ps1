# 在Windows上启动可见的PC模拟器窗口（Docker + WSLg）。 / Launch the visible PC simulator window on Windows (Docker + WSLg).
# 先运行 python tools/dev.py sdl-run 生成Release二进制。 / Run `python tools/dev.py sdl-run` first to build the Release binary.
# 用法 / Usage:  powershell -File tools\run_sim.ps1 [-Books DIR] [-Font TTF] [-FontDir DIR] [-WallpaperDir DIR]
param(
    [string]$Books = "build-dev/demo/books",
    [string]$Font = "build-dev/demo/simhei.ttf",
    [string]$FontDir = "build-dev/demo/fonts",
    [string]$WallpaperDir = "build-dev/demo/wallpapers"
)
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not (Test-Path "build-sim-sdl-run/pn_sim")) { Write-Error "缺少 build-sim-sdl-run/pn_sim，请先运行 python tools/dev.py sdl-run"; exit 1 }
docker rm -f pico-sim 2>$null | Out-Null
# 阅读状态与封面缓存放在Docker卷（容器内ext4）而不是Windows绑定挂载：后者的fsync和小读取很慢。
# Reading state and the cover cache live in a Docker volume (ext4 inside the VM) rather than the Windows bind mount, whose fsync and small reads are slow.
$arguments = @("run", "--rm", "--name", "pico-sim",
    "--mount", "type=bind,source=$root,target=/work", "-w", "/work",
    "-v", "pico-sim-data:/data",
    "-e", "DISPLAY=:0", "-e", "WAYLAND_DISPLAY=wayland-0", "-e", "XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir", "-e", "SDL_VIDEODRIVER=x11",
    "-v", "/run/desktop/mnt/host/wslg/.X11-unix:/tmp/.X11-unix", "-v", "/run/desktop/mnt/host/wslg:/mnt/wslg",
    "pico-nano-sdl:dev", "build-sim-sdl-run/pn_sim",
    "--library", $Books, "--font", $Font, "--state-dir", "/data/state", "--cover-cache", "/data/covers",
    "--wallpaper-dir", $WallpaperDir, "--wallpaper-store", "/data/wallpaper", "--font-dir", $FontDir)
Start-Process -FilePath docker -ArgumentList $arguments -WindowStyle Hidden
Write-Host "已启动模拟器窗口；关闭窗口或运行 docker stop pico-sim 结束。"
