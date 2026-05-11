#include <capture/capture_device.hpp>
#include <gui/gui.hpp>
#include <publisher/publisher.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running = false;
}

static void print_usage(const char *prog) {
    std::printf("Usage: %s [options]\n", prog);
    std::printf("Options:\n");
    std::printf("  -l, --list              List UVC devices\n");
    std::printf("  -d, --device <index>    Select device by index\n");
    std::printf("  -n, --name <name>       Sender name (default: device name)\n");
    std::printf("  --width <w>             Frame width\n");
    std::printf("  --height <h>            Frame height\n");
    std::printf("  --fps <f>               Frame rate\n");
    std::printf("  --gui                   Force GUI mode\n");
    std::printf("  -h, --help              Show this help\n");
}

static int list_devices() {
    auto devices = uvc::capture_device::enumerate();
    if (devices.empty()) {
        std::printf("No UVC devices found.\n");
        return 0;
    }

    std::printf("UVC Devices:\n");
    for (size_t i = 0; i < devices.size(); i++) {
        std::printf("  [%zu] %s (%s)\n", i, devices[i].name.c_str(), devices[i].unique_id.c_str());
    }

    return 0;
}

static int run_cli(int device_index, const std::string &sender_name,
                   uint32_t width, uint32_t height, uint32_t fps) {
    auto devices = uvc::capture_device::enumerate();
    if (devices.empty()) {
        std::fprintf(stderr, "No UVC devices found.\n");
        return 1;
    }

    if (device_index < 0 || device_index >= static_cast<int>(devices.size())) {
        std::fprintf(stderr, "Invalid device index %d. Available: 0-%zu\n",
                     device_index, devices.size() - 1);
        return 1;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    auto capture = uvc::create_capture_device();
    if (!capture->open(devices[device_index])) {
        std::fprintf(stderr, "Failed to open device: %s\n", devices[device_index].name.c_str());
        return 1;
    }

    std::string name = sender_name.empty() ? devices[device_index].name : sender_name;

    auto pub = std::make_unique<uvc::publisher>();
    if (!pub->create(name)) {
        std::fprintf(stderr, "Failed to create nozzle sender: %s\n", name.c_str());
        return 1;
    }

    uvc::format_info fmt;
    if (width > 0 && height > 0 && fps > 0) {
        fmt = {width, height, fps, "BGRA"};
    } else {
        fmt = capture->default_format();
    }

    std::printf("Starting capture: %s %ux%u @ %ufps -> nozzle://%s\n",
                devices[device_index].name.c_str(), fmt.width, fmt.height, fmt.fps, name.c_str());

    if (!capture->configure(fmt)) {
        std::fprintf(stderr, "Failed to configure format: %ux%u @ %ufps\n", fmt.width, fmt.height, fmt.fps);
        return 1;
    }

    if (!capture->start([pub_ptr = pub.get()](void *buffer, uint32_t w, uint32_t h) {
        pub_ptr->publish_frame(buffer, w, h);
    })) {
        std::fprintf(stderr, "Failed to start capture.\n");
        return 1;
    }

    std::printf("Capturing... Press Ctrl+C to stop.\n");

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::printf("\nStopping...\n");
    capture->stop();
    pub->destroy();

    return 0;
}

int main(int argc, char *argv[]) {
    bool list_mode = false;
    bool force_gui = false;
    int device_index = -1;
    std::string sender_name;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps = 0;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-l" || arg == "--list") {
            list_mode = true;
        } else if (arg == "-d" || arg == "--device") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Missing argument for %s\n", arg.c_str());
                return 1;
            }
            device_index = std::atoi(argv[++i]);
        } else if (arg == "-n" || arg == "--name") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Missing argument for %s\n", arg.c_str());
                return 1;
            }
            sender_name = argv[++i];
        } else if (arg == "--width") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Missing argument for --width\n");
                return 1;
            }
            width = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--height") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Missing argument for --height\n");
                return 1;
            }
            height = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--fps") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Missing argument for --fps\n");
                return 1;
            }
            fps = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--gui") {
            force_gui = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            print_usage(argv[0]);
            return 1;
        }
    }

    if (list_mode) {
        return list_devices();
    }

    if (force_gui) {
        uvc::gui app;
        if (!app.init()) {
            std::fprintf(stderr, "Failed to initialize GUI.\n");
            return 1;
        }
        app.run();
        app.shutdown();
        return 0;
    }

    if (device_index >= 0) {
        return run_cli(device_index, sender_name, width, height, fps);
    }

    // No explicit mode - detect launch context.
    // LSEnvironment in Info.plist sets UVC_NOZZLE_BUNDLE_LAUNCH when
    // launched via LaunchServices (Finder double-click, `open` command).
    // Terminal launches have TERM set.
    bool launched_from_ls = std::getenv("UVC_NOZZLE_BUNDLE_LAUNCH") != nullptr;
    bool in_terminal = std::getenv("TERM") != nullptr;

    if (launched_from_ls || !in_terminal) {
        uvc::gui app;
        if (!app.init()) {
            std::fprintf(stderr, "Failed to initialize GUI.\n");
            return 1;
        }
        app.run();
        app.shutdown();
        return 0;
    }

    print_usage(argv[0]);
    return 0;
}
