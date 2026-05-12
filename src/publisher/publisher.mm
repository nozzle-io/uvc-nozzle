#include <publisher/publisher.hpp>

#include <nozzle/nozzle_c.h>

#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#include <CoreVideo/CoreVideo.h>

namespace uvc {

struct publisher::impl {
	NozzleSender *sender{nullptr};
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
		if (!impl_->mtl_device) return false;

		NozzleSenderDesc desc{};
		desc.name = name.c_str();
		desc.application_name = "uvc-nozzle";
		desc.ring_buffer_size = ring_buffer_size;

		NozzleErrorCode err = nozzle_sender_create(&desc, &impl_->sender);
		if (err != NOZZLE_OK) {
			impl_->sender = nullptr;
			return false;
		}

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
		if (!io_surface) return false;

		MTLTextureDescriptor *desc = [[MTLTextureDescriptor alloc] init];
		desc.textureType = MTLTextureType2D;
		desc.pixelFormat = MTLPixelFormatBGRA8Unorm;
		desc.width = w;
		desc.height = h;
		desc.mipmapLevelCount = 1;
		desc.arrayLength = 1;
		desc.sampleCount = 1;
		desc.usage = MTLTextureUsageShaderRead;
		desc.storageMode = MTLStorageModeShared;

		id<MTLTexture> texture = [impl_->mtl_device newTextureWithDescriptor:desc
		                                                             iosurface:io_surface
	                                                                    plane:0];
		[desc release];

		if (!texture) return false;

		NozzleErrorCode err = nozzle_sender_publish_native_texture(
			impl_->sender, (__bridge void *)texture, w, h, NOZZLE_FORMAT_BGRA8_UNORM);

		[texture release];
		return err == NOZZLE_OK;
	}
}

void publisher::destroy() {
	if (!impl_) return;
	if (impl_->sender) {
		nozzle_sender_destroy(impl_->sender);
		impl_->sender = nullptr;
	}
	@autoreleasepool {
		impl_->mtl_device = nil;
	}
}

} // namespace uvc
