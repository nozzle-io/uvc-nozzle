#include <capture/capture_device.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <linux/videodev2.h>

#include <glob.h>

namespace uvc {

static constexpr int k_buffer_count = 4;
static constexpr int k_max_video_devices = 64;

struct mmap_buffer {
	void *start{nullptr};
	size_t length{0};
};

struct capture_device::impl {
	device_info selected_device;
	format_info selected_format{0, 0, 0, "BGRA"};
	std::vector<format_info> formats;
	int fd{-1};
	uint32_t negotiated_width{0};
	uint32_t negotiated_height{0};
	uint32_t negotiated_pixfmt{0};
	bool needs_yuyv_convert{false};

	std::vector<mmap_buffer> buffers;
	std::vector<uint8_t> convert_buf;

	std::thread capture_thread;
	std::atomic<bool> running{false};
	std::function<void(void *, uint32_t, uint32_t)> callback;

	~impl() { close_fd(); }

	void close_fd() {
		if (fd >= 0) {
			::close(fd);
			fd = -1;
		}
	}

	void unmap_buffers() {
		for (auto &b : buffers) {
			if (b.start != nullptr) {
				munmap(b.start, b.length);
			}
		}
		buffers.clear();
	}

	static int xioctl(int fd, unsigned long request, void *arg) {
		int r;
		do {
			r = ioctl(fd, request, arg);
		} while (r == -1 && errno == EINTR);
		return r;
	}

	static void yuyv_to_bgra(const uint8_t *yuyv, uint8_t *bgra, uint32_t w, uint32_t h) {
		uint32_t pixel_count = w * h;
		for (uint32_t i = 0; i < pixel_count; i += 2) {
			int y0 = yuyv[0];
			int u = yuyv[1];
			int y1 = yuyv[2];
			int v = yuyv[3];
			yuyv += 4;

			int c0 = y0 - 16;
			int c1 = y1 - 16;
			int d = u - 128;
			int e = v - 128;

			auto clip = [](int val) -> uint8_t {
				return static_cast<uint8_t>(val < 0 ? 0 : (val > 255 ? 255 : val));
			};

			bgra[0] = clip((298 * c0 + 516 * d + 128) >> 8);
			bgra[1] = clip((298 * c0 - 100 * d - 208 * e + 128) >> 8);
			bgra[2] = clip((298 * c0 + 409 * e + 128) >> 8);
			bgra[3] = 255;
			bgra += 4;

			bgra[0] = clip((298 * c1 + 516 * d + 128) >> 8);
			bgra[1] = clip((298 * c1 - 100 * d - 208 * e + 128) >> 8);
			bgra[2] = clip((298 * c1 + 409 * e + 128) >> 8);
			bgra[3] = 255;
			bgra += 4;
		}
	}
};

capture_device::capture_device() : impl_(std::make_unique<impl>()) {}

capture_device::~capture_device() { stop(); }

capture_device::capture_device(capture_device &&other) noexcept
	: impl_(std::move(other.impl_)) {}

capture_device &capture_device::operator=(capture_device &&other) noexcept {
	if (this != &other) {
		impl_ = std::move(other.impl_);
	}
	return *this;
}

std::vector<device_info> capture_device::enumerate() {
	std::vector<device_info> devices;

	for (int i = 0; i < k_max_video_devices; ++i) {
		std::string path = "/dev/video" + std::to_string(i);
		int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
		if (fd < 0) {
			continue;
		}

		struct v4l2_capability cap = {};
		if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0 &&
			(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
			device_info info;
			info.name = reinterpret_cast<const char *>(cap.card);
			info.unique_id = path;
			devices.push_back(std::move(info));
		}

		::close(fd);
	}

	return devices;
}

bool capture_device::open(const device_info &dev) {
	stop();
	impl_->unmap_buffers();
	impl_->close_fd();

	int fd = ::open(dev.unique_id.c_str(), O_RDWR);
	if (fd < 0) {
		return false;
	}

	struct v4l2_capability cap = {};
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) != 0 ||
		!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		::close(fd);
		return false;
	}

	impl_->fd = fd;
	impl_->selected_device = dev;
	impl_->formats.clear();
	impl_->needs_yuyv_convert = false;

	struct v4l2_fmtdesc fmtdesc = {};
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
		uint32_t pixfmt = fmtdesc.pixelformat;

		struct v4l2_frmsizeenum frmsize = {};
		frmsize.pixel_format = pixfmt;

		while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
			if (frmsize.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
				++frmsize.index;
				continue;
			}

			struct v4l2_frmivalenum frmival = {};
			frmival.pixel_format = pixfmt;
			frmival.width = frmsize.discrete.width;
			frmival.height = frmsize.discrete.height;

			while (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) == 0) {
				uint32_t fps = 0;
				if (frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
					if (frmival.discrete.numerator > 0) {
						fps = frmival.discrete.denominator / frmival.discrete.numerator;
					}
				}

				if (fps > 0) {
					format_info fi;
					fi.width = frmsize.discrete.width;
					fi.height = frmsize.discrete.height;
					fi.fps = fps;
					fi.pixel_format = "BGRA";
					impl_->formats.push_back(fi);
				}

				++frmival.index;
			}

			++frmsize.index;
		}

		++fmtdesc.index;
	}

	if (impl_->formats.empty()) {
		format_info fi;
		fi.width = 640;
		fi.height = 480;
		fi.fps = 30;
		fi.pixel_format = "BGRA";
		impl_->formats.push_back(fi);
	}

	impl_->selected_format = default_format();
	return true;
}

bool capture_device::configure(const format_info &fmt) {
	if (impl_->fd < 0) {
		return false;
	}

	int fd = impl_->fd;

	uint32_t preferred_fmts[] = {
		V4L2_PIX_FMT_BGR32,
		V4L2_PIX_FMT_YUYV,
	};

	uint32_t chosen_fmt = 0;
	for (auto pf : preferred_fmts) {
		struct v4l2_fmtdesc fmtdesc = {};
		fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
			if (fmtdesc.pixelformat == pf) {
				chosen_fmt = pf;
				break;
			}
			++fmtdesc.index;
		}
		if (chosen_fmt != 0) {
			break;
		}
	}

	if (chosen_fmt == 0) {
		struct v4l2_fmtdesc fmtdesc = {};
		fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
			chosen_fmt = fmtdesc.pixelformat;
		}
	}

	if (chosen_fmt == 0) {
		return false;
	}

	struct v4l2_format vfmt = {};
	vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vfmt.fmt.pix.width = fmt.width;
	vfmt.fmt.pix.height = fmt.height;
	vfmt.fmt.pix.pixelformat = chosen_fmt;
	vfmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (impl::xioctl(fd, VIDIOC_S_FMT, &vfmt) < 0) {
		return false;
	}

	impl_->negotiated_width = vfmt.fmt.pix.width;
	impl_->negotiated_height = vfmt.fmt.pix.height;
	impl_->negotiated_pixfmt = vfmt.fmt.pix.pixelformat;
	impl_->needs_yuyv_convert = (vfmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV);

	if (fmt.fps > 0) {
		struct v4l2_streamparm parm = {};
		parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		parm.parm.capture.timeperframe.numerator = 1;
		parm.parm.capture.timeperframe.denominator = fmt.fps;

		impl::xioctl(fd, VIDIOC_S_PARM, &parm);
	}

	impl_->selected_format = fmt;
	impl_->selected_format.width = impl_->negotiated_width;
	impl_->selected_format.height = impl_->negotiated_height;
	return true;
}

bool capture_device::start(std::function<void(void *, uint32_t, uint32_t)> callback) {
	if (impl_->fd < 0 || !callback) {
		return false;
	}

	stop();

	int fd = impl_->fd;

	struct v4l2_requestbuffers req = {};
	req.count = k_buffer_count;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (impl::xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
		return false;
	}

	if (req.count < 1) {
		return false;
	}

	impl_->buffers.resize(req.count);
	for (unsigned int i = 0; i < req.count; ++i) {
		struct v4l2_buffer buf = {};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (impl::xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
			impl_->unmap_buffers();
			return false;
		}

		impl_->buffers[i].length = buf.length;
		impl_->buffers[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, buf.m.offset);

		if (impl_->buffers[i].start == MAP_FAILED) {
			impl_->buffers[i].start = nullptr;
			impl_->unmap_buffers();
			return false;
		}
	}

	if (impl_->needs_yuyv_convert) {
		impl_->convert_buf.resize(
			static_cast<size_t>(impl_->negotiated_width) * impl_->negotiated_height * 4);
	}

	for (unsigned int i = 0; i < req.count; ++i) {
		struct v4l2_buffer buf = {};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (impl::xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
			impl_->unmap_buffers();
			return false;
		}
	}

	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (impl::xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
		impl_->unmap_buffers();
		return false;
	}

	impl_->callback = std::move(callback);
	impl_->running = true;

	impl_->capture_thread = std::thread([this]() {
		int fd = impl_->fd;
		uint32_t w = impl_->negotiated_width;
		uint32_t h = impl_->negotiated_height;

		while (impl_->running) {
			struct v4l2_buffer buf = {};
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;

			if (impl::xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
				if (errno == EINTR) {
					continue;
				}
				break;
			}

			if (buf.index < impl_->buffers.size()) {
				void *data = impl_->buffers[buf.index].start;
				uint32_t data_w = w;
				uint32_t data_h = h;

				if (impl_->needs_yuyv_convert) {
					impl::yuyv_to_bgra(
						static_cast<const uint8_t *>(data),
						impl_->convert_buf.data(),
						w, h);
					data = impl_->convert_buf.data();
				}

				impl_->callback(data, data_w, data_h);
			}

			if (impl_->running) {
				impl::xioctl(fd, VIDIOC_QBUF, &buf);
			}
		}
	});

	return true;
}

void capture_device::stop() {
	if (impl_->running) {
		impl_->running = false;

		if (impl_->fd >= 0) {
			enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			impl::xioctl(impl_->fd, VIDIOC_STREAMOFF, &type);
		}

		if (impl_->capture_thread.joinable()) {
			impl_->capture_thread.join();
		}

		impl_->unmap_buffers();
		impl_->callback = nullptr;
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
		if (f.width > best.width ||
			(f.width == best.width && f.height > best.height) ||
			(f.width == best.width && f.height == best.height && f.fps > best.fps)) {
			best = f;
		}
	}
	return best;
}

std::unique_ptr<capture_device> create_capture_device() {
	return std::make_unique<capture_device>();
}

} // namespace uvc
