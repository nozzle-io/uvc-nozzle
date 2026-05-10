#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

@interface UvcCaptureSession : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
- (nullable instancetype)initWithDevice:(AVCaptureDevice * _Nonnull)device;
- (BOOL)configureWithWidth:(uint32_t)width height:(uint32_t)height fps:(uint32_t)fps;
- (BOOL)startWithCallback:(void (^ _Nonnull)(void * _Nonnull pixelBuffer, uint32_t w, uint32_t h))callback;
- (void)stop;
- (BOOL)isRunning;
@end
