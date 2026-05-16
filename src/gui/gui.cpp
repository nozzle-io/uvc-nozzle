#include <gui/gui.hpp>
#include <gui/render_backend.hpp>

#include <imgui.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace uvc {

static constexpr float k_preview_w = 320.0f;
static constexpr float k_preview_h = 180.0f;
static constexpr float k_window_default_w = 800.0f;
static constexpr float k_window_default_h = 600.0f;

gui::gui() = default;

gui::~gui() {
    shutdown();
}

bool gui::init() {
    if (!glfwInit()) {
        return false;
    }

    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);

    int win_w = std::min(static_cast<int>(k_window_default_w), mode->width * 2 / 3);
    int win_h = std::min(static_cast<int>(k_window_default_h), mode->height * 2 / 3);

#if !defined(__linux__)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#endif
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window_ = glfwCreateWindow(win_w, win_h, "uvc-nozzle", nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        return false;
    }

    int mon_x, mon_y;
    glfwGetMonitorPos(monitor, &mon_x, &mon_y);
    glfwSetWindowPos(window_,
        mon_x + (mode->width - win_w) / 2,
        mon_y + (mode->height - win_h) / 2);

    backend_ = create_render_backend();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImFontConfig font_cfg;
    font_cfg.OversampleH = 2;
    font_cfg.OversampleV = 1;
    const char *font_path = backend_->get_system_font_path();
    io.Fonts->AddFontFromFileTTF(font_path, 16.0f, &font_cfg,
        io.Fonts->GetGlyphRangesJapanese());

    ImGui::StyleColorsDark();

    if (!backend_->init(window_)) {
        ImGui::DestroyContext();
        glfwDestroyWindow(window_);
        glfwTerminate();
        window_ = nullptr;
        return false;
    }

    camera_authorized_ = backend_->check_camera_authorization();
    if (camera_authorized_) {
        devices_ = capture_device::enumerate();
    }

    return true;
}

void gui::run() {
    running_ = true;

    while (running_ && !glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        int fb_w, fb_h;
        glfwGetFramebufferSize(window_, &fb_w, &fb_h);
        if (fb_w <= 0 || fb_h <= 0) {
            continue;
        }

        backend_->begin_frame();
        build_ui();
        backend_->end_frame();
    }
}

void gui::stop_session(capture_session &session) {
    session.device->stop();
    session.pub->destroy();
    session.running = false;
    if (session.preview_texture) {
        backend_->destroy_preview_texture(session.preview_texture);
        session.preview_texture = nullptr;
    }
}

void gui::shutdown() {
    running_ = false;

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (auto &session : sessions_) {
            if (session.running) {
                stop_session(session);
            }
        }
        sessions_.clear();
    }

    if (window_) {
        backend_->shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(window_);
        glfwTerminate();
        window_ = nullptr;
    }
}

void gui::build_ui() {
    int fb_w, fb_h;
    glfwGetFramebufferSize(window_, &fb_w, &fb_h);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(fb_w), static_cast<float>(fb_h)));
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::Begin("uvc-nozzle", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (!camera_authorized_) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
            "Camera access denied.\nGrant permission in System Settings > Privacy & Security > Camera.");
        ImGui::End();
        return;
    }

    if (ImGui::Button("Refresh Devices")) {
        devices_ = capture_device::enumerate();
    }

    if (devices_.empty()) {
        ImGui::Spacing();
        ImGui::Text("No cameras found.");
        ImGui::End();
        return;
    }

    static int selected_device_idx = 0;
    {
        const char *current = selected_device_idx < static_cast<int>(devices_.size())
                                  ? devices_[selected_device_idx].name.c_str()
                                  : "";
        if (ImGui::BeginCombo("Device", current)) {
            for (int i = 0; i < static_cast<int>(devices_.size()); i++) {
                bool is_selected = (selected_device_idx == i);
                if (ImGui::Selectable(devices_[i].name.c_str(), is_selected)) {
                    selected_device_idx = i;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    static char sender_name_buf[256] = "";
    if (sender_name_buf[0] == '\0' && !devices_.empty()) {
        std::strncpy(sender_name_buf, devices_[selected_device_idx].name.c_str(),
            sizeof(sender_name_buf) - 1);
    }
    ImGui::InputText("Sender Name", sender_name_buf, sizeof(sender_name_buf));

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);

        sessions_.erase(
            std::remove_if(sessions_.begin(), sessions_.end(),
                [](const capture_session &s) { return !s.running; }),
            sessions_.end());

        bool already_running = false;
        if (selected_device_idx < static_cast<int>(devices_.size())) {
            const auto &sel_id = devices_[selected_device_idx].unique_id;
            for (const auto &s : sessions_) {
                if (s.running && s.device_unique_id == sel_id) {
                    already_running = true;
                    break;
                }
            }
        }

        if (!already_running && selected_device_idx < static_cast<int>(devices_.size())) {
            if (ImGui::Button("Add Device")) {
                auto dev = create_capture_device();
                if (dev->open(devices_[selected_device_idx])) {
                    auto fmt = dev->default_format();
                    dev->configure(fmt);

                    auto pub = std::make_unique<publisher>();
                    std::string name(sender_name_buf[0]
                        ? sender_name_buf : devices_[selected_device_idx].name);

                    if (pub->create(name)) {
                        void *preview_tex =
                            backend_->create_preview_texture(fmt.width, fmt.height);

                        capture_session session;
                        session.device = std::move(dev);
                        session.pub = std::move(pub);
                        session.sender_name = name;
                        session.device_unique_id = devices_[selected_device_idx].unique_id;
                        session.preview_texture = preview_tex;
                        session.preview_mutex = std::make_unique<std::mutex>();
                        session.running = true;

                        std::mutex *mutex_ptr = session.preview_mutex.get();
                        void *tex_ptr = session.preview_texture;
                        render_backend *backend_ptr = backend_.get();

                        session.device->start(
                            [pub_ptr = session.pub.get(), tex_ptr, mutex_ptr, backend_ptr](
                                void *buffer, uint32_t w, uint32_t h) {
                                pub_ptr->publish_frame(buffer, w, h);
                                std::lock_guard<std::mutex> lock(*mutex_ptr);
                                backend_ptr->update_preview_from_native(
                                    tex_ptr, buffer, w, h);
                            });

                        sessions_.push_back(std::move(session));
                    }
                }
            }
        } else if (already_running) {
            ImGui::TextDisabled("(device already active)");
        }

        if (!sessions_.empty()) {
            ImGui::Separator();
            ImGui::Text("Active Sessions (%d):", static_cast<int>(sessions_.size()));
            ImGui::Spacing();

            for (size_t i = 0; i < sessions_.size(); i++) {
                ImGui::PushID(static_cast<int>(i));

                if (sessions_[i].preview_texture) {
                    std::lock_guard<std::mutex> plock(*sessions_[i].preview_mutex);
                    ImGui::Image(
                        (ImTextureID)sessions_[i].preview_texture,
                        ImVec2(k_preview_w, k_preview_h));
                }

                ImGui::Text("%s -> nozzle://%s",
                    sessions_[i].sender_name.c_str(),
                    sessions_[i].sender_name.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Stop")) {
                    stop_session(sessions_[i]);
                }

                ImGui::Spacing();
                ImGui::PopID();
            }

            if (sessions_.size() >= 2) {
                ImGui::Separator();
                if (ImGui::Button("Stop All")) {
                    for (auto &s : sessions_) {
                        if (s.running) {
                            stop_session(s);
                        }
                    }
                }
            }
        }
    }

    ImGui::End();
}

} // namespace uvc
