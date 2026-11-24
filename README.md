# C1-Slim 灰阶视频播放器

## 构建

在 PowerShell 中使用 Zig 0.13.0 交叉编译：

```powershell
.\build.ps1 -Zig zig
```

## 安装

适用于 C1-Slim e0266a128 / X1600 固件，需要 root、原机 mpenMain、FFmpeg、ALSA 工具和 BusyBox。将 `build/c1-video/` 的内容复制到设备 `/storage/c1-video/`，然后运行：

```sh
cd /storage/c1-video
chmod +x start.sh device-cache/*.sh device-cache/c1-video-unified device-cache/pack-gray
./start.sh
```

## 视频预处理

将视频放入设备 `/storage/mtp/video/`。播放器首次打开时解码并缓存为 296×152、30 fps、32 灰阶。竖屏顺时针旋转，画面按比例缩放，留白填白。

也可在电脑上用 Python 3 和 FFmpeg 预先转换尺寸、方向和帧率，再将输出文件放入同一目录：

```sh
python tools/prepare_video.py input.mp4 output.mkv
```
