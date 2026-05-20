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

enum class frame_payload_kind {
    cpu_bgra,
    cv_pixel_buffer,
    d3d11_texture2d,
    dma_buf
};

struct dma_buf_plane {
    uint32_t stride{0};
    uint32_t offset{0};
};

struct dma_buf_frame {
    int fd{-1};
    uint32_t fourcc{0};
    uint64_t modifier{0};
    uint32_t plane_count{0};
    dma_buf_plane planes[4]{};
};

struct captured_frame {
    frame_payload_kind payload_kind{frame_payload_kind::cpu_bgra};
    uint32_t width{0};
    uint32_t height{0};
    const void *cpu_bgra_pixels{nullptr};
    uint32_t cpu_row_stride_bytes{0};
    void *native_handle{nullptr};
    dma_buf_frame dma_buf{};

    static captured_frame cpu_bgra(
        const void *pixels,
        uint32_t w,
        uint32_t h,
        uint32_t row_stride_bytes = 0
    ) {
        captured_frame frame{};
        frame.payload_kind = frame_payload_kind::cpu_bgra;
        frame.width = w;
        frame.height = h;
        frame.cpu_bgra_pixels = pixels;
        frame.cpu_row_stride_bytes = row_stride_bytes == 0 ? w * 4 : row_stride_bytes;
        return frame;
    }

    static captured_frame cv_pixel_buffer(void *pixel_buffer, uint32_t w, uint32_t h) {
        captured_frame frame{};
        frame.payload_kind = frame_payload_kind::cv_pixel_buffer;
        frame.width = w;
        frame.height = h;
        frame.native_handle = pixel_buffer;
        return frame;
    }

    static captured_frame d3d11_texture2d(void *texture, uint32_t w, uint32_t h) {
        captured_frame frame{};
        frame.payload_kind = frame_payload_kind::d3d11_texture2d;
        frame.width = w;
        frame.height = h;
        frame.native_handle = texture;
        return frame;
    }

    static captured_frame dma_buf_native(const dma_buf_frame &dmabuf, uint32_t w, uint32_t h) {
        captured_frame frame{};
        frame.payload_kind = frame_payload_kind::dma_buf;
        frame.width = w;
        frame.height = h;
        frame.dma_buf = dmabuf;
        return frame;
    }

    bool is_cpu_bgra() const {
        return payload_kind == frame_payload_kind::cpu_bgra;
    }
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
    bool start(std::function<void(const captured_frame &frame)> callback);
    void stop();
    bool set_gpu_device(void *device);
    std::vector<format_info> available_formats() const;
    format_info default_format() const;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

std::unique_ptr<capture_device> create_capture_device();

} // namespace uvc
