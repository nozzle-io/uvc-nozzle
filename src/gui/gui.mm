#include <gui/gui.hpp>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_metal.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#include <cstdio>
#include <cstring>

namespace uvc {

gui::gui() = default;

gui::~gui() {
    shutdown();
}

bool gui::init() {
    if (!glfwInit()) {
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window_ = glfwCreateWindow(640, 480, "uvc-nozzle", nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        return false;
    }

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        glfwDestroyWindow(window_);
        glfwTerminate();
        return false;
    }
    metal_device_ = (void *)CFBridgingRetain(device);
    command_queue_ = (void *)CFBridgingRetain([device newCommandQueue]);

    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;

    NSWindow *ns_window = glfwGetCocoaWindow(window_);
    ns_window.contentView.layer = layer;
    ns_window.contentView.wantsLayer = YES;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOther(window_, true);
    ImGui_ImplMetal_Init(device);

    devices_ = capture_device::enumerate();

    return true;
}

void gui::run() {
    running_ = true;

    id<MTLDevice> device = (__bridge id<MTLDevice>)metal_device_;
    id<MTLCommandQueue> command_queue = (__bridge id<MTLCommandQueue>)command_queue_;

    while (running_ && !glfwWindowShouldClose(window_)) {
        @autoreleasepool {
            glfwPollEvents();

            int width, height;
            glfwGetFramebufferSize(window_, &width, &height);

            NSWindow *ns_window = glfwGetCocoaWindow(window_);
            id<CAMetalDrawable> drawable = [(CAMetalLayer *)ns_window.contentView.layer nextDrawable];
            if (!drawable) {
                continue;
            }

            MTLRenderPassDescriptor *render_pass = [MTLRenderPassDescriptor renderPassDescriptor];
            render_pass.colorAttachments[0].texture = drawable.texture;
            render_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            render_pass.colorAttachments[0].clearColor = MTLClearColorMake(0.1, 0.1, 0.1, 1.0);
            render_pass.colorAttachments[0].storeAction = MTLStoreActionStore;

            ImGui_ImplGlfw_NewFrame();
            ImGui_ImplMetal_NewFrame(render_pass);
            ImGui::NewFrame();

            build_ui();

            ImGui::Render();

            id<MTLCommandBuffer> command_buffer = [command_queue commandBuffer];
            id<MTLRenderCommandEncoder> render_encoder = [command_buffer renderCommandEncoderWithDescriptor:render_pass];

            ImDrawData *draw_data = ImGui::GetDrawData();
            ImGui_ImplMetal_RenderDrawData(draw_data, command_buffer, render_encoder);

            [render_encoder endEncoding];

            [command_buffer presentDrawable:drawable];
            [command_buffer commit];
        }
    }
}

void gui::shutdown() {
    running_ = false;

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto &session : sessions_) {
        if (session.running) {
            session.device->stop();
            session.pub->destroy();
            session.running = false;
        }
    }
    sessions_.clear();

    if (window_) {
        ImGui_ImplMetal_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(window_);
        glfwTerminate();
        window_ = nullptr;
    }

    if (command_queue_) {
        CFRelease(command_queue_);
        command_queue_ = nullptr;
    }
    if (metal_device_) {
        CFRelease(metal_device_);
        metal_device_ = nullptr;
    }
}

void gui::build_ui() {
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("uvc-nozzle", nullptr, ImGuiWindowFlags_MenuBar);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Devices")) {
            if (ImGui::MenuItem("Refresh")) {
                devices_ = capture_device::enumerate();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    if (devices_.empty()) {
        ImGui::Text("No UVC devices found.");
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
        std::strncpy(sender_name_buf, devices_[selected_device_idx].name.c_str(), sizeof(sender_name_buf) - 1);
    }
    ImGui::InputText("Sender Name", sender_name_buf, sizeof(sender_name_buf));

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        bool any_running = false;
        for (const auto &s : sessions_) {
            if (s.running) {
                any_running = true;
                ImGui::Text("Capturing -> nozzle://%s", s.sender_name.c_str());
            }
        }

        if (any_running) {
            if (ImGui::Button("Stop All", ImVec2(120, 0))) {
                for (auto &s : sessions_) {
                    if (s.running) {
                        s.device->stop();
                        s.pub->destroy();
                        s.running = false;
                    }
                }
            }
        } else {
            if (ImGui::Button("Start", ImVec2(120, 0))) {
                auto dev = create_capture_device();
                if (dev->open(devices_[selected_device_idx])) {
                    auto fmt = dev->default_format();
                    dev->configure(fmt);

                    auto pub = std::make_unique<publisher>();
                    std::string name(sender_name_buf[0] ? sender_name_buf : devices_[selected_device_idx].name);
                    if (pub->create(name)) {
                        dev->start([pub_ptr = pub.get()](void *buffer, uint32_t w, uint32_t h) {
                            pub_ptr->publish_frame(buffer, w, h);
                        });

                        capture_session session;
                        session.device = std::move(dev);
                        session.pub = std::move(pub);
                        session.sender_name = name;
                        session.running = true;
                        sessions_.push_back(std::move(session));
                    }
                }
            }
        }
    }

    ImGui::End();
}

} // namespace uvc
