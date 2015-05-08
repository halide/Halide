//
//  Created by abstephens on 1/20/15.
//  Copyright (c) 2015 Google. All rights reserved.
//

#import <Cocoa/Cocoa.h>

#import "AppDelegate.h"

int main(int argc, char * argv[]) {

  NSApplication *app = [NSApplication sharedApplication];

  AppDelegate *delegate = [[AppDelegate alloc] init];
  app.delegate = delegate;

  [app run];
}
