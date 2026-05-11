#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct GLFWwindow;

namespace uvc {

class render_backend {
public:
    virtual ~render_backend() = default;

    virtual bool init(GLFWwindow *window) = 0;
    virtual void shutdown() = 0;
    virtual void begin_frame() = 0;
    virtual void end_frame() = 0;

    virtual void *create_preview_texture(uint32_t w, uint32_t h) = 0;
    virtual void destroy_preview_texture(void *tex) = 0;
    virtual void update_preview_from_native(void *tex, void *native_buffer, uint32_t w, uint32_t h) = 0;

    virtual bool check_camera_authorization() = 0;
    virtual const char *get_system_font_path() = 0;
};

std::unique_ptr<render_backend> create_render_backend();

} // namespace uvc
