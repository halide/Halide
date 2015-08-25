//
//  AppDelegate.m
//  Halide test
//
//  Created by Andrew Adams on 6/27/14.
//  Copyright (c) 2014 Andrew Adams. All rights reserved.
//

#import "AppDelegate.h"
#import "HalideView.h"
#include <algorithm>
#include "HalideRuntime.h"
#include "reaction_diffusion_2_init.h"
#include "reaction_diffusion_2_render.h"
#include "reaction_diffusion_2_update.h"

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
        
    // Add a view for image output
    int image_width, image_height;
    HalideView *output_image = [ HalideView alloc ];
    {
        CGRect box = self.window.frame;
        box.origin.x += 10;
        box.origin.y += 30;
        box.size.width -= 20;
        box.size.height -= 100;
        image_width = box.size.width;
        image_height = box.size.height;
        output_image = [ output_image initWithFrame:box ];
        output_image.backgroundColor = [UIColor blackColor];
        [ self.window addSubview: output_image ];
        [ output_image setUserInteractionEnabled:true ];
    }
    
    // Add a view for text output
    UITextView *output_log = [ UITextView alloc ];
    int max_lines;
    {
        CGRect box = self.window.frame;
        box.origin.x += 10;
        box.origin.y += image_height + 40;
        box.size.width -= 20;
        box.size.height -= image_height + 50;
        output_log = [ output_log initWithFrame: box ];
        UIFont *font = [UIFont systemFontOfSize:20];
        [output_log setFont:font ];
        [ self.window addSubview: output_log ];
        max_lines = (int)(box.size.height / font.lineHeight) - 2;
    }
    
    // Override point for customization after application launch.
    self.window.backgroundColor = [UIColor whiteColor];
    [self.window makeKeyAndVisible];
    
    // Start a background task

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
            if (output_image.touch_active) {
                tx = (int)output_image.touch_position.x;
                ty = (int)output_image.touch_position.y;
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

            halide_copy_to_host(nullptr, &pixel_buf);
            
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

            std::swap(buf1, buf2);
            
            if (i % 30 == 0) {
                snprintf(log_text_begin, sizeof(log_text),
                         "Halide routine takes %0.3f ms\n", t_estimate * 1000);
            }
            
            // Update UI by dispatching a task to the UI thread
            dispatch_async(dispatch_get_main_queue(), ^(void) {
                [output_log setText: [NSString stringWithUTF8String:log_text_begin] ];
                
                [output_image setImage:im];
            });
        }
    });
    
    return YES;
}

- (void)applicationWillResignActive:(UIApplication *)application
{
    // Sent when the application is about to move from active to inactive state. This can occur for certain types of temporary interruptions (such as an incoming phone call or SMS message) or when the user quits the application and it begins the transition to the background state.
    // Use this method to pause ongoing tasks, disable timers, and throttle down OpenGL ES frame rates. Games should use this method to pause the game.
}

- (void)applicationDidEnterBackground:(UIApplication *)application
{
    // Use this method to release shared resources, save user data, invalidate timers, and store enough application state information to restore your application to its current state in case it is terminated later. 
    // If your application supports background execution, this method is called instead of applicationWillTerminate: when the user quits.
}

- (void)applicationWillEnterForeground:(UIApplication *)application
{
    // Called as part of the transition from the background to the inactive state; here you can undo many of the changes made on entering the background.
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
    // Restart any tasks that were paused (or not yet started) while the application was inactive. If the application was previously in the background, optionally refresh the user interface.
    
}

- (void)applicationWillTerminate:(UIApplication *)application
{
    // Called when the application is about to terminate. Save data if appropriate. See also applicationDidEnterBackground:.
}


@end
