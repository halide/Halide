//
//  HalideView.m
//  Halide test
//
//  Created by Andrew Adams on 7/23/14.
//  Copyright (c) 2014 Andrew Adams. All rights reserved.
//

#import "HalideView.h"

#include "HalideRuntime.h"
#include "reaction_diffusion_2_init.h"
#include "reaction_diffusion_2_render.h"
#include "reaction_diffusion_2_update.h"

@implementation HalideView

- (id)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    return self;
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

- (void)initiateRender {
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
        buffer_t buf1 = {0};
        buf1.extent[0] = image_width;
        buf1.extent[1] = image_height;
        buf1.extent[2] = 3;
        buf1.stride[0] = 1;
        buf1.stride[1] = image_width;
        buf1.stride[2] = image_width * image_height;
        buf1.elem_size = 4;
        
        buffer_t buf2 = buf1, pixel_buf = buf1;
        buf1.host = (uint8_t *)malloc(4 * 3 * image_width * image_height);
        buf2.host = (uint8_t *)malloc(4 * 3 * image_width * image_height);
        pixel_buf.extent[2] = pixel_buf.stride[2] = 0;
        pixel_buf.host = (uint8_t *)pixels;
        
        double t_estimate = 0.0;
        
        float cx = image_width / 2;
        float cy = image_height / 2;
        
        NSLog(@"Calling reaction_diffusion_2_init");
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

@end
