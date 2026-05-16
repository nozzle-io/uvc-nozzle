#include <capture/capture_device.hpp>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfobjects.h>
#include <codecapi.h>
#include <propsys.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "ole32.lib")

namespace uvc {

template <typename T>
void safe_release(T *&ptr) {
	if (ptr) {
		ptr->Release();
		ptr = nullptr;
	}
}

struct capture_device::impl {
	device_info selected_device{};
	format_info selected_format{0, 0, 0, "BGRA"};
	std::vector<format_info> formats;

	IMFMediaSource *source{nullptr};
	IMFSourceReader *reader{nullptr};

	std::atomic<bool> capturing{false};
	std::thread capture_thread;
	std::vector<uint8_t> frame_buffer;

	uint32_t configured_width{0};
	uint32_t configured_height{0};

	~impl() {
		stop_capture();
		safe_release(reader);
		safe_release(source);
		MFShutdown();
	}

	void stop_capture() {
		capturing.store(false);
		if (capture_thread.joinable()) {
			capture_thread.join();
		}
	}
};

capture_device::capture_device() : impl_(std::make_unique<impl>()) {}

capture_device::~capture_device() {
	stop();
}

capture_device::capture_device(capture_device &&other) noexcept
	: impl_(std::move(other.impl_)) {}

capture_device &capture_device::operator=(capture_device &&other) noexcept {
	if (this != &other) {
		impl_ = std::move(other.impl_);
	}
	return *this;
}

static std::string wide_to_utf8(const wchar_t *wide) {
	if (!wide) return {};
	int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0) return {};
	std::string result(len - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), len, nullptr, nullptr);
	return result;
}

std::vector<device_info> capture_device::enumerate() {
	std::vector<device_info> devices;

	HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
	if (FAILED(hr)) {
		return devices;
	}

	IMFAttributes *attrs = nullptr;
	hr = MFCreateAttributes(&attrs, 2);
	if (FAILED(hr)) {
		MFShutdown();
		return devices;
	}

	attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

	UINT32 count = 0;
	IMFActivate **activates = nullptr;
	hr = MFEnumDeviceSources(attrs, &activates, &count);
	attrs->Release();

	if (FAILED(hr) || count == 0) {
		if (activates) {
			CoTaskMemFree(activates);
		}
		MFShutdown();
		return devices;
	}

	for (UINT32 i = 0; i < count; ++i) {
		WCHAR *name_buf = nullptr;
		UINT32 name_len = 0;
		HRESULT name_hr = activates[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name_buf, &name_len);

		WCHAR *sym_buf = nullptr;
		UINT32 sym_len = 0;
		HRESULT sym_hr = activates[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &sym_buf, &sym_len);

		device_info info;
		if (SUCCEEDED(name_hr) && name_buf) {
			info.name = wide_to_utf8(name_buf);
			CoTaskMemFree(name_buf);
		}
		if (SUCCEEDED(sym_hr) && sym_buf) {
			info.unique_id = wide_to_utf8(sym_buf);
			CoTaskMemFree(sym_buf);
		}

		activates[i]->Release();
		devices.push_back(std::move(info));
	}

	CoTaskMemFree(activates);
	MFShutdown();
	return devices;
}

bool capture_device::open(const device_info &dev) {
	if (impl_->source) {
		return false;
	}

	HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
	if (FAILED(hr)) {
		return false;
	}

	IMFAttributes *attrs = nullptr;
	hr = MFCreateAttributes(&attrs, 2);
	if (FAILED(hr)) {
		MFShutdown();
		return false;
	}

	attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

	UINT32 count = 0;
	IMFActivate **activates = nullptr;
	hr = MFEnumDeviceSources(attrs, &activates, &count);
	attrs->Release();

	if (FAILED(hr)) {
		MFShutdown();
		return false;
	}

	IMFMediaSource *found_source = nullptr;
	for (UINT32 i = 0; i < count; ++i) {
		WCHAR *sym_buf = nullptr;
		UINT32 sym_len = 0;
		HRESULT sym_hr = activates[i]->GetAllocatedString(
			MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &sym_buf, &sym_len);

		bool match = false;
		if (SUCCEEDED(sym_hr) && sym_buf) {
			match = (dev.unique_id == wide_to_utf8(sym_buf));
			CoTaskMemFree(sym_buf);
		}

		if (match) {
			hr = activates[i]->ActivateObject(IID_PPV_ARGS(&found_source));
			if (FAILED(hr)) {
				found_source = nullptr;
			}
		}
		activates[i]->Release();

		if (found_source) {
			break;
		}
	}
	CoTaskMemFree(activates);

	if (!found_source) {
		MFShutdown();
		return false;
	}

	IMFSourceReader *reader = nullptr;
	IMFAttributes *reader_attrs = nullptr;
	hr = MFCreateAttributes(&reader_attrs, 1);
	if (FAILED(hr)) {
		found_source->Release();
		MFShutdown();
		return false;
	}

	reader_attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

	hr = MFCreateSourceReaderFromMediaSource(found_source, reader_attrs, &reader);
	reader_attrs->Release();

	if (FAILED(hr)) {
		found_source->Release();
		MFShutdown();
		return false;
	}

	impl_->source = found_source;
	impl_->reader = reader;
	impl_->selected_device = dev;

	impl_->formats.clear();
	DWORD type_index = 0;
	while (true) {
		IMFMediaType *native_type = nullptr;
		hr = reader->GetNativeMediaType(
			(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, type_index, &native_type);
		if (FAILED(hr)) {
			break;
		}

		UINT32 w = 0, h = 0;
		hr = MFGetAttributeSize(native_type, MF_MT_FRAME_SIZE, &w, &h);

		UINT32 num = 0, den = 0;
		hr = MFGetAttributeRatio(native_type, MF_MT_FRAME_RATE, &num, &den);

		format_info fi{};
		fi.width = w;
		fi.height = h;
		fi.fps = (den > 0) ? (num / den) : 30;
		fi.pixel_format = "BGRA";

		native_type->Release();

		if (w > 0 && h > 0) {
			impl_->formats.push_back(fi);
		}
		++type_index;
	}

	impl_->selected_format = default_format();
	return true;
}

bool capture_device::configure(const format_info &fmt) {
	if (!impl_->reader) {
		return false;
	}

	IMFMediaType *output_type = nullptr;
	HRESULT hr = MFCreateMediaType(&output_type);
	if (FAILED(hr)) {
		return false;
	}

	output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	output_type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	MFSetAttributeSize(output_type, MF_MT_FRAME_SIZE, fmt.width, fmt.height);
	if (fmt.fps > 0) {
		MFSetAttributeRatio(output_type, MF_MT_FRAME_RATE, fmt.fps, 1);
	}

	hr = impl_->reader->SetCurrentMediaType(
		(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output_type);
	output_type->Release();

	if (FAILED(hr)) {
		return false;
	}

	impl_->selected_format = fmt;
	impl_->configured_width = fmt.width;
	impl_->configured_height = fmt.height;
	impl_->frame_buffer.resize(fmt.width * fmt.height * 4);
	return true;
}

bool capture_device::start(std::function<void(void *pixel_buffer, uint32_t w, uint32_t h)> callback) {
	if (!impl_->reader || !callback) {
		return false;
	}

	if (impl_->capturing.load()) {
		return true;
	}

	if (impl_->configured_width == 0 || impl_->configured_height == 0) {
		format_info fmt = default_format();
		if (fmt.width == 0 || fmt.height == 0) {
			return false;
		}
		if (!configure(fmt)) {
			return false;
		}
	}

	impl_->capturing.store(true);
	impl_->capture_thread = std::thread([this, cb = std::move(callback)]() {
		DWORD stream_index = 0;
		DWORD flags = 0;
		LONGLONG timestamp = 0;
		IMFSample *sample = nullptr;

		while (impl_->capturing.load()) {
			safe_release(sample);

			HRESULT hr = impl_->reader->ReadSample(
				(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
				0, &stream_index, &flags, &timestamp, &sample);

			if (FAILED(hr) || !sample) {
				if (!impl_->capturing.load()) {
					break;
				}
				continue;
			}

			IMFMediaBuffer *buffer = nullptr;
			hr = sample->ConvertToContiguousBuffer(&buffer);
			if (FAILED(hr) || !buffer) {
				continue;
			}

			BYTE *data = nullptr;
			DWORD max_len = 0;
			DWORD cur_len = 0;
			hr = buffer->Lock(&data, &max_len, &cur_len);
			if (FAILED(hr)) {
				buffer->Release();
				continue;
			}

			DWORD expected = impl_->configured_width * impl_->configured_height * 4;
			if (cur_len >= expected && impl_->frame_buffer.size() >= expected) {
				std::memcpy(impl_->frame_buffer.data(), data, expected);
				cb(impl_->frame_buffer.data(), impl_->configured_width, impl_->configured_height);
			}

			buffer->Unlock();
			buffer->Release();
		}

		safe_release(sample);
	});

	return true;
}

void capture_device::stop() {
	if (impl_) {
		impl_->stop_capture();
	}
}

std::vector<format_info> capture_device::available_formats() const {
	return impl_->formats;
}

format_info capture_device::default_format() const {
	if (impl_->formats.empty()) {
		return {0, 0, 0, "BGRA"};
	}

	format_info best = impl_->formats[0];
	for (const auto &f : impl_->formats) {
		uint64_t best_pixels = static_cast<uint64_t>(best.width) * best.height;
		uint64_t f_pixels = static_cast<uint64_t>(f.width) * f.height;
		if (f_pixels > best_pixels ||
			(f_pixels == best_pixels && f.fps > best.fps)) {
			best = f;
		}
	}
	return best;
}

std::unique_ptr<capture_device> create_capture_device() {
	return std::make_unique<capture_device>();
}

} // namespace uvc
