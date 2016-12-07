//
//  HalideView.h
//  Halide test
//
//  Created by Andrew Adams on 7/23/14.
//  Copyright (c) 2014 Andrew Adams. All rights reserved.
//

#include <TargetConditionals.h>

#if __IPHONE_OS_VERSION_MIN_REQUIRED >= __IPHONE_8_0 && !(TARGET_IPHONE_SIMULATOR || TARGET_OS_SIMULATOR)
#define HAS_METAL_SDK 1
#else
#define HAS_METAL_SDK 0
#endif

#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

#if HAS_METAL_SDK
#import <Metal/Metal.h>
#endif  // HAS_METAL_SDK

@interface HalideView : UIImageView

@property CGPoint touch_position;
@property bool touch_active;
@property bool use_metal;
@property UITextView *outputLog;

#if HAS_METAL_SDK
// view has a handle to the metal device when created
@property (nonatomic, readonly) id <MTLDevice> device;
// view has a handle to the metal device when created
@property (nonatomic, readonly) id <MTLCommandQueue> commandQueue;
#endif  // HAS_METAL_SDK

- (void)initiateRender;

#if HAS_METAL_SDK
- (void)displayRender:(id <MTLDrawable>)drawable;
#endif  // HAS_METAL_SDK

@end
