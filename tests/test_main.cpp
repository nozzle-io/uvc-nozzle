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
