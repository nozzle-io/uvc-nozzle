#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace uvc {

struct device_info {
    std::string name;
    std::string unique_id;
};

struct format_info {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    std::string pixel_format;
};

class capture_device {
public:
    capture_device();
    ~capture_device();

    capture_device(const capture_device &) = delete;
    capture_device &operator=(const capture_device &) = delete;
    capture_device(capture_device &&) noexcept;
    capture_device &operator=(capture_device &&) noexcept;

    static std::vector<device_info> enumerate();

    bool open(const device_info &dev);
    bool configure(const format_info &fmt);
    bool start(std::function<void(void *pixel_buffer, uint32_t w, uint32_t h)> callback);
    void stop();
    std::vector<format_info> available_formats() const;
    format_info default_format() const;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

std::unique_ptr<capture_device> create_capture_device();

} // namespace uvc
