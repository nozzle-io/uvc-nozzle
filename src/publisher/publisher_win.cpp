#include <publisher/publisher.hpp>

#include <capture/capture_device.hpp>

#define WIN32_LEAN_AND_MEAN
#include <d3d11.h>
#include <d3d10_1.h>

#include <nozzle/nozzle_c.h>

#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

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
	ID3D11Device *d3d_device{nullptr};
	ID3D11DeviceContext *d3d_context{nullptr};

	void release_resources() {
		if (sender) {
			nozzle_sender_destroy(sender);
			sender = nullptr;
		}
		if (d3d_context) {
			d3d_context->Release();
			d3d_context = nullptr;
		}
		if (d3d_device) {
			d3d_device->Release();
			d3d_device = nullptr;
		}
	}
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
	UINT create_flags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;

	D3D_FEATURE_LEVEL feature_level{};
	HRESULT hr = D3D11CreateDevice(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		create_flags,
		nullptr,
		0,
		D3D11_SDK_VERSION,
		&impl_->d3d_device,
		&feature_level,
		&impl_->d3d_context
	);

	if (FAILED(hr)) return false;

	ID3D10Multithread *multithread = nullptr;
	hr = impl_->d3d_device->QueryInterface(__uuidof(ID3D10Multithread),
	                                       reinterpret_cast<void **>(&multithread));
	if (SUCCEEDED(hr) && multithread) {
		multithread->SetMultithreadProtected(TRUE);
		multithread->Release();
	}

	NozzleSenderDesc desc{};
	desc.name = name.c_str();
	desc.application_name = "uvc-nozzle";
	desc.ring_buffer_size = ring_buffer_size;

	NozzleNativeDevice native_dev{};
	native_dev.backend = NOZZLE_BACKEND_D3D11;
	native_dev.device = impl_->d3d_device;
	native_dev.context = impl_->d3d_context;

	NozzleErrorCode err = nozzle_sender_create_with_native_device(
		&desc, &native_dev, &impl_->sender);
	if (err != NOZZLE_OK) {
		impl_->sender = nullptr;
		impl_->release_resources();
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
		case frame_payload_kind::d3d11_texture2d:
			return publish_d3d11_texture(frame.native_handle, frame.width, frame.height);
		case frame_payload_kind::cv_pixel_buffer:
		case frame_payload_kind::dma_buf:
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
	if (!impl_->sender || !pixel_data) return false;
	if (w == 0 || h == 0) return false;
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
		// Commit rejects failed-unlock frames and releases the sender slot.
		(void)nozzle_sender_commit_frame(impl_->sender, frame);
		return false;
	}
	err = nozzle_sender_commit_frame(impl_->sender, frame);
	return err == NOZZLE_OK;
}

bool publisher::publish_cv_pixel_buffer(void *, uint32_t, uint32_t) {
	return false;
}

bool publisher::publish_d3d11_texture(void *native_texture, uint32_t w, uint32_t h) {
	if (!impl_->sender || !native_texture) return false;

	auto *d3d_texture = static_cast<ID3D11Texture2D *>(native_texture);
	NozzleErrorCode err = nozzle_sender_publish_native_texture(
		impl_->sender, d3d_texture, w, h, NOZZLE_FORMAT_BGRA8_UNORM);
	return err == NOZZLE_OK;
}

bool publisher::publish_dma_buf_frame(const captured_frame &) {
	return false;
}

void *publisher::get_native_device() const {
	return impl_ ? impl_->d3d_device : nullptr;
}

void publisher::destroy() {
	if (!impl_) return;
	impl_->release_resources();
}

} // namespace uvc
