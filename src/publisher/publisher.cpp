#include <publisher/publisher.hpp>

#include <nozzle/nozzle_c.h>

#include <cstring>

namespace uvc {

struct publisher::impl {
	NozzleSender *sender{nullptr};
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

bool publisher::publish_frame(void *pixel_buffer, uint32_t w, uint32_t h) {
	if (!impl_->sender || !pixel_buffer) {
		return false;
	}

	NozzleFrame *frame = nullptr;
	NozzleErrorCode err = nozzle_sender_acquire_writable_frame(
		impl_->sender, w, h, NOZZLE_FORMAT_BGRA8_UNORM, &frame);
	if (err != NOZZLE_OK) return false;

	NozzleMappedPixels pixels;
	err = nozzle_frame_lock_writable_pixels_with_origin(
		frame, NOZZLE_ORIGIN_TOP_LEFT, &pixels);
	if (err != NOZZLE_OK) {
		nozzle_frame_release(frame);
		return false;
	}

	const uint32_t src_row_bytes = w * 4;
	const auto *src = static_cast<const uint8_t *>(pixel_buffer);
	auto *dst = static_cast<uint8_t *>(pixels.data);

	if (pixels.row_stride_bytes == static_cast<int64_t>(src_row_bytes)) {
		std::memcpy(dst, src, static_cast<size_t>(src_row_bytes) * h);
	} else {
		for (uint32_t y = 0; y < h; y++) {
			std::memcpy(dst + y * pixels.row_stride_bytes,
				src + y * src_row_bytes,
				src_row_bytes);
		}
	}

	nozzle_frame_unlock_writable_pixels(frame);
	err = nozzle_sender_commit_frame(impl_->sender, frame);
	return err == NOZZLE_OK;
}

void publisher::destroy() {
	if (!impl_) return;
	if (impl_->sender) {
		nozzle_sender_destroy(impl_->sender);
		impl_->sender = nullptr;
	}
}

} // namespace uvc
