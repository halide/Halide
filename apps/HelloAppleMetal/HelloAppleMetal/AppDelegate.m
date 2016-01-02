//
//  AppDelegate.m
//  Halide test
//
//  Created by Andrew Adams on 6/27/14.
//  Copyright (c) 2014 Andrew Adams. All rights reserved.
//

#import "AppDelegate.h"
#import "HalideView.h"
#import "HalideViewController.h"
#include <algorithm>
#include "HalideRuntime.h"

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    HalideViewController *halide_view_controller = [[HalideViewController alloc] init];
    self.window.rootViewController = halide_view_controller;
    
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
    halide_view_controller.halide_view = output_image;
    
    // Add a view for text output
    UITextView *output_log = [ UITextView alloc ];
    output_image.outputLog = output_log;
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
