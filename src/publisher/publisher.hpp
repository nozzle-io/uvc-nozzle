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
    bool publish_frame(void *pixel_buffer, uint32_t w, uint32_t h);
    void destroy();

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace uvc
