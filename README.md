# uvc-nozzle

UVC camera capture to [nozzle](https://github.com/nozzle-io/nozzle) GPU texture sharing. Capture from USB video devices and publish as shared GPU textures that any nozzle receiver can access. Cross-platform: macOS (Metal/IOSurface), Windows (D3D11), Linux (DMA-BUF).

## Modes

### CLI

```bash
# List devices
uvc-nozzle -l

# Capture from device 0, publish as "cam0"
uvc-nozzle -d 0

# Custom sender name, resolution, fps
uvc-nozzle -d 0 -n front_cam --width 1920 --height 1080 --fps 30
```

Ctrl+C to stop.

### GUI

```bash
uvc-nozzle --gui
```

Device selector, sender name input, start/stop per session. Multiple devices simultaneously.

## Download

Grab `uvc-nozzle-macos-universal.zip` from [latest release](https://github.com/nozzle-io/uvc-nozzle/releases/latest). Universal binary (arm64 + x86_64).

On macOS you may need to remove quarantine:
```bash
xattr -cr uvc-nozzle.app
```

## Build

```bash
git clone --recurse-submodules https://github.com/nozzle-io/uvc-nozzle.git
cd uvc-nozzle
cmake -B build
cmake --build build
```

### Run tests

```bash
./build/uvc_tests --reporter compact
```

### macOS distribution build

```bash
cmake -B build -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

### Linux dependencies

```bash
sudo apt-get install libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxxf86vm-dev
```

## Architecture

### macOS

```
AVFoundation UVC capture
    ↓ CVPixelBuffer (BGRA)
    ↓ IOSurface
    ↓ MTLTexture
nozzle::sender (Metal/IOSurface backend)
    ↓ shared GPU texture
any nozzle receiver
```

### Windows

```
Media Foundation UVC capture
    ↓ IMFMediaBuffer (BGRA)
    ↓ CPU upload
nozzle::sender (D3D11 backend, acquire_writable_frame)
    ↓ shared GPU texture
any nozzle receiver
```

### Linux

```
V4L2 UVC capture (mmap)
    ↓ BGRA pixel data (YUYV→BGRA conversion if needed)
    ↓ CPU upload
nozzle::sender (DMA-BUF backend, acquire_writable_frame)
    ↓ shared GPU texture
any nozzle receiver
```

Cross-platform abstraction via `capture_device` and `render_backend` interfaces. Platform implementations in `src/capture/platform/` and `src/gui/platform/`.

## Dependencies

- [nozzle](https://github.com/nozzle-io/nozzle) — GPU texture sharing (submodule)
- [GLFW](https://www.glfw.org/) — window system for GUI (submodule)
- [Dear ImGui](https://github.com/ocornut/imgui) — GUI toolkit (submodule)
- [Catch2](https://github.com/catchorg/Catch2) — test framework (FetchContent)

## License

MIT
