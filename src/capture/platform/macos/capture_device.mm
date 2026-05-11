#import "capture_device.h"

#include <capture/capture_device.hpp>

#include <AVFoundation/AVFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>

namespace uvc {

struct capture_device::impl {
    device_info selected_device;
    format_info selected_format;
    std::vector<format_info> formats;
    UvcCaptureSession *session{nil};
};

capture_device::capture_device() : impl_(std::make_unique<impl>()) {}

capture_device::~capture_device() {
    stop();
    if (impl_->session) {
        [impl_->session release];
        impl_->session = nil;
    }
}

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

    @autoreleasepool {
        NSArray *device_types = @[
            AVCaptureDeviceTypeBuiltInWideAngleCamera,
            AVCaptureDeviceTypeExternalUnknown
        ];
        AVCaptureDeviceDiscoverySession *session = [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:device_types
            mediaType:AVMediaTypeVideo
            position:AVCaptureDevicePositionUnspecified];

        for (AVCaptureDevice *device in session.devices) {
            device_info info;
            info.name = [device.localizedName UTF8String];
            info.unique_id = [device.uniqueID UTF8String];
            devices.push_back(std::move(info));
        }
    }

    return devices;
}

bool capture_device::open(const device_info &dev) {
    @autoreleasepool {
        AVCaptureDevice *av_device = nil;
        NSArray *device_types = @[
            AVCaptureDeviceTypeBuiltInWideAngleCamera,
            AVCaptureDeviceTypeExternalUnknown
        ];
        AVCaptureDeviceDiscoverySession *session = [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:device_types
            mediaType:AVMediaTypeVideo
            position:AVCaptureDevicePositionUnspecified];

        for (AVCaptureDevice *device in session.devices) {
            if (std::string([device.uniqueID UTF8String]) == dev.unique_id) {
                av_device = device;
                break;
            }
        }

        if (!av_device) {
            return false;
        }

        impl_->selected_device = dev;

        impl_->formats.clear();
        for (AVCaptureDeviceFormat *fmt in av_device.formats) {
            CMVideoFormatDescriptionRef desc = fmt.formatDescription;
            CMVideoDimensions dims = CMVideoFormatDescriptionGetDimensions(desc);
            for (AVFrameRateRange *range in fmt.videoSupportedFrameRateRanges) {
                format_info fi;
                fi.width = dims.width;
                fi.height = dims.height;
                fi.fps = static_cast<uint32_t>(range.maxFrameRate);
                fi.pixel_format = "BGRA";
                impl_->formats.push_back(fi);
            }
        }

        impl_->session = [[UvcCaptureSession alloc] initWithDevice:av_device];
        if (!impl_->session) {
            return false;
        }

        impl_->selected_format = default_format();
        return true;
    }
}

bool capture_device::configure(const format_info &fmt) {
    if (!impl_->session) {
        return false;
    }

    impl_->selected_format = fmt;
    return [impl_->session configureWithWidth:fmt.width height:fmt.height fps:fmt.fps];
}

bool capture_device::start(std::function<void(void *pixel_buffer, uint32_t w, uint32_t h)> callback) {
    if (!impl_->session) {
        return false;
    }

    return [impl_->session startWithCallback:^(void *pb, uint32_t w, uint32_t h) {
        callback(pb, w, h);
    }];
}

void capture_device::stop() {
    if (impl_->session) {
        [impl_->session stop];
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
        if (f.width > best.width || (f.width == best.width && f.height > best.height) ||
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

@implementation UvcCaptureSession {
    AVCaptureDevice *_device;
    AVCaptureSession *_session;
    AVCaptureVideoDataOutput *_output;
    dispatch_queue_t _captureQueue;
    void (^_frameCallback)(void *, uint32_t, uint32_t);
}

- (nullable instancetype)initWithDevice:(AVCaptureDevice *)device {
    self = [super init];
    if (!self) {
        return nil;
    }

    _device = [device retain];
    _session = [[AVCaptureSession alloc] init];
    _output = [[AVCaptureVideoDataOutput alloc] init];
    _captureQueue = dispatch_queue_create("uvc_capture", DISPATCH_QUEUE_SERIAL);
    _frameCallback = nil;

    NSError *error = nil;
    AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:_device error:&error];
    if (!input) {
        return nil;
    }

    [_session addInput:input];

    _output.videoSettings = @{
        (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
    };
    _output.alwaysDiscardsLateVideoFrames = YES;

    [_session addOutput:_output];

    return self;
}

- (void)dealloc {
    [self stop];
    [_session removeOutput:_output];
    [_output release];
    [_session release];
    [_device release];
    dispatch_release(_captureQueue);
    [super dealloc];
}

- (BOOL)configureWithWidth:(uint32_t)width height:(uint32_t)height fps:(uint32_t)fps {
    for (AVCaptureDeviceFormat *fmt in _device.formats) {
        CMVideoFormatDescriptionRef desc = fmt.formatDescription;
        CMVideoDimensions dims = CMVideoFormatDescriptionGetDimensions(desc);

        if (dims.width != width || dims.height != height) {
            continue;
        }

        for (AVFrameRateRange *range in fmt.videoSupportedFrameRateRanges) {
            if (static_cast<uint32_t>(range.maxFrameRate) >= fps) {
                NSError *error = nil;
                [_device lockForConfiguration:&error];
                if (error) {
                    return NO;
                }
                _device.activeFormat = fmt;
                _device.activeVideoMinFrameDuration = range.minFrameDuration;
                _device.activeVideoMaxFrameDuration = range.minFrameDuration;
                [_device unlockForConfiguration];
                return YES;
            }
        }
    }

    return NO;
}

- (BOOL)startWithCallback:(void (^)(void *, uint32_t, uint32_t))callback {
    if (_frameCallback) {
        return YES;
    }

    _frameCallback = [callback copy];

    [_output setSampleBufferDelegate:self queue:_captureQueue];
    [_session startRunning];

    return YES;
}

- (void)stop {
    [_session stopRunning];
    [_output setSampleBufferDelegate:nil queue:_captureQueue];
    [_frameCallback release];
    _frameCallback = nil;
}

- (BOOL)isRunning {
    return _session.isRunning;
}

- (void)captureOutput:(AVCaptureOutput *)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection *)connection {

    if (!_frameCallback) {
        return;
    }

    CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!pixelBuffer) {
        return;
    }

    uint32_t w = static_cast<uint32_t>(CVPixelBufferGetWidth(pixelBuffer));
    uint32_t h = static_cast<uint32_t>(CVPixelBufferGetHeight(pixelBuffer));

    CVPixelBufferRetain(pixelBuffer);
    _frameCallback(pixelBuffer, w, h);
    CVPixelBufferRelease(pixelBuffer);
}

@end
