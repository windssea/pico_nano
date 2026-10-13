# 在Windows上启动可见的PC模拟器窗口（Docker + WSLg）。 / Launch the visible PC simulator window on Windows (Docker + WSLg).
# 先运行 py tools/dev.py sdl-run 生成Release二进制。 / Run `py tools/dev.py sdl-run` first to build the Release binary.
# 用法 / Usage:  powershell -File tools\run_sim.ps1 [-Books DIR] [-Font TTF] [-FontDir DIR] [-WallpaperDir DIR]
param(
    [string]$Books = "build-dev/sim-library/piconano/books",
    [string]$Font = "",
    [string]$FontDir = "build-dev/sim-library/piconano/fonts",
    [string]$WallpaperDir = "build-dev/sim-library/piconano/wallpapers",
    [string]$Script = "",
    [ValidateRange(0,5000)][int]$PresentDelayMs = 0,
    [ValidateRange(256,6144)][int]$BudgetKiB = 6144,
    [switch]$Timing
)
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not (Test-Path "build-sim-sdl-run/pn_sim")) { Write-Error "缺少 build-sim-sdl-run/pn_sim，请先运行 py tools/dev.py sdl-run"; exit 1 }
# 已运行的窗口保留，避免开发命令意外关闭正在看的页面。/ Keep a running window so development commands do not unexpectedly close the viewed page.
$running = docker ps --filter "name=^/pico-sim$" --format '{{.ID}}'
if ($LASTEXITCODE -ne 0) { Write-Error "Docker不可用，请检查 docker context ls 与 docker version"; exit 1 }
if ($running) { Write-Host "模拟器已运行，保留当前窗口；重建后用 docker stop pico-sim 再启动。"; exit 0 }
# 默认书库可复建，原小说只复制，不把阅读器删除操作指向私人源文件。/ Rebuild the default library and copy the novel so reader deletes never target private originals.
# 使用已有SDL镜像中的Python，避免Windows应用商店别名与解释器差异。/ Use Python in the existing SDL image, avoiding Windows Store aliases and interpreter differences.
docker run --rm --mount "type=bind,source=$root,target=/work" -w /work pico-nano-sdl:dev python3 tools/make_sim_library.py
if ($LASTEXITCODE -ne 0) { Write-Error "测试资源生成失败"; exit 1 }
foreach ($path in @($Books, $FontDir, $WallpaperDir)) {
    if (-not (Test-Path -LiteralPath $path)) { Write-Error "模拟器输入不存在：$path"; exit 1 }
}
# 所有输入都在当前绑定工作区内；转换成容器相对路径。/ All inputs stay in the bound workspace and become container-relative paths.
function Get-SimPath([string]$path) {
    $absolute = (Resolve-Path -LiteralPath $path).Path
    $prefix = $root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $absolute.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "模拟器输入必须位于工作区：$path" }
    return $absolute.Substring($prefix.Length).Replace('\', '/')
}
$Books = Get-SimPath $Books
$FontDir = Get-SimPath $FontDir
$WallpaperDir = Get-SimPath $WallpaperDir
if ($Font) { $Font = Get-SimPath $Font }
New-Item -ItemType Directory -Force -Path "build-dev/sim-frames" | Out-Null
# 阅读状态与封面缓存放在Docker卷（容器内ext4）而不是Windows绑定挂载：后者的fsync和小读取很慢。
# Reading state and the cover cache live in a Docker volume (ext4 inside the VM) rather than the Windows bind mount, whose fsync and small reads are slow.
$arguments = @("run", "--detach", "--rm", "--name", "pico-sim",
    "--mount", "type=bind,source=$root,target=/work", "-w", "/work",
    "-v", "pico-sim-data:/data",
    "-e", "DISPLAY=:0", "-e", "WAYLAND_DISPLAY=wayland-0", "-e", "XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir", "-e", "SDL_VIDEODRIVER=x11",
    "-e", "PN_SIM_FRAME_DIR=/work/build-dev/sim-frames",
    "-e", "PN_SIM_BOOT_PREVIEW=1",
    "-e", "PN_SIM_LIBRARY_SCRIPT=$Script",
    "-e", "PN_SIM_PRESENT_DELAY_MS=$PresentDelayMs",
    "-e", "PN_SIM_TIMING=$([int]$Timing.IsPresent)",
    "-e", "TZ=Asia/Shanghai",
    "-v", "/run/desktop/mnt/host/wslg/.X11-unix:/tmp/.X11-unix", "-v", "/run/desktop/mnt/host/wslg:/mnt/wslg",
    "pico-nano-sdl:dev", "stdbuf", "-oL", "build-sim-sdl-run/pn_sim",
    "--library", $Books, "--budget", "$($BudgetKiB * 1024)", "--state-dir", "/data/state", "--cover-cache", "/data/covers",
    "--wallpaper-dir", $WallpaperDir, "--wallpaper-store", "/data/wallpaper", "--font-dir", $FontDir)
if ($Font) { $arguments += @("--font", $Font) }
& docker @arguments
if ($LASTEXITCODE -ne 0) { Write-Error "模拟器启动失败"; exit 1 }
Start-Sleep -Seconds 2
$running = docker ps --filter "name=^/pico-sim$" --format '{{.ID}}'
if (-not $running) { Write-Error "模拟器已退出，请检查WSLg显示通道及输入文件"; exit 1 }
Write-Host "已启动模拟器窗口；关闭窗口或运行 docker stop pico-sim 结束。"
