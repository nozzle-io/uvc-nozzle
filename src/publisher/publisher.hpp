#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace uvc {

struct captured_frame;

class publisher {
public:
    publisher();
    ~publisher();

    publisher(const publisher &) = delete;
    publisher &operator=(const publisher &) = delete;
    publisher(publisher &&) noexcept;
    publisher &operator=(publisher &&) noexcept;

    bool create(const std::string &name, uint32_t ring_buffer_size = 3);
    bool publish_frame(const captured_frame &frame);
    bool publish_cpu_bgra_frame(
        const void *pixel_data,
        uint32_t w,
        uint32_t h,
        uint32_t row_stride_bytes = 0
    );
    bool publish_cv_pixel_buffer(void *pixel_buffer, uint32_t w, uint32_t h);
    bool publish_d3d11_texture(void *texture, uint32_t w, uint32_t h);
    bool publish_dma_buf_frame(const captured_frame &frame);
    void destroy();

    void *get_native_device() const;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace uvc
