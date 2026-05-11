#include <publisher/publisher.hpp>

#include <nozzle/nozzle.hpp>
#include <nozzle/backends/metal.hpp>

#include <CoreVideo/CoreVideo.h>
#include <IOSurface/IOSurface.h>
#include <Metal/Metal.h>

namespace uvc {

struct publisher::impl {
    std::unique_ptr<nozzle::sender> sender;
    id<MTLDevice> mtl_device{nil};
};

publisher::publisher() : impl_(std::make_unique<impl>()) {}

publisher::~publisher() {
    destroy();
}

publisher::publisher(publisher &&other) noexcept : impl_(std::move(other.impl_)) {}

publisher &publisher::operator=(publisher &&other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool publisher::create(const std::string &name, uint32_t ring_buffer_size) {
    @autoreleasepool {
        impl_->mtl_device = MTLCreateSystemDefaultDevice();
        if (!impl_->mtl_device) {
            return false;
        }

        auto result = nozzle::sender::create({
            .name = name,
            .application_name = "uvc-nozzle",
            .ring_buffer_size = ring_buffer_size,
        });

        if (!result.ok()) {
            return false;
        }

        impl_->sender = std::make_unique<nozzle::sender>(std::move(result.value()));
        return true;
    }
}

bool publisher::publish_frame(void *pixel_buffer, uint32_t w, uint32_t h) {
    if (!impl_->sender || !pixel_buffer) {
        return false;
    }

    @autoreleasepool {
        CVPixelBufferRef cv_buffer = static_cast<CVPixelBufferRef>(pixel_buffer);
        IOSurfaceRef io_surface = CVPixelBufferGetIOSurface(cv_buffer);

        if (!io_surface) {
            return false;
        }

        MTLTextureDescriptor *tex_desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                            width:w
                                                                                           height:h
                                                                                        mipmapped:NO];
        tex_desc.usage = MTLTextureUsageShaderRead;
        tex_desc.storageMode = MTLStorageModeShared;

        id<MTLTexture> mtl_texture = [impl_->mtl_device newTextureWithDescriptor:tex_desc
                                                                        iosurface:io_surface
                                                                            plane:0];

        if (!mtl_texture) {
            return false;
        }

        auto publish_result = impl_->sender->publish_native_texture(
            (__bridge void *)mtl_texture, w, h, nozzle::texture_format::bgra8_unorm);

        return publish_result.ok();
    }
}

void publisher::destroy() {
    if (!impl_) return;
    impl_->sender.reset();
    if (impl_->mtl_device) {
        [impl_->mtl_device release];
        impl_->mtl_device = nil;
    }
}

} // namespace uvc
