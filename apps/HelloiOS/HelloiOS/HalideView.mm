#import "HalideView.h"

#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "HalideRuntimeMetal.h"

#include "reaction_diffusion_2_init.h"
#include "reaction_diffusion_2_render.h"
#include "reaction_diffusion_2_update.h"

#include "reaction_diffusion_2_metal_init.h"
#include "reaction_diffusion_2_metal_render.h"
#include "reaction_diffusion_2_metal_update.h"

using Halide::Runtime::Buffer;

struct HalideFuncs {
    int (*init)(const void *, halide_buffer_t *);
    int (*update)(const void *, halide_buffer_t *, int, int, int, halide_buffer_t *);
    int (*render)(const void *, halide_buffer_t *, int, halide_buffer_t *);
};

static const HalideFuncs kHalideCPU = {
    reaction_diffusion_2_init,
    reaction_diffusion_2_update,
    reaction_diffusion_2_render};

static const HalideFuncs kHalideMetal = {
    reaction_diffusion_2_metal_init,
    reaction_diffusion_2_metal_update,
    reaction_diffusion_2_metal_render};

@implementation HalideView {
@private
    __weak CAMetalLayer *_metalLayer;

    Buffer<float> buf1;
    Buffer<float> buf2;
    Buffer<uint8_t> pixel_buf;

    int32_t iteration;
    double frameElapsedEstimate;
}

+ (Class)layerClass {
    return [CAMetalLayer class];
}

- (void)initCommon {
    self.opaque = YES;
    self.backgroundColor = nil;
    self.userInteractionEnabled = YES;

    _metalLayer = (CAMetalLayer *)self.layer;
    _metalLayer.delegate = self;

    _device = MTLCreateSystemDefaultDevice();
    _commandQueue = [_device newCommandQueue];

    _metalLayer.device = _device;
    _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;

    _metalLayer.framebufferOnly = NO;
}

- (void)resetFrameTime {
    iteration = 0;
    frameElapsedEstimate = -1;
}

- (void)updateFrameTime:(double)elapsed using_metal:(bool)using_metal {
    // Smooth elapsed using an IIR
    if (frameElapsedEstimate == -1) {
        frameElapsedEstimate = elapsed;
    } else {
        frameElapsedEstimate = (frameElapsedEstimate * 31 + elapsed) / 32.0;
    }
    if ((iteration % 30) == 0) {
        dispatch_async(dispatch_get_main_queue(), ^(void) {
          [self updateLogWith:self->frameElapsedEstimate using_metal:using_metal];
        });
    }
    iteration += 1;
}

- (void)initBufsWithWidth:(int)w height:(int)h using_metal:(bool)using_metal {
    // Make a pair of buffers to represent the current state
    if (using_metal) {
        buf1 = Buffer<float>::make_interleaved(w, h, 3);
        buf2 = Buffer<float>::make_interleaved(w, h, 3);
    } else {
        buf1 = Buffer<float>(w, h, 3);
        buf2 = Buffer<float>(w, h, 3);
    }

    // We really only need to pad this for the use_metal case,
    // but it doesn't really hurt to always do it.
    const int c = 4;
    const int pad_pixels = (64 / sizeof(int32_t));
    const int row_stride = (w + pad_pixels - 1) & ~(pad_pixels - 1);
    const halide_dimension_t pixelBufShape[] = {
        {0, row_stride, c},
        {0, h, c * row_stride},
        {0, c, 1}};

    // This allows us to make a Buffer with an arbitrary shape
    // and memory managed by Buffer itself
    pixel_buf = Buffer<uint8_t>(nullptr, 3, pixelBufShape);
    pixel_buf.allocate();
    // the blit command requires the padding to exist even on the last row,
    // so we allocate it padded then crop.
    pixel_buf.crop(0, 0, w);
}

- (void)didMoveToWindow {
    self.contentScaleFactor = self.window.screen.nativeScale;
}

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        [self initCommon];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    self = [super initWithCoder:coder];

    if (self) {
        [self initCommon];
    }
    return self;
}

- (void)updateLogWith:(double)elapsedTime using_metal:(bool)using_metal {
    NSString *mode = using_metal ? @"Metal (double-tap for CPU)" : @"CPU (double-tap for Metal)";
    self.statsLabel.text = [NSString stringWithFormat:@"%.1f ms • %@", elapsedTime * 1000, mode];
}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event {
    UITouch *touch = [touches anyObject];
    self.touch_position = [touch locationInView:self];
    self.touch_active = [self pointInside:self.touch_position withEvent:event];
    NSUInteger numTaps = [touch tapCount];
    if (numTaps > 1) {
        self.use_metal = !self.use_metal;
        NSLog(@"TBTaps: %d, self.use_metal %d", (int)numTaps, (int)self.use_metal);
    }
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event {
    self.touch_position = [touches.anyObject locationInView:self];
    self.touch_active = [self pointInside:self.touch_position withEvent:event];
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event {
    self.touch_active = false;
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event {
    self.touch_active = false;
}

- (void)renderOneFrame:(const HalideFuncs &)halide_funcs using_metal:(bool)using_metal {
    // -1 is interpreted as no touch event
    int tx = -1, ty = -1;
    if (self.touch_active) {
        // Note that buf/bounds is not necessarily the same as self.contentScaleFactor
        tx = (int)(self.touch_position.x * buf1.dim(0).extent() / self.bounds.size.width);
        ty = (int)(self.touch_position.y * buf1.dim(1).extent() / self.bounds.size.height);
        NSLog(@"touch %d %d", tx, ty);
    }

    const bool output_bgra = true;

    // A note on timing: based on our experimentation, this is indeed effective for
    // timing Metal launches, not just CPU kernels. Other GPU API implementations
    // may return way before actually completing kernel execution, but Metal
    // (at least in this context) doesn't seem to, making this basic timing approach
    // fairly effective.
    //
    // However, there seems to be a large minimum latency to return from the Metal launches,
    // which can make this an underestimate of the potential GPU throughput; for example,
    // running the update and render steps 10 times per frame (instead of once)
    // converges to a steady state per-frame cost which is often much less than
    // the single iteration cost.

    double t_before = CACurrentMediaTime();
    halide_funcs.update((__bridge void *)self, buf1, tx, ty, iteration, buf2);
    halide_funcs.render((__bridge void *)self, buf2, output_bgra, pixel_buf);
    double t_after = CACurrentMediaTime();

    std::swap(buf1, buf2);

    [self updateFrameTime:(t_after - t_before) using_metal:using_metal];
}

- (void)initiateRender {
    bool using_metal = self.use_metal;
    const HalideFuncs &halide_funcs = using_metal ? kHalideMetal : kHalideCPU;
    const int required_stride = using_metal ? 3 : 1;

    // Create autorelease pool per frame to avoid possible deadlock situations
    // because there are 3 CAMetalDrawables sitting in an autorelease pool.
    @autoreleasepool {
        id<CAMetalDrawable> drawable = [_metalLayer nextDrawable];
        id<MTLTexture> texture = drawable.texture;

        // handle display changes here
        if (!buf1.data() ||
            texture.width != buf1.dim(0).extent() ||
            texture.height != buf1.dim(1).extent() ||
            buf1.dim(0).stride() != required_stride) {

            // set the metal layer to the drawable size in case orientation or size changes
            CGSize drawableSize = self.bounds.size;
            _metalLayer.drawableSize = drawableSize;

            [self initBufsWithWidth:drawableSize.width height:drawableSize.height using_metal:using_metal];
            halide_funcs.init((__bridge void *)self, buf1);

            [self resetFrameTime];
        }

        [self renderOneFrame:halide_funcs using_metal:using_metal];

        [self renderOneFrame:halide_funcs using_metal:using_metal];

        id<MTLBuffer> buffer = using_metal ?
                                   (__bridge id<MTLBuffer>)(void *)halide_metal_get_buffer((void *)&self, pixel_buf) :
                                   [self.device newBufferWithBytes:pixel_buf.data()
                                                            length:pixel_buf.size_in_bytes()
                                                           options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
        id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];

        MTLSize image_size;
        image_size.width = pixel_buf.dim(0).extent();
        image_size.height = pixel_buf.dim(1).extent();
        image_size.depth = 1;
        MTLOrigin origin = {0, 0, 0};

        const int bytesPerRow = pixel_buf.dim(1).stride() * pixel_buf.type().bits / 8;
        [blitEncoder
                 copyFromBuffer:buffer
                   sourceOffset:0
              sourceBytesPerRow:bytesPerRow
            sourceBytesPerImage:pixel_buf.dim(1).stride() * pixel_buf.dim(1).extent()  // Not sizeInBytes because we're also copying the padding on the last row
                     sourceSize:image_size
                      toTexture:drawable.texture
               destinationSlice:0
               destinationLevel:0
              destinationOrigin:origin];
        [blitEncoder endEncoding];
        [commandBuffer addCompletedHandler:^(id MTLCommandBuffer) {
          dispatch_async(dispatch_get_main_queue(), ^(void) {
            [drawable present];
            [self initiateRender];
          });
        }];
        [commandBuffer commit];
    }
}

@end

#ifdef __cplusplus
extern "C" {
#endif

int halide_metal_acquire_context(void *user_context, struct halide_metal_device **device_ret,
                                 struct halide_metal_command_queue **queue_ret, bool create) {
    HalideView *view = (__bridge HalideView *)user_context;
    *device_ret = (__bridge struct halide_metal_device *)view.device;
    *queue_ret = (__bridge struct halide_metal_command_queue *)view.commandQueue;
    return 0;
}

int halide_metal_release_context(void *user_context) {
    return 0;
}

#ifdef __cplusplus
}
#endif
