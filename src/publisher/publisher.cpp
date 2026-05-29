#include <publisher/publisher.hpp>

#include <capture/capture_device.hpp>
#include <nozzle/nozzle_c.h>

#include <cstring>

namespace uvc {

namespace {

struct nozzle_frame_guard {
	NozzleFrame *frame{nullptr};

	explicit nozzle_frame_guard(NozzleFrame *frame)
	: frame{frame}
	{}

	~nozzle_frame_guard() {
		if (frame) {
			nozzle_frame_release(frame);
		}
	}

	nozzle_frame_guard(const nozzle_frame_guard &) = delete;
	nozzle_frame_guard &operator=(const nozzle_frame_guard &) = delete;
};

} // anonymous namespace

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

bool publisher::publish_frame(const captured_frame &frame) {
	switch (frame.payload_kind) {
		case frame_payload_kind::cpu_bgra:
			return publish_cpu_bgra_frame(
				frame.cpu_bgra_pixels,
				frame.width,
				frame.height,
				frame.cpu_row_stride_bytes);
		case frame_payload_kind::cv_pixel_buffer:
			return publish_cv_pixel_buffer(frame.native_handle, frame.width, frame.height);
		case frame_payload_kind::d3d11_texture2d:
			return publish_d3d11_texture(frame.native_handle, frame.width, frame.height);
		case frame_payload_kind::dma_buf:
			return publish_dma_buf_frame(frame);
		default:
			return false;
	}
}

bool publisher::publish_cpu_bgra_frame(
	const void *pixel_data,
	uint32_t w,
	uint32_t h,
	uint32_t row_stride_bytes
) {
	if (!impl_->sender || !pixel_data) {
		return false;
	}
	if (w == 0 || h == 0) {
		return false;
	}
	const uint32_t src_row_bytes = row_stride_bytes == 0 ? w * 4 : row_stride_bytes;
	const uint32_t copy_row_bytes = w * 4;
	if (src_row_bytes < copy_row_bytes) {
		return false;
	}

	NozzleFrame *frame = nullptr;
	NozzleErrorCode err = nozzle_sender_acquire_writable_frame(
		impl_->sender, w, h, NOZZLE_FORMAT_BGRA8_UNORM, &frame);
	if (err != NOZZLE_OK) return false;
	nozzle_frame_guard frame_guard{frame};

	NozzleMappedPixels pixels;
	err = nozzle_frame_lock_writable_pixels_with_origin(
		frame, NOZZLE_ORIGIN_TOP_LEFT, &pixels);
	if (err != NOZZLE_OK) {
		(void)nozzle_sender_discard_frame(impl_->sender, frame);
		return false;
	}

	const auto *src = static_cast<const uint8_t *>(pixel_data);
	auto *dst = static_cast<uint8_t *>(pixels.data);

	if (pixels.row_stride_bytes == static_cast<int64_t>(src_row_bytes) &&
		src_row_bytes == copy_row_bytes) {
		std::memcpy(dst, src, static_cast<size_t>(copy_row_bytes) * h);
	} else {
		for (uint32_t y = 0; y < h; y++) {
			std::memcpy(dst + y * pixels.row_stride_bytes,
				src + y * src_row_bytes,
				copy_row_bytes);
		}
	}

	err = nozzle_frame_unlock_writable_pixels_checked(frame);
	if (err != NOZZLE_OK) {
		(void)nozzle_sender_discard_frame(impl_->sender, frame);
		return false;
	}
	err = nozzle_sender_commit_frame(impl_->sender, frame);
	return err == NOZZLE_OK;
}

bool publisher::publish_cv_pixel_buffer(void *, uint32_t, uint32_t) {
	return false;
}

bool publisher::publish_d3d11_texture(void *, uint32_t, uint32_t) {
	return false;
}

bool publisher::publish_dma_buf_frame(const captured_frame &) {
	return false;
}

void *publisher::get_native_device() const {
	return nullptr;
}

void publisher::destroy() {
	if (!impl_) return;
	if (impl_->sender) {
		nozzle_sender_destroy(impl_->sender);
		impl_->sender = nullptr;
	}
}

} // namespace uvc
