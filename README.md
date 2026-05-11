# uvc-nozzle

UVC camera capture to [nozzle](https://github.com/nozzle-io/nozzle) GPU texture sharing. Capture from USB video devices and publish as shared Metal textures that any nozzle receiver can access.

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

### Build for distribution

```bash
cmake -B build -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

## Architecture

```
AVFoundation UVC capture
    ↓ CVPixelBuffer (BGRA)
    ↓ IOSurface
    ↓ MTLTexture
nozzle::sender (Metal/IOSurface backend)
    ↓ shared GPU texture
any nozzle receiver
```

Cross-platform abstraction via `capture_device` interface. macOS implementation in `src/capture/platform/macos/`. Windows and Linux paths ready.

## Dependencies

- [nozzle](https://github.com/nozzle-io/nozzle) — GPU texture sharing (submodule)
- [GLFW](https://www.glfw.org/) — window system for GUI (submodule)
- [Dear ImGui](https://github.com/ocornut/imgui) — GUI toolkit (submodule)
- [Catch2](https://github.com/catchorg/Catch2) — test framework (FetchContent)

## License

MIT
