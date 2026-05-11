#include <gui/render_backend.hpp>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AVFoundation/AVFoundation.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_metal.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#include <CoreVideo/CoreVideo.h>

namespace uvc {

class metal_render_backend : public render_backend {
public:
    bool init(GLFWwindow *window) override {
        window_ = window;

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            return false;
        }
        device_ = (void *)CFBridgingRetain(device);
        command_queue_ = (void *)CFBridgingRetain([device newCommandQueue]);

        NSWindow *ns_window = glfwGetCocoaWindow(window_);

        CAMetalLayer *layer = [CAMetalLayer layer];
        layer.device = device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.contentsScale = ns_window.screen.backingScaleFactor;

        int fb_w, fb_h;
        glfwGetFramebufferSize(window_, &fb_w, &fb_h);
        layer.drawableSize = CGSizeMake(fb_w, fb_h);

        ns_window.contentView.layer = layer;
        ns_window.contentView.wantsLayer = YES;

        ImGui_ImplGlfw_InitForOther(window_, true);
        ImGui_ImplMetal_Init(device);

        return true;
    }

    void shutdown() override {
        if (window_) {
            ImGui_ImplMetal_Shutdown();
            ImGui_ImplGlfw_Shutdown();
        }
        if (command_queue_) {
            CFRelease(command_queue_);
            command_queue_ = nullptr;
        }
        if (device_) {
            CFRelease(device_);
            device_ = nullptr;
        }
    }

    void begin_frame() override {
        int width, height;
        glfwGetFramebufferSize(window_, &width, &height);

        NSWindow *ns_window = glfwGetCocoaWindow(window_);
        CAMetalLayer *layer = (CAMetalLayer *)ns_window.contentView.layer;

        CGFloat scale = ns_window.screen.backingScaleFactor;
        if (layer.contentsScale != scale) {
            layer.contentsScale = scale;
        }

        CGSize fb_size = CGSizeMake(width, height);
        if (!CGSizeEqualToSize(layer.drawableSize, fb_size)) {
            layer.drawableSize = fb_size;
        }

        id<CAMetalDrawable> drawable = [layer nextDrawable];
        drawable_ = (void *)CFBridgingRetain(drawable);

        MTLRenderPassDescriptor *render_pass = [MTLRenderPassDescriptor renderPassDescriptor];
        render_pass.colorAttachments[0].texture = drawable.texture;
        render_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        render_pass.colorAttachments[0].clearColor = MTLClearColorMake(0.1, 0.1, 0.1, 1.0);
        render_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        render_pass_ = (void *)CFBridgingRetain(render_pass);

        ImGui_ImplGlfw_NewFrame();
        ImGui_ImplMetal_NewFrame(render_pass);
        ImGui::NewFrame();
    }

    void end_frame() override {
        ImGui::Render();

        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        id<MTLCommandQueue> command_queue = (__bridge id<MTLCommandQueue>)command_queue_;
        MTLRenderPassDescriptor *render_pass =
            (__bridge MTLRenderPassDescriptor *)render_pass_;
        id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)drawable_;

        id<MTLCommandBuffer> command_buffer = [command_queue commandBuffer];
        id<MTLRenderCommandEncoder> render_encoder =
            [command_buffer renderCommandEncoderWithDescriptor:render_pass];

        ImDrawData *draw_data = ImGui::GetDrawData();
        ImGui_ImplMetal_RenderDrawData(draw_data, command_buffer, render_encoder);

        [render_encoder endEncoding];
        [command_buffer presentDrawable:drawable];
        [command_buffer commit];

        CFRelease(render_pass_);
        render_pass_ = nullptr;
        CFRelease(drawable_);
        drawable_ = nullptr;
    }

    void *create_preview_texture(uint32_t w, uint32_t h) override {
        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        MTLTextureDescriptor *desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                width:w height:h mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModeShared;
        id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
        return (void *)CFBridgingRetain(texture);
    }

    void destroy_preview_texture(void *tex) override {
        if (tex) {
            CFRelease(tex);
        }
    }

    void update_preview_from_native(void *tex, void *native_buffer, uint32_t w, uint32_t h) override {
        CVPixelBufferRef pb = static_cast<CVPixelBufferRef>(native_buffer);
        CVPixelBufferLockBaseAddress(pb, 0);
        void *base = CVPixelBufferGetBaseAddress(pb);
        if (base) {
            size_t stride = CVPixelBufferGetBytesPerRow(pb);
            id<MTLTexture> texture = (__bridge id<MTLTexture>)tex;
            [texture replaceRegion:MTLRegionMake2D(0, 0, w, h)
                      mipmapLevel:0
                        withBytes:base
                      bytesPerRow:stride];
        }
        CVPixelBufferUnlockBaseAddress(pb, 0);
    }

    bool check_camera_authorization() override {
        @autoreleasepool {
            AVAuthorizationStatus auth =
                [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
            if (auth == AVAuthorizationStatusAuthorized) {
                return true;
            }
            if (auth == AVAuthorizationStatusNotDetermined) {
                dispatch_semaphore_t sem = dispatch_semaphore_create(0);
                __block bool granted = false;
                [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                    completionHandler:^(BOOL ok) {
                        granted = ok;
                        dispatch_semaphore_signal(sem);
                    }];
                dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
                dispatch_release(sem);
                return granted;
            }
            return false;
        }
    }

    const char *get_system_font_path() override {
        return "/System/Library/Fonts/ヒラギノ角ゴシック W4.ttc";
    }

private:
    GLFWwindow *window_{nullptr};
    void *device_{nullptr};
    void *command_queue_{nullptr};
    void *render_pass_{nullptr};
    void *drawable_{nullptr};
};

std::unique_ptr<render_backend> create_render_backend() {
    return std::make_unique<metal_render_backend>();
}

} // namespace uvc
