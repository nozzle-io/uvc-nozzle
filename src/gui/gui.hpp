#pragma once

#include <capture/capture_device.hpp>
#include <publisher/publisher.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct GLFWwindow;

namespace uvc {

struct capture_session {
    std::unique_ptr<capture_device> device;
    std::unique_ptr<publisher> pub;
    std::string sender_name;
    bool running{false};
};

class gui {
public:
    gui();
    ~gui();

    gui(const gui &) = delete;
    gui &operator=(const gui &) = delete;

    bool init();
    void run();
    void shutdown();

private:
    void build_ui();

    GLFWwindow *window_{nullptr};
    std::vector<device_info> devices_;
    std::vector<capture_session> sessions_;
    std::mutex sessions_mutex_;
    std::atomic<bool> running_{false};
};

} // namespace uvc
