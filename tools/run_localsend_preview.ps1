# 中文：启动独立的附近传书界面预览，不创建传输服务。/ English: launch the separate nearby-transfer UI preview without a transfer service.
$taskRoot = Split-Path -Parent $PSScriptRoot
Set-Location $taskRoot
if (-not (Test-Path "build-sim-sdl-run/pn_localsend_preview")) { Write-Error "请先运行 python tools/dev.py sdl-run"; exit 1 }
$running = docker ps --filter "name=^/pico-localsend-preview$" --format '{{.ID}}'
if ($LASTEXITCODE -ne 0) { Write-Error "Docker不可用"; exit 1 }
if ($running) { Write-Host "附近传书预览已运行，保留当前窗口。"; exit 0 }
$arguments = @("run", "--detach", "--rm", "--name", "pico-localsend-preview",
    "--mount", "type=bind,source=$taskRoot,target=/work", "-w", "/work",
    "-e", "DISPLAY=:0", "-e", "WAYLAND_DISPLAY=wayland-0", "-e", "XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir", "-e", "SDL_VIDEODRIVER=x11",
    "-v", "/run/desktop/mnt/host/wslg/.X11-unix:/tmp/.X11-unix", "-v", "/run/desktop/mnt/host/wslg:/mnt/wslg",
    "pico-nano-sdl:dev", "build-sim-sdl-run/pn_localsend_preview")
& docker @arguments
if ($LASTEXITCODE -ne 0) { Write-Error "预览启动失败"; exit 1 }
Write-Host "附近传书界面预览已启动：点击选择/分页/确认；空格切换示例状态，Esc关闭。此窗口不传输文件。"
