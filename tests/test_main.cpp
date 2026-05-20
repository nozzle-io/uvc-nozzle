#include <capture/capture_device.hpp>
#include <publisher/publisher.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("device_info default construction", "[capture]") {
    uvc::device_info info;
    REQUIRE(info.name.empty());
    REQUIRE(info.unique_id.empty());
}

TEST_CASE("format_info fields", "[capture]") {
    uvc::format_info fmt{1920, 1080, 60, "BGRA"};
    REQUIRE(fmt.width == 1920);
    REQUIRE(fmt.height == 1080);
    REQUIRE(fmt.fps == 60);
    REQUIRE(fmt.pixel_format == "BGRA");
}

TEST_CASE("captured_frame separates CPU and native payloads", "[capture][dispatch]") {
    uint8_t pixels[16]{};
    auto cpu = uvc::captured_frame::cpu_bgra(pixels, 2, 2);
    REQUIRE(cpu.payload_kind == uvc::frame_payload_kind::cpu_bgra);
    REQUIRE(cpu.cpu_bgra_pixels == pixels);
    REQUIRE(cpu.native_handle == nullptr);
    REQUIRE(cpu.cpu_row_stride_bytes == 8);
    REQUIRE(cpu.is_cpu_bgra());

    void *native = reinterpret_cast<void *>(0x1234);
    auto cv = uvc::captured_frame::cv_pixel_buffer(native, 4, 3);
    REQUIRE(cv.payload_kind == uvc::frame_payload_kind::cv_pixel_buffer);
    REQUIRE(cv.cpu_bgra_pixels == nullptr);
    REQUIRE(cv.native_handle == native);
    REQUIRE(!cv.is_cpu_bgra());

    auto d3d = uvc::captured_frame::d3d11_texture2d(native, 5, 6);
    REQUIRE(d3d.payload_kind == uvc::frame_payload_kind::d3d11_texture2d);
    REQUIRE(d3d.cpu_bgra_pixels == nullptr);
    REQUIRE(d3d.native_handle == native);
    REQUIRE(!d3d.is_cpu_bgra());
}

TEST_CASE("captured_frame carries explicit DMA-BUF metadata", "[capture][dispatch]") {
    uvc::dma_buf_frame dmabuf{};
    dmabuf.fd = 7;
    dmabuf.fourcc = 875713089;
    dmabuf.modifier = 0;
    dmabuf.plane_count = 1;
    dmabuf.planes[0].stride = 128;
    dmabuf.planes[0].offset = 0;

    auto frame = uvc::captured_frame::dma_buf_native(dmabuf, 16, 8);
    REQUIRE(frame.payload_kind == uvc::frame_payload_kind::dma_buf);
    REQUIRE(frame.cpu_bgra_pixels == nullptr);
    REQUIRE(frame.native_handle == nullptr);
    REQUIRE(frame.dma_buf.fd == 7);
    REQUIRE(frame.dma_buf.fourcc == 875713089);
    REQUIRE(frame.dma_buf.plane_count == 1);
    REQUIRE(frame.dma_buf.planes[0].stride == 128);
}

TEST_CASE("enumerate returns without crash", "[capture]") {
    auto devices = uvc::capture_device::enumerate();
    REQUIRE(devices.size() >= 0);
}

TEST_CASE("capture_device lifecycle", "[capture]") {
    auto dev = uvc::create_capture_device();
    REQUIRE(dev != nullptr);
}

TEST_CASE("capture_device move semantics", "[capture]") {
    auto dev1 = uvc::create_capture_device();
    auto dev2 = std::move(dev1);
    REQUIRE(dev2 != nullptr);
}

TEST_CASE("available_formats without open", "[capture]") {
    auto dev = uvc::create_capture_device();
    auto formats = dev->available_formats();
    REQUIRE(formats.empty());
}

TEST_CASE("default_format without open", "[capture]") {
    auto dev = uvc::create_capture_device();
    auto fmt = dev->default_format();
    REQUIRE(fmt.width == 0);
    REQUIRE(fmt.height == 0);
}

TEST_CASE("publisher create and destroy", "[publisher][metal]") {
    uvc::publisher pub;
    if (!pub.create("test_sender")) {
        SKIP("Metal device not available");
    }
    pub.destroy();
}

TEST_CASE("publisher create and move", "[publisher][metal]") {
    uvc::publisher pub1;
    if (!pub1.create("move_test")) {
        SKIP("Metal device not available");
    }
    uvc::publisher pub2 = std::move(pub1);
    pub2.destroy();
}

TEST_CASE("publisher create with custom ring buffer", "[publisher][metal]") {
    uvc::publisher pub;
    if (!pub.create("ring_test", 5)) {
        SKIP("Metal device not available");
    }
    pub.destroy();
}

TEST_CASE("publisher destroy without create", "[publisher]") {
    uvc::publisher pub;
    pub.destroy();
}
