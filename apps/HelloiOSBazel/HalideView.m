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
#include "reaction_diffusion_2_metal_init.h"
#include "reaction_diffusion_2_metal_render.h"
#include "reaction_diffusion_2_metal_update.h"


@implementation HalideView
{
@private
#if HAS_METAL_SDK
    __weak CAMetalLayer *_metalLayer;
#endif  // HAS_METAL_SDK
    
    struct buffer_t buf1;
    struct buffer_t buf2;
    struct buffer_t pixel_buf;
    
    int32_t iteration;
    
    double lastFrameTime;
    double frameElapsedEstimate;
}


#if HAS_METAL_SDK
+ (Class)layerClass
{
    return [CAMetalLayer class];
}
#endif  // HAS_METAL_SDK

- (void)initCommon
{
    self.opaque          = YES;
    self.backgroundColor = nil;
    
#if HAS_METAL_SDK
    _metalLayer = (CAMetalLayer *)self.layer;
    _metalLayer.delegate = self;

    _device = MTLCreateSystemDefaultDevice();
    _commandQueue = [_device newCommandQueue];
    
    _metalLayer.device      = _device;
    _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    
    _metalLayer.framebufferOnly = NO;
#endif  // HAS_METAL_SDK

    memset(&buf1, 0, sizeof(buf1));
    memset(&buf2, 0, sizeof(buf2));
    memset(&pixel_buf, 0, sizeof(pixel_buf));
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

#if HAS_METAL_SDK
- (void)initiateRenderMetal {
    NSLog(@"initiateRenderMetal");
    // Create autorelease pool per frame to avoid possible deadlock situations
    // because there are 3 CAMetalDrawables sitting in an autorelease pool.
    @autoreleasepool
    {
        id <CAMetalDrawable> drawable = [_metalLayer nextDrawable];
        
        id <MTLTexture> texture = drawable.texture;
 
        float cx = 0.f;
        float cy = 0.f;

        // handle display changes here
        if (texture.width != buf1.extent[0] ||
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
            memset(&buf1, 0, sizeof(buf1));
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
            memset(&pixel_buf, 0, sizeof(pixel_buf));
            pixel_buf.extent[0] = buf1.extent[0];
            pixel_buf.extent[1] = buf1.extent[1];
            pixel_buf.stride[0] = 1;
            pixel_buf.stride[1] = (pixel_buf.extent[1] + 63) & ~63;
            pixel_buf.elem_size = sizeof(uint32_t);
            pixel_buf.host = (uint8_t *)malloc(4 * pixel_buf.stride[1] * pixel_buf.extent[1]);

            NSLog(@"Calling reaction_diffusion_2_metal_init size (%u x %u)", buf1.extent[0], buf1.extent[1]);
            reaction_diffusion_2_metal_init((__bridge void *)self, cx, cy, &buf1);
            NSLog(@"Returned from reaction_diffusion_2_metal_init");
            
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
            
        NSLog(@"Calling reaction_diffusion_2_metal_update size (%u x %u)", buf1.extent[0], buf1.extent[1]);
        reaction_diffusion_2_metal_update((__bridge void *)self, &buf1, tx, ty, cx, cy, iteration++, &buf2);
        NSLog(@"Returned from reaction_diffusion_2_metal_update");

        NSLog(@"Calling reaction_diffusion_2_metal_render size (%u x %u)", buf2.extent[0], buf2.extent[1]);
        reaction_diffusion_2_metal_render((__bridge void *)self, &buf2, &pixel_buf);
        NSLog(@"Returned from reaction_diffusion_2_metal_render");

        buffer_t tmp;
        tmp = buf1; buf1 = buf2; buf2 = tmp;

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
        [commandBuffer addCompletedHandler: ^(id MTLCommandBuffer) {
            dispatch_async(dispatch_get_main_queue(), ^(void) {
                [self displayRender:drawable];
            });}];
        [commandBuffer commit];
        [_commandQueue insertDebugCaptureBoundary];
    }
}
#endif  // HAS_METAL_SDK

- (void)initiateRenderCPU {
    NSLog(@"initiateRenderCPU");

    // Start a background task

    CGRect box = self.window.frame;
    int image_width = box.size.width;
    int image_height = box.size.height;

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        char log_text[2048];
        char *log_text_begin = &(log_text[0]);
        
        // Make a frame buffer
        uint32_t *pixels = (uint32_t *)malloc(4*image_width*image_height);
        
        CGDataProviderRef provider =
            CGDataProviderCreateWithData(NULL, pixels, image_width * image_height * 4, NULL);

        CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
        
        // Make a pair of buffers to represent the current state
        memset(&buf1, 0, sizeof(buf1));
        buf1.extent[0] = image_width;
        buf1.extent[1] = image_height;
        buf1.extent[2] = 3;
        buf1.stride[0] = 1;
        buf1.stride[1] = image_width;
        buf1.stride[2] = image_width * image_height;
        buf1.elem_size = 4;
        
        buf2 = buf1, pixel_buf = buf1;
        buf1.host = (uint8_t *)malloc(4 * 3 * image_width * image_height);
        buf2.host = (uint8_t *)malloc(4 * 3 * image_width * image_height);
        pixel_buf.extent[2] = pixel_buf.stride[2] = 0;
        pixel_buf.host = (uint8_t *)pixels;
        
        double t_estimate = 0.0;
        
        float cx = image_width / 2;
        float cy = image_height / 2;
        
        NSLog(@"Calling reaction_diffusion_2_init z"); 
        reaction_diffusion_2_init(cx, cy, &buf1);
        NSLog(@"Returned from reaction_diffusion_2_init");
   
        for (int i = 0; ;i++) {
  
            // Grab the current touch position (or leave it far off-screen if there isn't one)
            int tx = -100, ty = -100;
            if (self.touch_active) {
                tx = (int)self.touch_position.x;
                ty = (int)self.touch_position.y;
            }
            
            //NSLog(@"Calling reaction_diffusion_2_update");
            double t_before_update = CACurrentMediaTime();
            reaction_diffusion_2_update(&buf1, tx, ty, cx, cy, i, &buf2);
            double t_after_update = CACurrentMediaTime();
            //NSLog(@"Returned from reaction_diffusion_2_update");
          
            //NSLog(@"Calling reaction_diffusion_2_render");
            double t_before_render = CACurrentMediaTime();
            reaction_diffusion_2_render(&buf2, &pixel_buf);
            double t_after_render = CACurrentMediaTime();
            //NSLog(@"Returned from reaction_diffusion_2_render");

            halide_copy_to_host(NULL, &pixel_buf);
            
            double t_elapsed = (t_after_update - t_before_update) + (t_after_render - t_before_render);
            
            // Smooth elapsed using an IIR
            if (i == 0) t_estimate = t_elapsed;
            else t_estimate = (t_estimate * 31 + t_elapsed) / 32.0;
            
            CGImageRef image_ref =
                CGImageCreate(image_width, image_height, 8, 32, 4*image_width,
                              color_space,
                              kCGBitmapByteOrderDefault,
                              provider, NULL, NO,
                              kCGRenderingIntentDefault);
            
            UIImage *im = [UIImage imageWithCGImage:image_ref];
            
            CGImageRelease(image_ref);

            buffer_t tmp;
            tmp = buf1; buf1 = buf2; buf2 = tmp;
            
            if (i % 30 == 0) {
                snprintf(log_text_begin, sizeof(log_text),
                         "Halide routine takes %0.3f ms\n", t_estimate * 1000);
            }
            
            // Update UI by dispatching a task to the UI thread
            dispatch_async(dispatch_get_main_queue(), ^(void) {
                [self.outputLog setText: [NSString stringWithUTF8String:log_text_begin] ];
                [self setImage:im];
            });
        }
    });
}

- (void)initiateRender {
    NSLog(@"initiateRender");
#if HAS_METAL_SDK
    if (self.use_metal) {
        [self initiateRenderMetal];
        return;
    }
#endif

    [self initiateRenderCPU];
}

#if HAS_METAL_SDK
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

#endif  // HAS_METAL_SDK

@end

#if HAS_METAL_SDK

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

#endif  // HAS_METAL_SDK
