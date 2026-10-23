# 在Windows上启动可见的PC模拟器窗口（Docker + WSLg）。 / Launch the visible PC simulator window on Windows (Docker + WSLg).
# 先运行 py tools/dev.py sdl-run 生成Release二进制。 / Run `py tools/dev.py sdl-run` first to build the Release binary.
# 用法 / Usage:  powershell -File tools\run_sim.ps1 [-Books DIR] [-Font TTF] [-FontDir DIR] [-WallpaperDir DIR]
param(
    [string]$Books = "tests/testBook",
    [string]$Font = "",
    [string]$FontDir = "tests/testFont",
    [string]$WallpaperDir = "",
    [string]$BackgroundDir = "",
    [string]$Script = "",
    [ValidatePattern('^$|^[a-z0-9][a-z0-9-]{0,47}$')][string]$CoverCacheNamespace = "",
    [ValidateRange(0,5000)][int]$PresentDelayMs = 0,
    [ValidateRange(256,6144)][int]$BudgetKiB = 6144,
    [switch]$Timing,
    [switch]$Demo,
    [switch]$NoReadingCache,
    [switch]$Feedback,
    [switch]$Follow
)
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not (Test-Path "build-sim-sdl-run/pn_sim")) { Write-Error "缺少 build-sim-sdl-run/pn_sim，请先运行 py tools/dev.py sdl-run"; exit 1 }
# 已运行的窗口保留，避免开发命令意外关闭正在看的页面。/ Keep a running window so development commands do not unexpectedly close the viewed page.
$running = docker ps --filter "name=^/pico-sim$" --format '{{.ID}}'
if ($LASTEXITCODE -ne 0) { Write-Error "Docker不可用，请检查 docker context ls 与 docker version"; exit 1 }
if ($running) { Write-Host "模拟器已运行，保留当前窗口；重建后用 docker stop pico-sim 再启动。"; exit 0 }
# 原创演示库不依赖私人资源，并使用独立状态。/ Original demo inputs need no private resources and use separate state.
$stateBase = '/data/user-samples'
if ($Demo) {
    docker run --rm --mount "type=bind,source=$root,target=/work" -w /work pico-nano-sdl:dev python3 tools/make_sim_library.py --out build-dev/demo-library/piconano/books --private-book build-dev/demo-library/no-private-book.epub --local-inputs build-dev/demo-library/no-private-inputs.json
    if ($LASTEXITCODE -ne 0) { Write-Error "演示资源生成失败"; exit 1 }
    $Books = 'build-dev/demo-library/piconano/books'
    $FontDir = 'build-dev/demo-library/piconano/fonts'
    if (-not $WallpaperDir) { $WallpaperDir = 'build-dev/demo-library/piconano/wallpapers' }
    $stateBase = '/data/demo-samples'
}
foreach ($path in @($Books, $FontDir)) {
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
# 真实用户原件只读，所有删除/覆盖演练只作用于新建副本。/ Keep user originals read-only; deletion/overwrite journeys operate on fresh copies.
docker run --rm --mount "type=bind,source=$root,target=/work" -w /work pico-nano-sdl:dev python3 tools/prepare_local_samples.py --books $Books --fonts $FontDir
if ($LASTEXITCODE -ne 0) { Write-Error "用户测试资源复制失败"; exit 1 }
$samples = Get-Content -LiteralPath 'build-dev/user-samples/current.json' -Raw -Encoding UTF8 | ConvertFrom-Json
# 每次仍用新副本，但容器内路径固定，封面缓存和字体选择才能跨窗口复用。/ Keep fresh copies but stable in-container paths so cover caches and font choices survive window restarts.
$sampleRelative = Get-SimPath $samples.root
$sampleRoot = (Resolve-Path -LiteralPath $sampleRelative).Path
$sourceFontDir = (Resolve-Path -LiteralPath $FontDir).Path.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
$Books = '/samples/books'
$FontDir = '/samples/fonts'
if ($WallpaperDir) { $WallpaperDir = Get-SimPath $WallpaperDir } else { $WallpaperDir = '/samples/wallpapers' }
if ($BackgroundDir) { $BackgroundDir = Get-SimPath $BackgroundDir } else { $BackgroundDir = '/samples/backgrounds' }
if ($Font) {
    $sourceFont = (Resolve-Path -LiteralPath $Font).Path
    if ($sourceFont.StartsWith($sourceFontDir, [StringComparison]::OrdinalIgnoreCase)) { $Font = '/samples/fonts/' + $sourceFont.Substring($sourceFontDir.Length).Replace('\', '/') }
    else { $Font = Get-SimPath $Font }
}
# 用户样本与早期生成样本分开保留状态；不删除旧进度或缓存。/ Keep user-sample state separate from earlier generated fixtures without deleting old progress or caches.
docker run --rm -v pico-sim-data:/data --entrypoint python3 pico-nano-sdl:dev -c "import pathlib; [pathlib.Path(p).mkdir(parents=True,exist_ok=True) for p in ('$stateBase/state','$stateBase/covers','$stateBase/wallpaper','$stateBase/backgrounds')]"
if ($LASTEXITCODE -ne 0) { Write-Error "用户样本状态目录初始化失败"; exit 1 }

# 开发冷缓存对照使用独立命名空间，保留默认缓存与阅读状态。/ Use an isolated namespace for development cold-cache comparisons, preserving default caches and reading state.
$coverCacheDirectory = "$stateBase/covers"
if ($CoverCacheNamespace) { $coverCacheDirectory = "$stateBase/covers-$CoverCacheNamespace" }
New-Item -ItemType Directory -Force -Path "build-dev/sim-frames" | Out-Null
# 阅读状态与封面缓存放在Docker卷（容器内ext4）而不是Windows绑定挂载：后者的fsync和小读取很慢。
# Reading state and the cover cache live in a Docker volume (ext4 inside the VM) rather than the Windows bind mount, whose fsync and small reads are slow.
$arguments = @("run", "--detach", "--rm", "--name", "pico-sim",
    "--mount", "type=bind,source=$root,target=/work", "-w", "/work",
    "--mount", "type=bind,source=$sampleRoot,target=/samples",
    "-v", "pico-sim-data:/data",
    "-e", "DISPLAY=:0", "-e", "WAYLAND_DISPLAY=wayland-0", "-e", "XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir", "-e", "SDL_VIDEODRIVER=x11",
    "-e", "PN_SIM_FRAME_DIR=/work/build-dev/sim-frames",
    "-e", "PN_SIM_BOOT_PREVIEW=1",
    "-e", "PN_BACKGROUND_DIR=$BackgroundDir", "-e", "PN_BACKGROUND_CACHE=$stateBase/backgrounds",
    "-e", "PN_SIM_LIBRARY_SCRIPT=$Script",
    "-e", "PN_SIM_PRESENT_DELAY_MS=$PresentDelayMs",
    "-e", "PN_SIM_TIMING=$([int]$Timing.IsPresent)",
    "-e", "PN_SIM_FEEDBACK=$([int]$Feedback.IsPresent)",
    "-e", "PN_SIM_FOLLOW=$([int]$Follow.IsPresent)",
    "-e", "TZ=Asia/Shanghai",
    "-v", "/run/desktop/mnt/host/wslg/.X11-unix:/tmp/.X11-unix", "-v", "/run/desktop/mnt/host/wslg:/mnt/wslg",
    "pico-nano-sdl:dev", "stdbuf", "-oL", "build-sim-sdl-run/pn_sim",
    "--library", $Books, "--budget", "$($BudgetKiB * 1024)", "--state-dir", "$stateBase/state", "--cover-cache", $coverCacheDirectory,
    "--wallpaper-dir", $WallpaperDir, "--wallpaper-store", "$stateBase/wallpaper", "--font-dir", $FontDir)
if ($Font) { $arguments += @("--font", $Font) }
# 仅切换派生索引后端，不清除缓存、位置或书籍原件。/ Switch only the derived-index backend, preserving caches, positions and original books.
if (-not $NoReadingCache) { $arguments = @("run", "-e", "PN_READING_CACHE=$stateBase/reading") + $arguments[1..($arguments.Length - 1)] }
& docker @arguments
if ($LASTEXITCODE -ne 0) { Write-Error "模拟器启动失败"; exit 1 }
Start-Sleep -Seconds 2
$running = docker ps --filter "name=^/pico-sim$" --format '{{.ID}}'
if (-not $running) { Write-Error "模拟器已退出，请检查WSLg显示通道及输入文件"; exit 1 }
Write-Host "已启动模拟器窗口；关闭窗口或运行 docker stop pico-sim 结束。"
