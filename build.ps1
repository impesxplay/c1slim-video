param([string]$Zig = "zig")
$ErrorActionPreference = "Stop"
$src = Join-Path $PSScriptRoot "src"
$app = Join-Path $PSScriptRoot "build/c1-video"
$cache = Join-Path $app "device-cache"
New-Item -ItemType Directory -Force $cache | Out-Null
$flags = @("cc", "-target", "mipsel-linux-musl", "-mcpu=mips32r2", "-static", "-O3")
& $Zig @flags -pthread "$src/app_unified.c" "$src/video_unified.c" "$src/media_device.c" -lm -o "$cache/c1-video-unified"
if ($LASTEXITCODE) { throw "Player build failed" }
& $Zig @flags "$src/pack_gray.c" -o "$cache/pack-gray"
if ($LASTEXITCODE) { throw "Cache packer build failed" }
Copy-Item "$PSScriptRoot/device/start.sh" $app
Copy-Item "$PSScriptRoot/device/unified.sh","$PSScriptRoot/device/prepare.sh","$PSScriptRoot/device/cjk12.bin" $cache
