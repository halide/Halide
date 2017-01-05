//
//  HalideView.m
//  Halide test
//
//  Created by Andrew Adams on 7/23/14.
//  Copyright (c) 2014 Andrew Adams. All rights reserved.
//

#import "HalideView.h"

#include "HalideRuntime.h"
#include "HalideRuntimeMetal.h"

#include "reaction_diffusion_2_init.h"
#include "reaction_diffusion_2_render.h"
#include "reaction_diffusion_2_update.h"

#include <algorithm>

@implementation HalideView
{
@private
    __weak CAMetalLayer *_metalLayer;
    
    BOOL _layerSizeDidUpdate;
    struct buffer_t buf1;
    struct buffer_t buf2;
    struct buffer_t pixel_buf;
    
    int32_t iteration;
    
    double lastFrameTime;
    double frameElapsedEstimate;
}

+ (Class)layerClass
{
    return [CAMetalLayer class];
}

- (void)initCommon
{
    self.opaque          = YES;
    self.backgroundColor = nil;
    
    _metalLayer = (CAMetalLayer *)self.layer;
    _metalLayer.delegate = self;

    _device = MTLCreateSystemDefaultDevice();
    _commandQueue = [_device newCommandQueue];
    
    _metalLayer.device      = _device;
    _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    
    _metalLayer.framebufferOnly = NO;

    buf1 = {0};
    buf2 = {0};
    pixel_buf = {0};
    iteration = 0;
    lastFrameTime = -1;
    frameElapsedEstimate = -1;
}

- (void)didMoveToWindow
{
    self.contentScaleFactor = self.window.screen.nativeScale;
}


- (id)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    
    if(self)
    {
        [self initCommon];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
    self = [super initWithCoder:coder];
    
    if(self)
    {
        [self initCommon];
    }
    return self;
}

- (void)initiateRender {
    // Create autorelease pool per frame to avoid possible deadlock situations
    // because there are 3 CAMetalDrawables sitting in an autorelease pool.
    @autoreleasepool
    {
        id <CAMetalDrawable> drawable = [_metalLayer nextDrawable];
        
        id <MTLTexture> texture = drawable.texture;
 
        float cx;
        float cy;

        // handle display changes here
        if(texture.width != buf1.extent[0] ||
           texture.height != buf1.extent[1]) {
 
            // set the metal layer to the drawable size in case orientation or size changes
            CGSize drawableSize = self.bounds.size;

            drawableSize.width  = ((long)drawableSize.width + 7) & ~7;
            drawableSize.height  = ((long)drawableSize.height + 7) & ~7;

            _metalLayer.drawableSize = drawableSize;
            
            // Free old buffers if size changes.
            halide_device_free((void *)&self, &buf1);
            halide_device_free((void *)&self, &buf2);
            halide_device_free((void *)&self, &pixel_buf);
            free(buf1.host);
            free(buf2.host);
            free(pixel_buf.host);

            cx = drawableSize.width / 2;
            cy = drawableSize.height / 2;
            
            // Make a pair of buffers to represent the current state
            buf1 = {0};
            buf1.extent[0] = (int32_t)drawableSize.width;
            buf1.extent[1] = (int32_t)drawableSize.height;
            buf1.extent[2] = 3;
            buf1.stride[0] = 3;
            buf1.stride[1] = buf1.extent[0] * buf1.stride[0];
            buf1.stride[2] = 1;
            buf1.elem_size = sizeof(float);
            
            buf2 = buf1;
            buf1.host = (uint8_t *)malloc(4 * 3 * buf1.extent[0] * buf1.extent[1]);
            buf2.host = (uint8_t *)malloc(4 * 3 * buf2.extent[0] * buf2.extent[1]);
            // Destination buf must have rows a multiple of 64 bytes for Metal's copyFromBuffer method.
            pixel_buf = {0};
            pixel_buf.extent[0] = buf1.extent[0];
            pixel_buf.extent[1] = buf1.extent[1];
            pixel_buf.stride[0] = 1;
            pixel_buf.stride[1] = (pixel_buf.extent[1] + 63) & ~63;
            pixel_buf.elem_size = sizeof(uint32_t);
            pixel_buf.host = (uint8_t *)malloc(4 * pixel_buf.stride[1] * pixel_buf.extent[1]);

            NSLog(@"Calling reaction_diffusion_2_init size (%u x %u)", buf1.extent[0], buf1.extent[1]);
            reaction_diffusion_2_init((__bridge void *)self, cx, cy, &buf1);
            NSLog(@"Returned from reaction_diffusion_2_init");
            
            iteration = 0;
            lastFrameTime = -1;
            frameElapsedEstimate = -1;
        }
        
        // Grab the current touch position (or leave it far off-screen if there isn't one)
        int tx = -100, ty = -100;
        if (_touch_active) {
            tx = (int)_touch_position.x;
            ty = (int)_touch_position.y;
        }
            
        reaction_diffusion_2_update((__bridge void *)self, &buf1, tx, ty, cx, cy, iteration++, &buf2);
        reaction_diffusion_2_render((__bridge void *)self, &buf2, &pixel_buf);

        std::swap(buf1, buf2);

        id <MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
        id <MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];

        MTLSize image_size;
        image_size.width = pixel_buf.extent[0];
        image_size.height = pixel_buf.extent[1];
        image_size.depth = 1;
        MTLOrigin origin = { 0, 0, 0};

        id <MTLBuffer> buffer = (__bridge id <MTLBuffer>)(void *)halide_metal_get_buffer((void *)&self, &pixel_buf);
        [blitEncoder copyFromBuffer:buffer sourceOffset: 0
            sourceBytesPerRow: pixel_buf.stride[1] * pixel_buf.elem_size
            sourceBytesPerImage: pixel_buf.stride[1] * pixel_buf.extent[1] * pixel_buf.elem_size
            sourceSize: image_size toTexture: texture
                   destinationSlice: 0 destinationLevel: 0 destinationOrigin: origin];
        [blitEncoder endEncoding];
        [commandBuffer addCompletedHandler: ^(id <MTLCommandBuffer>) {
            dispatch_async(dispatch_get_main_queue(), ^(void) {
                [self displayRender:drawable];
            });}];
        [commandBuffer commit];
        [_commandQueue insertDebugCaptureBoundary];
    }
}

- (void)displayRender:(id <MTLDrawable>)drawable
{
    [drawable present];
    double frameTime = CACurrentMediaTime();
    
    if (lastFrameTime == -1) {
        lastFrameTime = frameTime;
    } else {
        double t_elapsed = (frameTime - lastFrameTime) + (frameTime - lastFrameTime);
    
        lastFrameTime = frameTime;

        // Smooth elapsed using an IIR
        if (frameElapsedEstimate == -1) {
            frameElapsedEstimate = t_elapsed;
        } else {
            frameElapsedEstimate = (frameElapsedEstimate * 31 + t_elapsed) / 32.0;
        }

        if ((iteration % 30) == 0) {
            char log_text[2048];
            char *log_text_begin = &(log_text[0]);

            snprintf(log_text_begin, sizeof(log_text),
                     "Halide routine takes %0.3f ms\n", frameElapsedEstimate * 1000);
            [_outputLog setText: [NSString stringWithUTF8String:log_text_begin]];
        }
    }

    [self initiateRender];
}

- (void)setContentScaleFactor:(CGFloat)contentScaleFactor
{
    [super setContentScaleFactor:contentScaleFactor];
}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event {
    self.touch_position = [touches.anyObject locationInView:self];
    self.touch_active = true;
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event {
    self.touch_position = [touches.anyObject locationInView:self];
    self.touch_active = true;
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event {
    self.touch_active = false;
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event {
    self.touch_active = false;
}

@end

extern "C" {

int halide_metal_acquire_context(void *user_context, halide_metal_device **device_ret,
                                 halide_metal_command_queue **queue_ret, bool create) {
    HalideView *view = (__bridge HalideView *)user_context;
    *device_ret = (__bridge halide_metal_device *)view.device;
    *queue_ret = (__bridge halide_metal_command_queue *)view.commandQueue;
    return 0;
}

int halide_metal_release_context(void *user_context) {
    return 0;
}

}
