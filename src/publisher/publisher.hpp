#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace uvc {

class publisher {
public:
    publisher();
    ~publisher();

    publisher(const publisher &) = delete;
    publisher &operator=(const publisher &) = delete;
    publisher(publisher &&) noexcept;
    publisher &operator=(publisher &&) noexcept;

    bool create(const std::string &name, uint32_t ring_buffer_size = 3);
    bool publish_frame(const void *pixel_data, uint32_t w, uint32_t h);
    bool publish_native_frame(void *native_texture, uint32_t w, uint32_t h);
    void destroy();

    void *get_native_device() const;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace uvc
