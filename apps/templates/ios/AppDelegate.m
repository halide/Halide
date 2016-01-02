//
//  Created by abstephens on 1/20/15.
//  Copyright (c) 2015 Google. All rights reserved.
//

#import "AppDelegate.h"
#import "AppProtocol.h"
#import "ViewController.h"

@interface AppDelegate ()

@end

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {

  // Register the app custom URL scheme for processing requests from itself
  [NSURLProtocol registerClass:[AppProtocol class]];

  CGRect mainFrame = [UIScreen mainScreen].applicationFrame;

  self.window = [[UIWindow alloc] initWithFrame:mainFrame];

  self.window.backgroundColor = [UIColor redColor];
  self.window.hidden = NO;

  self.window.rootViewController = [[ViewController alloc] init];

  return YES;
}

@end
