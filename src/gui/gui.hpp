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

class render_backend;

struct capture_session {
    std::unique_ptr<capture_device> device;
    std::unique_ptr<publisher> pub;
    std::string sender_name;
    std::string device_unique_id;
    void *preview_texture{nullptr};
    std::unique_ptr<std::mutex> preview_mutex;
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
    void stop_session(capture_session &session);

    GLFWwindow *window_{nullptr};
    std::unique_ptr<render_backend> backend_;
    std::vector<device_info> devices_;
    std::vector<capture_session> sessions_;
    std::mutex sessions_mutex_;
    std::atomic<bool> running_{false};
    bool camera_authorized_{false};
};

} // namespace uvc
